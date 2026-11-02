package main

// Voix communes et sons intégrés (synthétisés).

import (
	"math"
	"math/rand"
	"sync"
)

const (
	twoPi    = 2 * math.Pi
	maxVoice = 320 // 8 pistes stéréo x accord de 8 notes = 128 voix ; on garde de la marge pour la pédale
)

// fréquence d'échantillonnage du moteur (change seulement quand le son est arrêté)
var SR = 44100.0

const (
	iGrand = iota
	iUpright
	iPad
	iLead
	iSquare
	iGuitar
	iBass
	nInst
)

var instLabels = [nInst]string{
	"Grand Piano",
	"Upright Piano",
	"Synth Pad",
	"Synth Lead",
	"Square Lead",
	"Guitare",
	"Guitare Basse",
}

var instCats = [nInst]string{"Piano", "Piano", "Pad", "Lead", "Lead", "Guitare", "Basse"}

// Volume relatif de chaque son intégré (équilibrage)
var instGain = [nInst]float64{0.089, 0.166, 0.178, 0.171, 0.105, 1.0, 0.82}

func midiFreq(n int) float64 { return 440 * math.Pow(2, float64(n-69)/12) }

func fold(n, lo, hi int) int {
	for n < lo {
		n += 12
	}
	for n > hi {
		n -= 12
	}
	return n
}

// ---------------------------------------------------------------- filtre passe-bas

type biquad struct {
	b0, b1, b2, a1, a2 float64
	x1, x2, y1, y2     float64
	init               bool
	lastFc, lastQ      float64
}

// passe-bas 12 dB/octave ; qdB = résonance au-dessus du plat
func (f *biquad) set(fc, qdB float64) {
	if fc == f.lastFc && qdB == f.lastQ {
		return // mêmes réglages qu'au bloc précédent : les coefficients sont déjà bons
	}
	f.lastFc, f.lastQ = fc, qdB
	if fc < 20 {
		fc = 20
	}
	if fc > 0.45*SR {
		fc = 0.45 * SR
	}
	q := 0.7071 * math.Pow(10, qdB/20)
	w0 := twoPi * fc / SR
	cs, sn := math.Cos(w0), math.Sin(w0)
	alpha := sn / (2 * q)
	a0 := 1 + alpha
	f.b0 = (1 - cs) / 2 / a0
	f.b1 = (1 - cs) / a0
	f.b2 = f.b0
	f.a1 = -2 * cs / a0
	f.a2 = (1 - alpha) / a0
}

func (f *biquad) process(buf []float64) {
	b0, b1, b2, a1, a2 := f.b0, f.b1, f.b2, f.a1, f.a2
	x1, x2, y1, y2 := f.x1, f.x2, f.y1, f.y2
	for i, x := range buf {
		y := b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2
		x2, x1 = x1, x
		y2, y1 = y1, y
		buf[i] = y
	}
	f.x1, f.x2, f.y1, f.y2 = x1, x2, y1, y2
}

func (f *biquad) reset() { f.x1, f.x2, f.y1, f.y2 = 0, 0, 0, 0 }

// ---------------------------------------------------------------- base commune des voix

type voiceI interface {
	base() *vbase
	// écrit exactement len(dst) échantillons mono dans dst (déjà mis à zéro) ; false = voix terminée
	render(e *Engine, dst []float64) bool
}

type vbase struct {
	tag                                                   int
	ch                                                    *Channel
	keyDown, pedalHold, sostHold, released, dead, evicted bool
	age, trel, rel                                        float64
	pan                                                   float64 // -1..1 (panoramique propre à l'échantillon)
	excl                                                  int     // classe exclusive SF2
	extraAtt                                              float64 // attaque supplémentaire (s)
	sfFc, sfQ                                             float64 // filtre propre au son (Hz, dB) ; sfFc<=0 : aucun
	flt                                                   biquad
	fltActive                                             bool
	press                                                 float64 // pression sur la touche de cette voix (aftertouch par touche)
}

func (b *vbase) base() *vbase { return b }

func (b *vbase) release() {
	if !b.released {
		b.released = true
		b.trel = b.age
	}
}

