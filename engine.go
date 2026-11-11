package main

// Moteur de Layers Keys : 8 canaux de sons superposables, mixage stéréo.

import (
	"fmt"
	"math"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"
)

const nCh = 8

// ---------------------------------------------------------------- réglages d'un son

const (
	pCutoff = iota
	pBright
	pReso
	pAttack
	pRelease
	pDyn
	pVib
	pRate
	nParam
)

var paramNames = [nParam]string{"CUTOFF", "BRILLANCE", "RÉSONANCE", "ATTAQUE", "RELÂCHEMENT", "DYNAMIQUE", "VIBRATO", "VITESSE"}
var paramDefault = [nParam]float64{1, 0, 0, 0, 0, 0.5, 0, 0.4526}

func cutoffHz(x float64) float64  { return 20 * math.Pow(1000, x) }
func attackSec(x float64) float64 { return 2.0 * x * x }

// durée ajoutée au relâchement (temps pour descendre de 60 dB) : 0 → rien, milieu ≈ 3,5 s, maximum 10 s
func releaseSec(x float64) float64 { return 10.0 * math.Pow(clamp01(x), 1.5) }
func dynDB(x float64) float64      { return 48 * x }
func vibCents(x float64) float64   { return 60 * x }
func rateHz(x float64) float64     { return 0.5 + 9.5*x }
func resoDB(x float64) float64     { return 18 * x }

func paramText(i int, x float64) string {
	switch i {
	case pCutoff:
		if x > 0.995 {
			return "OUVERT"
		}
		hz := cutoffHz(x)
		if hz >= 1000 {
			return fmt.Sprintf("%.1f kHz", hz/1000)
		}
		return fmt.Sprintf("%.0f Hz", hz)
	case pBright:
		if x < 0.005 {
			return "OFF"
		}
		return fmt.Sprintf("%.0f%%", x*100)
	case pReso:
		return fmt.Sprintf("%.0f%%", x*100)
	case pAttack:
		return fmt.Sprintf("+%.0f ms", attackSec(x)*1000)
	case pRelease:
		if r := releaseSec(x); r >= 1 {
			return fmt.Sprintf("+%.1f s", r)
		}
		return fmt.Sprintf("+%.0f ms", releaseSec(x)*1000)
	case pDyn:
		return fmt.Sprintf("%.0f dB", dynDB(x))
	case pVib:
		if x < 0.005 {
			return "OFF"
		}
		return fmt.Sprintf("%.0f cts", vibCents(x))
	case pRate:
		return fmt.Sprintf("%.1f Hz", rateHz(x))
	}
	return ""
}

// ---------------------------------------------------------------- canal

type Channel struct {
	idx  int
	src  Source
	Name string
	Cat  string

	Vol        float64 // dB (-60 = coupé)
	Pan        float64 // -1..1
	Oct, Trans int
	Mute, Solo bool
	Hold       bool
	Pedal      bool // réagit à la pédale sustain
	Filter     bool // filtre passe-bas actif
	AT         bool // l'aftertouch (pression sur les touches) agit sur cette piste
	ModFX      bool // la molette de modulation commande l'effet « planant » de cette piste
	P          [nParam]float64

	peak    float64 // niveau pour le vumètre
	curGain float64
}

func newChannel(i int) *Channel {
	c := &Channel{idx: i, Pedal: true, Filter: true, AT: true, P: paramDefault, curGain: 1}
	return c
}

func (c *Channel) Source() Source { return c.src }
func (c *Channel) Peak() float64  { return c.peak }

// ---------------------------------------------------------------- source de sons

type Source interface {
	// crée les voix pour la note n (après transposition) et la vitesse vel (1..127)
	Spawn(e *Engine, ch *Channel, n, vel int) []voiceI
}

type builtinSource struct{ inst int }

func (s builtinSource) Spawn(e *Engine, ch *Channel, n, vel int) []voiceI {
	v := float64(vel) / 127
	switch s.inst {
	case iGrand, iUpright:
		return []voiceI{newPiano(s.inst, n, v)}
	case iPad, iLead, iSquare:
		sv := newSynth(s.inst, n, v)
		sv.vib += vibCents(ch.P[iVibIdx()]) * 0.000578
		if ch.P[pVib] > 0.005 {
			sv.lfoRate = rateHz(ch.P[pRate])
		}
		return []voiceI{sv}
	case iGuitar:
		sv := &synVoice{kind: kPluck, buf: e.pluck(iGuitar, fold(n, 40, 88)), gain: instGain[iGuitar] * (0.15 + 0.85*v)}
		sv.rel = 0.22
		return []voiceI{sv}
	default:
		sv := &synVoice{kind: kPluck, buf: e.pluck(iBass, fold(n-12, 28, 67)), gain: instGain[iBass] * (0.15 + 0.85*v)}
		sv.rel = 0.20
		return []voiceI{sv}
	}
}

