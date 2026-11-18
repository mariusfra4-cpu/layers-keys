package africanloops

import (
	"math"
	"sort"
	"sync"
)

// NumTracks : pistes du mixer.
const NumTracks = 8

// Track est une piste du mixer.
type Track struct {
	Name  string
	Audio *Audio
	Gain  float64 // 0..2 (1 = 0 dB)
	Pan   float64 // -1 gauche .. +1 droite
	Mute  bool
	Solo  bool
	Peak  float64 // niveau mesuré (vumètre)
}

// Looper joue les pistes en boucle, toutes calées sur le même tempo.
//
// Changement de tempo sans changer la hauteur du son : la loop est découpée à
// chaque coup (comme une boîte à rythmes) et chaque morceau est rejoué à sa
// vitesse normale, simplement placé plus tôt ou plus tard. Pour les percussions,
// c'est la méthode qui garde les attaques les plus nettes.
type Looper struct {
	mu           sync.Mutex
	rate         int     // fréquence de sortie
	srcRate      int     // fréquence des loops
	srcFrames    int     // longueur de la loop d'origine
	origBPM      float64 // tempo d'origine de la loop
	globalBPM    float64
	preset       bool      // vrai = « Preset BPM » : on joue au tempo d'origine
	slices       []float64 // débuts des morceaux (échantillons de la loop d'origine)
	pos          float64   // position de lecture (échantillons de sortie)
	playing      bool
	Master       float64
	MasterPan    float64
	MasterMute   bool
	Tracks       [NumTracks]*Track
	PeakL, PeakR float64
}

// NewLooper crée un lecteur pour une sortie à la fréquence donnée.
func NewLooper(rate int) *Looper {
	l := &Looper{rate: rate, globalBPM: 120, Master: 1}
	for i := range l.Tracks {
		l.Tracks[i] = &Track{Gain: 1}
	}
	return l
}

// Load place une loop (déjà séparée en pistes) dans le lecteur.
func (l *Looper) Load(info LoopInfo, stems []Stem) {
	l.mu.Lock()
	defer l.mu.Unlock()
	for i := range l.Tracks {
		l.Tracks[i] = &Track{Gain: 1}
	}
	l.srcFrames, l.srcRate = 0, l.rate
	for i, s := range stems {
		if i >= NumTracks {
			break
		}
		l.Tracks[i] = &Track{Name: s.Kind, Audio: s.Audio, Gain: 1}
		l.srcRate = s.Audio.Rate
		if n := s.Audio.Frames(); n > l.srcFrames {
			l.srcFrames = n
		}
	}
	l.origBPM = info.Tempo.BPM
	l.slices = l.slices[:0]
	l.slices = append(l.slices, 0)
	for _, t := range info.Slices {
		s := t * float64(l.srcRate)
		if s > l.slices[len(l.slices)-1]+0.01*float64(l.srcRate) && s < float64(l.srcFrames) {
			l.slices = append(l.slices, s)
		}
	}
	// sans coups (nappes, voix) : découpage régulier en doubles-croches
	if len(l.slices) < 2 && l.origBPM > 0 {
		step := 60 / l.origBPM / 4 * float64(l.srcRate)
		for s := step; s < float64(l.srcFrames); s += step {
			l.slices = append(l.slices, s)
		}
	}
	l.pos = 0
}