func (b *vbase) finishedRelease() bool { return b.released && b.age-b.trel >= b.rel }

func (b *vbase) applyEnv(buf []float64, att float64) {
	for i := range buf {
		t := b.age + float64(i)/SR
		g := 1.0
		if att > 0 && t < att {
			g = t / att
		}
		if b.released {
			g *= math.Exp(-6.9 * math.Max(t-b.trel, 0) / b.rel)
		}
		buf[i] *= g
	}
}

// ---------------------------------------------------------------- voix intégrées

const (
	kPiano = iota
	kSynth
	kPluck
)

type synVoice struct {
	vbase
	kind int

	// piano (synthèse additive, oscillateurs rotatifs)
	f0                 float64
	mult, a, tau       []float64
	s, c               []float64
	maxAge             float64
	noiseAmp, noiseTau float64

	// synthés (table d'onde)
	table   []float64
	det     []float64
	ph      []float64
	amp     float64
	att     float64
	vib     float64
	lfo     float64
	lfoRate float64

	// cordes pincées (échantillon pré-calculé)
	buf  []float32
	pos  float64
	gain float64
}

type pianoParams struct {
	kmax                     int
	maxf, binc, hammer, hexp float64
	tau0, tslope, tdamp      float64
	cents                    []float64
	noise, ntau, rel         float64
}

var pianoP = [2]pianoParams{
	{26, 9500, 1.5e-4, 0.125, 0.85, 7.0, 17.0, 0.8, []float64{0}, 0.012, 0.006, 0.28},
	{18, 8000, 2.2e-4, 0.16, 1.05, 4.2, 15.0, 1.2, []float64{-1.0, 1.2}, 0.03, 0.012, 0.18},
}

func newPiano(inst, n int, v float64) *synVoice {
	p := pianoP[inst]
	f0 := midiFreq(n)
	K := int(math.Max(3, math.Min(float64(p.kmax), p.maxf/f0)))
	b := p.binc * math.Pow(2, float64(n-60)/18)
	base := make([]float64, K)
	for i := 0; i < K; i++ {
		k := float64(i + 1)
		base[i] = (1 / math.Pow(k, p.hexp)) * (math.Abs(math.Sin(math.Pi*k*p.hammer)) + 0.10)
	}
	if inst == iUpright {
		for i := 2; i < 5 && i < K; i++ {
			base[i] *= 1.35
		}
	}
	amp := instGain[inst] * (0.15 + 0.85*v)
	tau1 := p.tau0 * math.Pow(2, -float64(n-36)/p.tslope)
	U := len(p.cents)
	vo := &synVoice{kind: kPiano, f0: f0}
	vo.rel = p.rel
	vo.maxAge = 18 * tau1
	vo.noiseAmp = p.noise * amp
	vo.noiseTau = p.ntau
	for u := 0; u < U; u++ {
		ph0 := rand.Float64()
		det := math.Pow(2, p.cents[u]/1200)
		for i := 0; i < K; i++ {
			k := float64(i + 1)
			m := k * math.Sqrt(1+b*k*k) * det
			vo.mult = append(vo.mult, m)
			vo.a = append(vo.a, amp*base[i]/base[0]*math.Pow(v, 0.10*(k-1))/float64(U))
			vo.tau = append(vo.tau, tau1/(1+p.tdamp*math.Pow(k-1, 0.9)))
			phi := twoPi * m * ph0
			vo.s = append(vo.s, math.Sin(phi))
			vo.c = append(vo.c, math.Cos(phi))
		}
	}
	return vo
}

func pianoDecay(x, tau float64) float64 {
	return 0.7*math.Exp(-x/tau) + 0.3*math.Exp(-x/(3.5*tau))
}