func iVibIdx() int { return pVib }

// ---------------------------------------------------------------- moteur

type Engine struct {
	licLocked   int32  // 1 : logiciel non autorisé, aucune note ne sonne
	licDeadline int64  // fin de la démo (ns Unix), 0 = pas de limite
	pulled      int64  // nombre d'images sonores demandées par la carte son (diagnostic)
	outPeakBits uint64 // plus fort niveau sorti du moteur depuis la dernière lecture (diagnostic)
	toneLeft    int64  // images restantes du bip de test (contourne licence, pistes et effets)
	leslieFast  int32  // vitesse du Leslie de toutes les pistes : 1 = rapide (atomique)
	tonePhase   float64
	Player      *Player
	Pads        *padPlayer // pads continus (pads.go)
	cpuBits     uint64     // charge moyenne du calcul audio (0..1, en 64 bits)
	cpuPeak     uint64
	slowN       int64  // blocs audio dont le calcul a pris plus de 70 % du temps disponible (diagnostic)
	slowMax     uint64 // pire bloc (en millièmes du temps disponible)
	voiceLimit  int32  // nombre maximal de notes en même temps (baisse toute seule si le PC est surchargé)
	calm        int
	liveCount   int32
	fxp         [nCh]unsafe.Pointer // *FXCfg par piste : réglages des effets (lus par le thread audio)
	fx          [nCh]*fxChain
	fxNew       [nCh]unsafe.Pointer // chaîne d'effets préparée par l'interface (évite d'allouer dans le thread audio)
	chL, chR    [nCh][]float64      // sortie d'une piste qui a des effets, avant mélange
	limDrop     float64             // réduction de gain actuelle du limiteur (0 = aucune)
	limReq      []float64
	xrun, cool  int
	cutHeld     int64 // notes encore tenues qui ont dû être coupées (compteur de diagnostic)
	mu          sync.Mutex
	ch          [nCh]*Channel
	voices      []voiceI

	master, cc7, cc11 float64
	revAmt            float64
	bend, mod         float64
	bendS, modS       float64 // molettes lissées (évite les « marches d'escalier » audibles)
	at, atS           float64 // aftertouch reçu / aftertouch lissé (0..1)
	atMode            int     // 0 vibrato + brillance, 1 vibrato, 2 brillance, 3 désactivé
	atAmt             float64
	velTab            atomic.Pointer[[128]uint8]
	bendRaw           int
	revHold           int
	send              []float64
	sustain, sost     bool
	soft              bool
	transpose, octave int

	rev *reverb

	cacheMu sync.Mutex
	cache   map[[2]int][]float32
	warming [nInst]bool

	tmp, mixL, mixR []float64
}

func NewEngine() *Engine {
	e := &Engine{voiceLimit: maxVoice, master: 0.8, cc7: 1, cc11: 1, bend: 1, revAmt: 0.15, atAmt: 1, cache: map[[2]int][]float32{}, rev: newReverb(), Player: newPlayer(), Pads: newPadPlayer()}
	for i := range e.ch {
		e.ch[i] = newChannel(i)
	}
	e.Pads.gate = e.soundAllowed
	return e
}

func (e *Engine) Channel(i int) *Channel { return e.ch[i] }

func (e *Engine) pluck(inst, n int) []float32 {
	key := [2]int{inst, n}
	e.cacheMu.Lock()
	b, ok := e.cache[key]
	e.cacheMu.Unlock()
	if ok {
		return b
	}
	b = renderPluck(inst, n)
	e.cacheMu.Lock()
	e.cache[key] = b
	e.cacheMu.Unlock()
	return b
}

// Prépare en arrière-plan les sons de cordes (évite un petit délai à la première note)
func (e *Engine) Warm(inst int) {
	if inst != iGuitar && inst != iBass {
		return
	}
	e.cacheMu.Lock()
	if e.warming[inst] {
		e.cacheMu.Unlock()
		return
	}
	e.warming[inst] = true
	e.cacheMu.Unlock()
	lo, hi := 40, 88
	if inst == iBass {
		lo, hi = 28, 67
	}
	go func() {
		for d := 0; d <= 30; d++ {
			for _, n := range []int{60 + d, 60 - d} {
				if n >= lo && n <= hi {
					e.pluck(inst, n)
				}
			}
		}
	}()
}