// SetGlobalBPM règle le tempo commun (ou celui du logiciel hôte).
func (l *Looper) SetGlobalBPM(bpm float64) {
	if bpm < 20 || bpm > 400 {
		return
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	old := l.loopLen()
	l.globalBPM = bpm
	if old > 0 { // on garde la même position musicale
		l.pos = l.pos / old * l.loopLen()
	}
}

// SetPresetBPM : vrai = jouer la loop à son tempo d'origine.
func (l *Looper) SetPresetBPM(on bool) {
	l.mu.Lock()
	defer l.mu.Unlock()
	old := l.loopLen()
	l.preset = on
	if old > 0 {
		l.pos = l.pos / old * l.loopLen()
	}
}

// GlobalBPM renvoie le tempo commun.
func (l *Looper) GlobalBPM() float64 { l.mu.Lock(); defer l.mu.Unlock(); return l.globalBPM }

// PlayingBPM renvoie le tempo réellement joué.
func (l *Looper) PlayingBPM() float64 { l.mu.Lock(); defer l.mu.Unlock(); return l.playBPM() }

func (l *Looper) playBPM() float64 {
	if l.preset || l.origBPM <= 0 {
		return l.origBPM
	}
	return l.globalBPM
}

// speed : >1 = plus rapide que l'original.
func (l *Looper) speed() float64 {
	if b := l.playBPM(); b > 0 && l.origBPM > 0 {
		return b / l.origBPM
	}
	return 1
}

// loopLen : longueur de la boucle en échantillons de sortie.
func (l *Looper) loopLen() float64 {
	if l.srcFrames == 0 {
		return 0
	}
	return float64(l.srcFrames) / float64(l.srcRate) * float64(l.rate) / l.speed()
}

// Play / Stop.
func (l *Looper) Play() { l.mu.Lock(); l.playing = true; l.mu.Unlock() }
func (l *Looper) Stop() { l.mu.Lock(); l.playing = false; l.pos = 0; l.mu.Unlock() }

// Playing indique si la lecture est en cours.
func (l *Looper) Playing() bool { l.mu.Lock(); defer l.mu.Unlock(); return l.playing }

// Position renvoie la position dans la boucle (0..1).
func (l *Looper) Position() float64 {
	l.mu.Lock()
	defer l.mu.Unlock()
	if n := l.loopLen(); n > 0 {
		return l.pos / n
	}
	return 0
}

// sourcePos traduit une position de sortie en position dans la loop d'origine.
// ok=false : on est dans le silence entre la fin d'un morceau et le coup suivant
// (quand on ralentit). gain sert aux petits fondus qui évitent les clics.
func (l *Looper) sourcePos(outPos float64) (src float64, gain float64, ok bool) {
	sp := l.speed()
	srcGrid := outPos / float64(l.rate) * float64(l.srcRate) * sp // position « musicale »
	i := sort.SearchFloat64s(l.slices, srcGrid+1e-9) - 1
	if i < 0 {
		i = 0
	}
	start := l.slices[i]
	end := float64(l.srcFrames)
	if i+1 < len(l.slices) {
		end = l.slices[i+1]
	}
	// temps écoulé depuis le début du morceau, à vitesse normale
	elapsed := (srcGrid - start) / sp
	src = start + elapsed
	if src >= end {
		return 0, 0, false
	}
	gain = 1
	if math.Abs(sp-1) < 1e-9 {
		return src, gain, true // tempo d'origine : lecture exacte, sans aucun traitement
	}
	fade := 0.004 * float64(l.srcRate) // 4 ms
	// on ralentit : le morceau s'éteint en douceur avant le silence qui le suit
	if left := end - src; sp < 1 && left < fade {
		gain = left / fade
	}
	if sp > 1 { // on accélère : le coup suivant arrive plus tôt, le morceau est coupé
		nextOut := (end - start) / sp // durée du morceau à la nouvelle vitesse
		if elapsed > nextOut {
			return 0, 0, false
		}
		if r := nextOut - elapsed; r < fade {
			gain = math.Min(gain, r/fade)
		}
	}
	return src, gain, true
}

// Render remplit les tampons de sortie (stéréo). Appelé par le moteur audio.
func (l *Looper) Render(outL, outR []float32) {
	l.mu.Lock()
	defer l.mu.Unlock()
	for i := range outL {
		outL[i], outR[i] = 0, 0
	}
	n := l.loopLen()
	if !l.playing || n <= 0 {
		l.PeakL *= 0.9
		l.PeakR *= 0.9
		return
	}
	anySolo := false
	for _, t := range l.Tracks {
		if t.Audio != nil && t.Solo {
			anySolo = true
		}
	}
	type tg struct {
		t      *Track
		gl, gr float64
		peak   float64
	}
	var live []tg
	for _, t := range l.Tracks {
		if t.Audio == nil {
			continue
		}
		audible := !t.Mute && (!anySolo || t.Solo)
		g := 0.0
		if audible {
			g = t.Gain
		}
		a := (clampPan(t.Pan) + 1) * math.Pi / 4 // panoramique à puissance constante
		live = append(live, tg{t: t, gl: g * math.Cos(a) * math.Sqrt2, gr: g * math.Sin(a) * math.Sqrt2})
	}
	mg := l.Master
	if l.MasterMute {
		mg = 0
	}
	ma := (clampPan(l.MasterPan) + 1) * math.Pi / 4
	mgl, mgr := mg*math.Cos(ma)*math.Sqrt2, mg*math.Sin(ma)*math.Sqrt2
	pl, pr := l.PeakL*0.9, l.PeakR*0.9
	for i := range outL {
		src, g, ok := l.sourcePos(l.pos)
		if ok {
			j := int(src)
			f := float32(src - float64(j))
			for k := range live {
				a := live[k].t.Audio
				if j >= len(a.L) {
					continue
				}
				sl, sr := a.L[j], a.R[j]
				if j+1 < len(a.L) && f != 0 {
					sl += (a.L[j+1] - a.L[j]) * f
					sr += (a.R[j+1] - a.R[j]) * f
				}
				vl := float64(sl) * g * live[k].gl
				vr := float64(sr) * g * live[k].gr
				if p := math.Max(math.Abs(vl), math.Abs(vr)); p > live[k].peak {
					live[k].peak = p
				}
				outL[i] += float32(vl * mgl)
				outR[i] += float32(vr * mgr)
			}
		}
		pl = math.Max(pl, math.Abs(float64(outL[i])))
		pr = math.Max(pr, math.Abs(float64(outR[i])))
		l.pos++
		if l.pos >= n {
			l.pos -= n
		}
	}
	for _, x := range live {
		x.t.Peak = math.Max(x.t.Peak*0.9, x.peak)
	}
	l.PeakL, l.PeakR = pl, pr
}

func clampPan(p float64) float64 {
	if p < -1 {
		return -1
	}
	if p > 1 {
		return 1
	}
	return p
}