func (v *synVoice) renderPiano(e *Engine, dst []float64) bool {
	if v.finishedRelease() || v.age > v.maxAge {
		return false
	}
	B := len(dst)
	dt := float64(B) / SR
	for k := range v.mult {
		a0 := v.a[k] * pianoDecay(v.age, v.tau[k])
		a1 := v.a[k] * pianoDecay(v.age+dt, v.tau[k])
		if a0 < 2e-5 && a1 < 2e-5 {
			continue
		}
		th := twoPi * v.f0 * v.mult[k] * e.bendS / SR
		cs, sn := math.Cos(th), math.Sin(th)
		da := (a1 - a0) / float64(B)
		s, c, a := v.s[k], v.c[k], a0
		for i := 0; i < B; i++ {
			dst[i] += a * s
			a += da
			s, c = s*cs+c*sn, c*cs-s*sn
		}
		m := math.Sqrt(s*s + c*c)
		v.s[k], v.c[k] = s/m, c/m
	}
	if v.age < 0.12 && v.noiseAmp > 0 {
		prev := 0.0
		for i := 0; i < B; i++ {
			t := v.age + float64(i)/SR
			if t > 0.12 {
				break
			}
			x := rand.NormFloat64()
			prev = 0.7*prev + 0.3*x
			dst[i] += prev * math.Exp(-t/v.noiseTau) * v.noiseAmp * 2
		}
	}
	v.applyEnv(dst, 0.002)
	v.age += dt
	return true
}

type synthParams struct {
	kmax          int
	maxf          float64
	cents         []float64
	att, rel, vib float64
}

var synthP = [3]synthParams{
	{16, 7000, []float64{-10, 0, 10}, 0.35, 0.9, 0.0015},
	{40, 14000, []float64{-6, 6}, 0.008, 0.14, 0.004},
	{40, 14000, []float64{-4, 4}, 0.01, 0.12, 0.0025},
}

const tableSize = 4096

var (
	tables   = map[[2]int][]float64{}
	tablesMu sync.Mutex
)

func getTable(inst, K int) []float64 {
	tablesMu.Lock()
	defer tablesMu.Unlock()
	key := [2]int{inst, K}
	if t, ok := tables[key]; ok {
		return t
	}
	amps := make([]float64, K)
	for i := 0; i < K; i++ {
		k := float64(i + 1)
		switch inst {
		case iPad:
			amps[i] = (1 / k) / (1 + math.Pow(k/5, 2))
		case iLead:
			amps[i] = (1 / k) / (1 + math.Pow(k/20, 2))
		default:
			if i%2 == 0 { // harmoniques impaires
				amps[i] = (1 / k) / (1 + math.Pow(k/30, 2))
			}
		}
	}
	t := make([]float64, tableSize+1)
	peak := 0.0
	for j := 0; j < tableSize; j++ {
		x := float64(j) / tableSize
		s := 0.0
		for i := 0; i < K; i++ {
			s += amps[i] * math.Sin(twoPi*float64(i+1)*x)
		}
		t[j] = s
		if math.Abs(s) > peak {
			peak = math.Abs(s)
		}
	}
	for j := range t {
		t[j] /= peak
	}
	t[tableSize] = t[0]
	tables[key] = t
	return t
}

func newSynth(inst, n int, v float64) *synVoice {
	c := synthP[inst-iPad]
	f0 := midiFreq(n)
	K := int(math.Min(float64(c.kmax), math.Max(1, c.maxf/f0)))
	vo := &synVoice{kind: kSynth, f0: f0, att: c.att, vib: c.vib, lfoRate: 5.3}
	vo.rel = c.rel
	vo.table = getTable(inst, K)
	for _, ct := range c.cents {
		vo.det = append(vo.det, math.Pow(2, ct/1200))
		vo.ph = append(vo.ph, rand.Float64())
	}
	vo.amp = instGain[inst] * (0.15 + 0.85*v) / math.Sqrt(float64(len(c.cents)))
	return vo
}

func (v *synVoice) renderSynth(e *Engine, dst []float64) bool {
	if v.finishedRelease() {
		return false
	}
	depth := v.vib + e.vibFor(&v.vbase)*0.012
	step := twoPi * v.lfoRate / SR
	for i := range dst {
		lfo := math.Sin(v.lfo)
		v.lfo += step
		inc := v.f0 / SR * e.bendS * (1 + depth*lfo)
		s := 0.0
		for u := range v.det {
			x := v.ph[u] * tableSize
			j := int(x)
			fr := x - float64(j)
			s += v.table[j]*(1-fr) + v.table[j+1]*fr
			p := v.ph[u] + v.det[u]*inc
			if p >= 1 {
				p -= 1
			}
			v.ph[u] = p
		}
		dst[i] = s * v.amp
	}
	if v.lfo > 1e6 {
		v.lfo = math.Mod(v.lfo, twoPi)
	}
	v.applyEnv(dst, v.att)
	v.age += float64(len(dst)) / SR
	return true
}