// remplace le son d'un canal
func (e *Engine) SetSource(i int, src Source, name, cat string) {
	e.mu.Lock()
	c := e.ch[i]
	c.src, c.Name, c.Cat = src, name, cat
	e.mu.Unlock()
	if b, ok := src.(builtinSource); ok {
		e.Warm(b.inst)
	}
}

// son, nom et catégorie d'un canal (lecture protégée : un chargement peut les changer à tout moment)
func (e *Engine) ChInfo(i int) (Source, string, string) {
	e.mu.Lock()
	c := e.ch[i]
	src, name, cat := c.src, c.Name, c.Cat
	e.mu.Unlock()
	return src, name, cat
}

func (e *Engine) SetMaster(v float64) { e.master = v }
func (e *Engine) SetReverb(v float64) { e.revAmt = v }
func (e *Engine) SetBend(value int) {
	e.bendRaw = value
	e.bend = math.Pow(2, float64(value)/8192*2/12)
}

// valeurs actuelles des deux molettes (pitch : -8192..8191, modulation : 0..1)
func (e *Engine) Bend() int         { return e.bendRaw }
func (e *Engine) ModWheel() float64 { return e.mod }
func (e *Engine) SetMod(v float64)  { e.mod = clampf(v, 0, 1) }

// part de la molette de modulation qui sert de vibrato pour une piste
func (e *Engine) vibWheel(ch *Channel) float64 {
	if ch.ModFX {
		return e.modS * 0.12 // avec l'effet planant, la molette ouvre le son : seulement un léger mouvement
	}
	return e.modS
}

// ---------------------------------------------------------------- aftertouch (pression sur les touches)

// SetAftertouch : pression « de canal » (0..127), la même pour toutes les touches
func (e *Engine) SetAftertouch(v int) { e.at = clampf(float64(v)/127, 0, 1) }

// PolyPressure : pression d'une seule touche
func (e *Engine) PolyPressure(tag, v int) {
	e.mu.Lock()
	for _, vo := range e.voices {
		if b := vo.base(); b.tag == tag && b.keyDown {
			b.press = clampf(float64(v)/127, 0, 1)
		}
	}
	e.mu.Unlock()
}

func (e *Engine) SetAftertouchMode(mode int, amt float64) {
	e.atMode = clamp(mode, 0, 3)
	e.atAmt = clampf(amt, 0.1, 2)
}

// pression effective sur une voix (0 si l'aftertouch est coupé pour sa piste ou en général)
func (e *Engine) pressureOf(b *vbase) float64 {
	if !b.ch.AT || e.atMode == 3 {
		return 0
	}
	p := math.Max(e.atS, b.press)
	return clampf(p*e.atAmt, 0, 1.5)
}

// vibrato demandé à une voix : molette de modulation + pression
func (e *Engine) vibFor(b *vbase) float64 {
	w := e.vibWheel(b.ch)
	if e.atMode == 0 || e.atMode == 1 {
		w += 0.7 * e.pressureOf(b)
	}
	return w
}

// ouverture du son (molette ou pression) : la plus forte des deux
func (e *Engine) openFor(b *vbase) float64 {
	m := e.modS
	if e.atMode == 0 || e.atMode == 2 {
		m = math.Max(m, e.pressureOf(b))
	}
	return clampf(m, 0, 1)
}

// ---------------------------------------------------------------- courbe de vélocité

// SetVelCurve : gamma < 1 = une frappe légère sonne plus fort (réglage « Dure » de l'écran), > 1 = le son reste plus doux (« Douce ») ; lo/hi = limites
func (e *Engine) SetVelCurve(gamma float64, lo, hi int) {
	var t [128]uint8
	lo, hi = clamp(lo, 1, 127), clamp(hi, 1, 127)
	if hi < lo {
		hi = lo
	}
	gamma = clampf(gamma, 0.3, 3)
	for v := 1; v < 128; v++ {
		x := math.Pow(float64(v-1)/126, gamma)
		t[v] = uint8(clamp(int(math.Round(float64(lo)+(float64(hi-lo))*x)), 1, 127))
	}
	e.velTab.Store(&t)
}

// MapVel applique la courbe à une vélocité reçue d'un clavier MIDI
func (e *Engine) MapVel(v int) int {
	t := e.velTab.Load()
	if t == nil || v < 1 || v > 127 {
		return v
	}
	return int(t[v])
}

func (e *Engine) soloAny() bool {
	for _, c := range e.ch {
		if c.Solo {
			return true
		}
	}
	return false
}

// SetLicenseGate : verrouille le son (licence absente ou démo finie) ou fixe l'heure de fin de démo.
func (e *Engine) SetLicenseGate(locked bool, deadline time.Time) {
	var l int32
	if locked {
		l = 1
	}
	var d int64
	if !deadline.IsZero() {
		d = deadline.UnixNano()
	}
	atomic.StoreInt64(&e.licDeadline, d)
	atomic.StoreInt32(&e.licLocked, l)
}

func (e *Engine) soundAllowed() bool {
	if atomic.LoadInt32(&e.licLocked) != 0 {
		return false
	}
	if d := atomic.LoadInt64(&e.licDeadline); d != 0 && time.Now().UnixNano() > d {
		return false
	}
	return true
}

func (e *Engine) NoteOn(note, vel, tag int) {
	if vel <= 0 {
		e.NoteOff(tag)
		return
	}
	if !e.soundAllowed() {
		return
	}
	if e.soft {
		vel = int(float64(vel) * 0.7)
		if vel < 1 {
			vel = 1
		}
	}
	solo := e.soloAny()
	var nv []voiceI
	for _, ch := range e.ch {
		src, _, _ := e.ChInfo(ch.idx)
		if src == nil || ch.Mute || (solo && !ch.Solo) || ch.Vol <= -59.9 {
			continue
		}
		n := note + e.transpose + 12*e.octave + ch.Trans + 12*ch.Oct
		if n < 0 || n > 127 {
			continue
		}
		for _, v := range src.Spawn(e, ch, n, vel) {
			b := v.base()
			b.ch = ch
			b.tag = tag
			b.keyDown = true
			b.extraAtt = attackSec(ch.P[pAttack])
			extra := releaseSec(ch.P[pRelease])
			if _, ok := v.(*sampVoice); ok {
				extra /= 0.6 // les échantillons descendent de 100 dB sur la durée : on convertit en durée « 60 dB »
			}
			b.rel += extra
			nv = append(nv, v)
		}
	}
	if len(nv) == 0 {
		return
	}
	e.mu.Lock()
	// classes exclusives (ex. charley ouvert/fermé) : la nouvelle note coupe les anciennes
	for _, v := range nv {
		b := v.base()
		if b.excl == 0 {
			continue
		}
		for _, o := range e.voices {
			ob := o.base()
			if ob.ch == b.ch && ob.excl == b.excl && !ob.released {
				ob.rel = 0.03
				ob.keyDown, ob.pedalHold, ob.sostHold = false, false, false
				ob.release()
			}
		}
	}
	e.voices = append(e.voices, nv...)
	live := 0
	for _, o := range e.voices {
		if !o.base().evicted {
			live++
		}
	}
	if ex := live - int(atomic.LoadInt32(&e.voiceLimit)); ex > 0 {
		// on coupe en douceur les plus anciennes notes : d'abord celles déjà relâchées
		cnt := 0
		for pass := 0; pass < 2 && cnt < ex; pass++ {
			for _, o := range e.voices {
				if cnt >= ex {
					break
				}
				ob := o.base()
				if !ob.evicted && ob.released == (pass == 0) {
					if pass == 1 {
						atomic.AddInt64(&e.cutHeld, 1)
					}
					ob.evicted = true
					ob.rel = 0.04
					ob.release()
					cnt++
				}
			}
		}
	}
	e.mu.Unlock()
}

// la voix doit-elle rester tenue (pédale ou hold du canal) ?
func (e *Engine) held(b *vbase) bool {
	return (e.sustain && b.ch.Pedal) || b.ch.Hold
}

func (e *Engine) NoteOff(tag int) {
	e.mu.Lock()
	for _, v := range e.voices {
		b := v.base()
		if b.tag == tag && b.keyDown {
			b.keyDown = false
			if e.held(b) {
				b.pedalHold = true
			}
			if !(b.pedalHold || b.sostHold) {
				b.release()
			}
		}
	}
	e.mu.Unlock()
}

// libère les voix qui n'étaient tenues que par la pédale / le hold
func (e *Engine) releaseHeld() {
	for _, v := range e.voices {
		b := v.base()
		if b.pedalHold && !e.held(b) {
			b.pedalHold = false
			if !b.keyDown && !b.sostHold {
				b.release()
			}
		}
	}
}