// ---------------------------------------------------------------- cordes pincées (guitare, basse)

func renderPluck(inst, n int) []float32 {
	f := midiFreq(n)
	var dur, tau, target float64
	var smooth int
	if inst == iGuitar {
		dur, tau, smooth, target = 3.5, 2.0, 3, 0.10
	} else {
		dur, tau, smooth, target = 3.8, 1.7, 9, 0.12
	}
	N := int(math.Round(SR/f - 0.5))
	if N < 4 {
		N = 4
	}
	P := float64(N) + 0.5
	rng := rand.New(rand.NewSource(int64(1000 + n)))
	raw := make([]float64, N)
	for i := range raw {
		raw[i] = rng.Float64()*2 - 1
	}
	x := make([]float64, N)
	half := smooth / 2
	for i := 0; i < N; i++ {
		s := 0.0
		for d := -half; d <= half; d++ {
			s += raw[((i+d)%N+N)%N]
		}
		x[i] = s / float64(smooth)
	}
	mean, peak := 0.0, 0.0
	for _, y := range x {
		mean += y
	}
	mean /= float64(N)
	for i := range x {
		x[i] -= mean
		if math.Abs(x[i]) > peak {
			peak = math.Abs(x[i])
		}
	}
	for i := range x {
		x[i] /= peak
	}
	g := math.Exp(-(P / SR) / tau)
	total := int(dur * SR)
	periods := total/N + 3
	out := make([]float64, periods*N)
	cur := x
	next := make([]float64, N)
	for p := 0; p < periods; p++ {
		copy(out[p*N:], cur)
		for j := 0; j < N; j++ {
			prev := cur[(j-1+N)%N]
			next[j] = g * 0.5 * (cur[j] + prev)
		}
		cur, next = next, cur
	}
	r := P * f / SR
	m := int(float64(total) / r)
	y := make([]float64, m)
	for j := 0; j < m; j++ {
		pos := float64(j) * r
		i0 := int(pos)
		fr := pos - float64(i0)
		if i0+1 >= len(out) {
			break
		}
		y[j] = out[i0]*(1-fr) + out[i0+1]*fr
	}
	fade := int(0.3 * SR)
	if fade > m {
		fade = m
	}
	for i := 0; i < fade; i++ {
		y[m-fade+i] *= 1 - float64(i)/float64(fade)
	}
	for i := 0; i < 40 && i < m; i++ {
		y[i] *= float64(i) / 40
	}
	lim := int(SR / 2)
	if lim > m {
		lim = m
	}
	ss := 0.0
	for i := 0; i < lim; i++ {
		ss += y[i] * y[i]
	}
	rms := math.Sqrt(ss/float64(lim)) + 1e-9
	res := make([]float32, m)
	for i := range y {
		res[i] = float32(y[i] * target / rms)
	}
	return res
}

func (v *synVoice) renderPluck(e *Engine, dst []float64) bool {
	L := len(v.buf)
	if v.finishedRelease() || v.pos >= float64(L-2) {
		return false
	}
	lim := float64(L) - 2.0001
	for i := range dst {
		idx := v.pos + e.bendS*float64(i)
		if idx > lim {
			idx = lim
		}
		i0 := int(idx)
		fr := idx - float64(i0)
		dst[i] = (float64(v.buf[i0])*(1-fr) + float64(v.buf[i0+1])*fr) * v.gain
	}
	v.applyEnv(dst, 0)
	v.pos += e.bendS * float64(len(dst))
	v.age += float64(len(dst)) / SR
	return true
}

func (v *synVoice) render(e *Engine, dst []float64) bool {
	switch v.kind {
	case kPiano:
		return v.renderPiano(e, dst)
	case kSynth:
		return v.renderSynth(e, dst)
	default:
		return v.renderPluck(e, dst)
	}
}