func (e *Engine) SetSustain(down bool) {
	e.mu.Lock()
	e.sustain = down
	e.releaseHeld()
	e.mu.Unlock()
}

func (e *Engine) SetHold(i int, on bool) {
	e.mu.Lock()
	e.ch[i].Hold = on
	e.releaseHeld()
	e.mu.Unlock()
}

func (e *Engine) SetPedalAware(i int, on bool) {
	e.mu.Lock()
	e.ch[i].Pedal = on
	e.releaseHeld()
	e.mu.Unlock()
}

func (e *Engine) SetSostenuto(down bool) {
	e.mu.Lock()
	e.sost = down
	for _, v := range e.voices {
		b := v.base()
		if down {
			if b.keyDown && !b.released {
				b.sostHold = true
			}
		} else if b.sostHold {
			b.sostHold = false
			if !b.keyDown && !b.pedalHold {
				b.release()
			}
		}
	}
	e.mu.Unlock()
}

func (e *Engine) Control(cc, val int) {
	switch cc {
	case 64:
		e.SetSustain(val >= 64)
	case 66:
		e.SetSostenuto(val >= 64)
	case 67:
		e.soft = val >= 64
	case 1:
		e.mod = float64(val) / 127
	case 7:
		e.cc7 = float64(val) / 127
	case 11:
		e.cc11 = float64(val) / 127
	case 120:
		e.Panic()
	case 123:
		e.AllNotesOff()
	case 121:
		e.bend, e.mod, e.soft, e.at = 1, 0, false, 0
		e.SetSustain(false)
		e.SetSostenuto(false)
	}
}

func (e *Engine) AllNotesOff() {
	e.mu.Lock()
	for _, v := range e.voices {
		b := v.base()
		b.keyDown, b.pedalHold, b.sostHold = false, false, false
		b.release()
	}
	e.mu.Unlock()
}

func (e *Engine) Panic() {
	e.mu.Lock()
	e.voices = nil
	e.sustain, e.sost = false, false
	e.mu.Unlock()
}

func (e *Engine) VoiceCount() int {
	e.mu.Lock()
	defer e.mu.Unlock()
	return len(e.voices)
}

func (e *Engine) applyVoiceFilter(b *vbase, buf []float64) {
	ch := b.ch
	fc, q := 22000.0, 0.0
	if b.sfFc > 0 {
		fc, q = math.Min(fc, b.sfFc), b.sfQ
	}
	if ch.ModFX {
		// effet « planant » : le son est plus doux au repos, la molette l'ouvre (plus de clarté) avec un peu de résonance
		m := e.openFor(b)
		u := cutoffHz(ch.P[pCutoff]) * math.Pow(2, -2.6*(1-m)+4*ch.P[pBright]*m)
		if u < fc {
			fc = u
		}
		q += resoDB(ch.P[pReso]) + 3*math.Sin(math.Pi*m)
	} else if ch.Filter {
		u := cutoffHz(ch.P[pCutoff]) * math.Pow(2, 4*ch.P[pBright]*e.openFor(b))
		if u < fc {
			fc = u
		}
		q += resoDB(ch.P[pReso])
	}
	if fc >= 18000 && q < 0.5 {
		b.fltActive = false
		return
	}
	if !b.fltActive {
		b.flt.reset()
		b.fltActive = true
	}
	b.flt.set(fc, q)
	b.flt.process(buf)
}

// Calcule len(out)/2 images audio (stéréo 16 bits entrelacées).
// softLimit : limiteur transparent. Jusqu'à 0,7 le son passe tel quel (aucune déformation, contrairement à un tanh qui
// colore dès les niveaux moyens) ; au-delà, la courbe s'arrondit doucement et ne dépasse jamais 1.
func softLimit(x float64) float64 {
	const knee = 0.7
	a := x
	if a < 0 {
		a = -a
	}
	if a <= knee {
		return x
	}
	u := (a - knee) / (1 - knee)
	y := knee + (1-knee)*u/(1+u)
	if x < 0 {
		return -y
	}
	return y
}

// limitBlock applique le volume général puis un limiteur « à anticipation » : quand un pic va dépasser 0,9, le volume
// baisse en douceur AVANT le pic (sur ~40 échantillons), puis remonte lentement. Le son n'est donc plus écrasé d'un coup
// (ce qui donne le son « qui arrache ») ; le softLimit final ne sert plus que de filet de sécurité.
func (e *Engine) limitBlock(B int, g float64) {
	const thr, W = 0.9, 48
	mixL, mixR := e.mixL[:B], e.mixR[:B]
	pk := 0.0
	for i := 0; i < B; i++ {
		a := math.Max(math.Abs(mixL[i]), math.Abs(mixR[i])) * g
		if a > pk {
			pk = a
		}
	}
	gcur := 1 - e.limDrop
	if pk <= thr && gcur > 0.9999 {
		for i := 0; i < B; i++ {
			mixL[i] *= g
			mixR[i] *= g
		}
		return
	}
	if len(e.limReq) < B {
		e.limReq = make([]float64, B)
	}
	req := e.limReq[:B]
	for i := 0; i < B; i++ {
		a := math.Max(math.Abs(mixL[i]), math.Abs(mixR[i])) * g
		req[i] = 1
		if a > thr {
			req[i] = thr / a
		}
	}
	rel := 1 - math.Exp(-1/(0.12*SR))
	for i := 0; i < B; i++ {
		t := 1.0
		end := i + W
		if end > B {
			end = B
		}
		for j := i; j < end; j++ {
			if req[j] < t {
				t = req[j]
			}
		}
		if t < gcur {
			gcur += (t - gcur) * 0.12
		} else {
			gcur += (t - gcur) * rel
		}
		mixL[i] *= g * gcur
		mixR[i] *= g * gcur
	}
	e.limDrop = 1 - gcur
}

func (e *Engine) Render(out []int16) {
	t0 := time.Now()
	B := len(out) / 2
	defer e.measure(t0, B)
	e.mixBlock(B)
	mixL, mixR := e.mixL[:B], e.mixR[:B]
	e.limitBlock(B, e.master*e.cc7*e.cc11*1.414)
	e.diagOut(mixL, mixR, B)
	for i := 0; i < B; i++ {
		out[2*i] = int16(softLimit(mixL[i]) * 32000)
		out[2*i+1] = int16(softLimit(mixR[i]) * 32000)
	}
}

// RenderF : même son que Render, en nombres à virgule (utilisé par le plugin VST3)
func (e *Engine) RenderF(l, r []float32) {
	t0 := time.Now()
	B := len(l)
	if len(r) < B {
		B = len(r)
	}
	defer e.measure(t0, B)
	e.mixBlock(B)
	mixL, mixR := e.mixL[:B], e.mixR[:B]
	e.limitBlock(B, e.master*e.cc7*e.cc11*1.414)
	e.diagOut(mixL, mixR, B)
	for i := 0; i < B; i++ {
		l[i] = float32(softLimit(mixL[i]) * (32000.0 / 32768))
		r[i] = float32(softLimit(mixR[i]) * (32000.0 / 32768))
	}
}

// mixBlock calcule B images de son dans mixL / mixR (avant le réglage de volume final)
func (e *Engine) mixBlock(B int) {
	if len(e.tmp) < B {
		e.tmp = make([]float64, B)
		e.mixL = make([]float64, B)
		e.mixR = make([]float64, B)
		e.send = make([]float64, B)
	}
	tmp, mixL, mixR, send := e.tmp[:B], e.mixL[:B], e.mixR[:B], e.send[:B]
	e.atS += (e.at - e.atS) * 0.4 // la pression est lissée pour éviter les à-coups
	if e.bendS == 0 {
		e.bendS = e.bend
	}
	e.bendS += (e.bend - e.bendS) * 0.5 // pitch et modulation : glissement doux d'une valeur MIDI à la suivante
	e.modS += (e.mod - e.modS) * 0.5
	for i := range mixL {
		mixL[i], mixR[i], send[i] = 0, 0, 0
	}
	anySend := false
	solo := e.soloAny()
	var g0, g1, peak [nCh]float64
	// plus il y a de pistes qui sonnent, plus on baisse chacune un peu : 8 pistes superposées ne doivent pas saturer
	e.mu.Lock()
	nAud := 0
	for _, ch := range e.ch {
		if ch.src != nil && !(ch.Mute || (solo && !ch.Solo)) && ch.Vol > -59.9 {
			nAud++
		}
	}
	e.mu.Unlock()
	comp := 1.0
	if nAud > 2 {
		comp = 1 / math.Sqrt(float64(nAud)/2)
	}
	for i, ch := range e.ch {
		t := 0.0
		if !(ch.Mute || (solo && !ch.Solo)) && ch.Vol > -59.9 {
			t = math.Pow(10, ch.Vol/20) * comp
		}
		g0[i], g1[i] = ch.curGain, t
		ch.curGain = t
	}
	dt := float64(B) / SR
	// pistes avec effets : leur son passe d'abord par un tampon à elles
	var fxc [nCh]*FXCfg
	for ci := range e.ch {
		if c := e.fxOf(ci); c != nil {
			fxc[ci] = c
			if len(e.chL[ci]) < B {
				e.chL[ci], e.chR[ci] = make([]float64, B), make([]float64, B)
			}
			l, r := e.chL[ci][:B], e.chR[ci][:B]
			for i := range l {
				l[i], r[i] = 0, 0
			}
		}
	}
	e.mu.Lock()
	alive := e.voices[:0]
	for _, v := range e.voices {
		b := v.base()
		if b.dead {
			continue
		}
		for i := range tmp {
			tmp[i] = 0
		}
		if !v.render(e, tmp) {
			b.dead = true
			continue
		}
		alive = append(alive, v)
		if b.extraAtt > 0 {
			if age0 := b.age - dt; age0 < b.extraAtt {
				for i := range tmp {
					t := age0 + float64(i)/SR
					if t >= b.extraAtt {
						break
					}
					tmp[i] *= t / b.extraAtt
				}
			}
		}
		e.applyVoiceFilter(b, tmp)
		ci := b.ch.idx
		a0, a1 := g0[ci], g1[ci]
		if a0 == 0 && a1 == 0 {
			continue
		}
		p := b.ch.Pan + b.pan
		if p < -1 {
			p = -1
		} else if p > 1 {
			p = 1
		}
		ang := (p + 1) * math.Pi / 4
		gl, gr := math.Cos(ang), math.Sin(ang)
		da := (a1 - a0) / float64(B)
		pk := peak[ci]
		sendAmt := 0.0
		if b.ch.ModFX {
			sendAmt = e.openFor(b)
			anySend = anySend || sendAmt > 0.003
		}
		dL, dR := mixL, mixR
		if fxc[ci] != nil {
			dL, dR = e.chL[ci][:B], e.chR[ci][:B]
		}
		for i, s := range tmp {
			y := s * (a0 + da*float64(i))
			dL[i] += y * gl
			dR[i] += y * gr
			send[i] += y * sendAmt
			if y > pk {
				pk = y
			} else if -y > pk {
				pk = -y
			}
		}
		peak[ci] = pk
	}
	e.voices = alive
	atomic.StoreInt32(&e.liveCount, int32(len(alive)))
	e.mu.Unlock()
	lesFast := e.LeslieFast()
	for ci, c := range fxc {
		if c == nil {
			if e.fx[ci] != nil {
				e.fx[ci].clear()
			}
			continue
		}
		if p := atomic.SwapPointer(&e.fxNew[ci], nil); p != nil {
			e.fx[ci] = (*fxChain)(p)
		}
		if e.fx[ci] == nil {
			e.fx[ci] = newFXChain()
		}
		l, r := e.chL[ci][:B], e.chR[ci][:B]
		e.fx[ci].process(l, r, c, lesFast)
		for i := range l {
			mixL[i] += l[i]
			mixR[i] += r[i]
		}
	}
	for i, ch := range e.ch {
		ch.peak = math.Max(ch.peak*0.88, peak[i])
	}
	if e.revAmt > 0.002 || anySend {
		e.revHold = int(4 * SR) // la queue de réverbération continue 4 secondes après la dernière note
	}
	if e.revHold > 0 {
		e.revHold -= B
		var sd []float64
		if anySend {
			sd = send
		}
		e.rev.process(mixL, mixR, sd, e.revAmt, 0.9)
	}
	e.Player.mix(mixL, mixR)
	e.Pads.mix(mixL, mixR)
}

// SetSampleRate change la fréquence de travail (44100, 48000, 96000…). Toutes les notes sont coupées.
// À appeler pendant que la sortie audio est arrêtée.
func (e *Engine) SetSampleRate(r int) {
	if r < 8000 || r > 192000 {
		r = 44100
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	e.voices = nil
	SR = float64(r)
	e.cacheMu.Lock()
	e.cache = map[[2]int][]float32{}
	e.cacheMu.Unlock()
	e.rev = newReverb()
	e.fx = [nCh]*fxChain{}
	for i := range e.fxNew {
		atomic.StorePointer(&e.fxNew[i], nil)
	}
	e.chL, e.chR = [nCh][]float64{}, [nCh][]float64{}
}

// Rate renvoie la fréquence de travail actuelle.
func (e *Engine) Rate() int { return int(SR) }

// ---------------------------------------------------------------- charge du processeur

// mesure le temps de calcul d'un bloc audio et protège contre la surcharge :
// si le calcul prend trop de temps, on limite le nombre de notes jouées en même temps.
func (e *Engine) measure(t0 time.Time, frames int) {
	budget := float64(frames) / SR
	r := time.Since(t0).Seconds() / budget
	if r > 0.7 { // diagnostic : bloc lent, relevé sans rien écrire depuis le thread audio
		atomic.AddInt64(&e.slowN, 1)
		if m := uint64(r * 1000); m > atomic.LoadUint64(&e.slowMax) {
			atomic.StoreUint64(&e.slowMax, m)
		}
	}
	old := math.Float64frombits(atomic.LoadUint64(&e.cpuBits))
	avg := old*0.97 + r*0.03
	atomic.StoreUint64(&e.cpuBits, math.Float64bits(avg))
	pk := math.Float64frombits(atomic.LoadUint64(&e.cpuPeak))
	if r > pk {
		pk = r
	} else {
		pk = pk*0.995 + r*0.005
	}
	atomic.StoreUint64(&e.cpuPeak, math.Float64bits(pk))
	// La protection ne réagit qu'à une vraie surcharge qui dure (pas à un simple à-coup du système) :
	// elle baisse la limite de notes de 1/8 seulement, au plus une fois tous les quarts de seconde, sans jamais descendre sous 48.
	lim := atomic.LoadInt32(&e.voiceLimit)
	if r > 1.0 {
		e.xrun++
	} else if e.xrun > 0 && int(avg*1000)%7 == 0 {
		e.xrun--
	}
	if e.cool > 0 {
		e.cool--
	}
	switch {
	case (avg > 0.8 || e.xrun >= 3) && e.cool == 0:
		if lim > 48 {
			lim = lim - lim/8
			if lim < 48 {
				lim = 48
			}
			atomic.StoreInt32(&e.voiceLimit, lim)
		}
		e.xrun = 0
		e.calm = 0
		e.cool = int(0.25 * SR / float64(frames))
	case avg < 0.45:
		e.calm++
		if e.calm > int(SR/float64(frames)) && lim < maxVoice { // 1 seconde de calme : on remonte
			atomic.StoreInt32(&e.voiceLimit, int32(mini(int(lim)+16, maxVoice)))
			e.calm = 0
		}
	default:
		e.calm = 0
	}
}

// Load renvoie la charge moyenne et la pointe récente du calcul audio (1 = 100 % du temps disponible).
func (e *Engine) Load() (avg, peak float64) {
	return math.Float64frombits(atomic.LoadUint64(&e.cpuBits)), math.Float64frombits(atomic.LoadUint64(&e.cpuPeak))
}

// Voices renvoie le nombre de notes en cours et la limite actuelle.
func (e *Engine) Voices() (live, limit int) {
	return int(atomic.LoadInt32(&e.liveCount)), int(atomic.LoadInt32(&e.voiceLimit))
}

// diagOut : compte ce que la carte son demande, relève le niveau de sortie et ajoute le bip de test s'il est demandé
func (e *Engine) diagOut(mixL, mixR []float64, B int) {
	atomic.AddInt64(&e.pulled, int64(B))
	pk := 0.0
	for i := 0; i < B; i++ {
		if a := math.Abs(mixL[i]); a > pk {
			pk = a
		}
	}
	if old := math.Float64frombits(atomic.LoadUint64(&e.outPeakBits)); pk > old {
		atomic.StoreUint64(&e.outPeakBits, math.Float64bits(pk))
	}
	if left := atomic.LoadInt64(&e.toneLeft); left > 0 {
		w := 2 * math.Pi * 440 / SR
		for i := 0; i < B; i++ {
			s := 0.3 * math.Sin(e.tonePhase)
			e.tonePhase += w
			mixL[i] += s
			mixR[i] += s
		}
		if e.tonePhase > 2*math.Pi {
			e.tonePhase -= 2 * math.Pi
		}
		atomic.AddInt64(&e.toneLeft, -int64(B))
	}
}

// StartTone : bip de test de 2 secondes
func (e *Engine) StartTone() { atomic.StoreInt64(&e.toneLeft, int64(2*SR)) }

// DiagRead : (images demandées par la carte son, plus fort niveau depuis la dernière lecture)
func (e *Engine) DiagRead() (int64, float64) {
	p := math.Float64frombits(atomic.SwapUint64(&e.outPeakBits, 0))
	return atomic.LoadInt64(&e.pulled), p
}
