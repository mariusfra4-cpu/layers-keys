package main

// Effets sur toute la sortie : compresseur, égaliseur 6 bandes, chorus, tremolo / auto-pan, delay.
// Ordre : compresseur → égaliseur → chorus → tremolo → delay → (réverbération du moteur) → limiteur.
// Tous les calculs se font en nombres à virgule, sans allocation pendant le jeu.

import (
	"math"
	"sync/atomic"
	"unsafe"
)

func unsafePtr(c *FXCfg) unsafe.Pointer { return unsafe.Pointer(c) }

// ---------------------------------------------------------------- réglages (enregistrés dans les presets et projets)

type EQBand struct{ F, G, Q float64 } // fréquence (Hz), gain (dB), largeur Q

type EQCfg struct {
	On             bool
	HP             float64 // coupe-bas (Hz) : 20 = aucun
	LS, P1, P2, HS EQBand  // basses, médium 1, médium 2, aigus
	LP             float64 // coupe-haut (Hz) : 20000 = aucun
}

type CompCfg struct {
	On                                         bool
	Thresh, Ratio, Attack, Release, MakeupGain float64 // dB, :1, ms, ms, dB
}

type ChorusCfg struct {
	On               bool
	Rate, Depth, Mix float64 // Hz, 0..1, 0..1
}

type DelayCfg struct {
	On                        bool
	Time, Feedback, Mix, Tone float64 // ms, 0..0,9, 0..1, Hz (filtre des répétitions)
}

type TremCfg struct {
	On          bool
	Rate, Depth float64 // Hz, 0..1
	Pan         bool    // vrai : auto-pan (le son passe de gauche à droite)
}

type FXCfg struct {
	EQ     EQCfg
	Comp   CompCfg
	Chorus ChorusCfg
	Delay  DelayCfg
	Trem   TremCfg
}

func defaultFX() FXCfg {
	return FXCfg{
		EQ: EQCfg{HP: 20, LP: 20000,
			LS: EQBand{F: 120, Q: 0.7}, P1: EQBand{F: 500, Q: 1}, P2: EQBand{F: 2500, Q: 1}, HS: EQBand{F: 8000, Q: 0.7}},
		Comp:   CompCfg{Thresh: -18, Ratio: 3, Attack: 10, Release: 160, MakeupGain: 3},
		Chorus: ChorusCfg{Rate: 0.8, Depth: 0.5, Mix: 0.4},
		Delay:  DelayCfg{Time: 350, Feedback: 0.35, Mix: 0.25, Tone: 5000},
		Trem:   TremCfg{Rate: 5, Depth: 0.5},
	}
}

func fin(v, def float64) float64 {
	if math.IsNaN(v) || math.IsInf(v, 0) {
		return def
	}
	return v
}

// sanitize remet chaque réglage dans sa plage (un fichier abîmé ne doit jamais faire planter le son)
func (c *FXCfg) sanitize() {
	d := defaultFX()
	b := func(x *EQBand, dx EQBand, flo, fhi float64) {
		x.F = clampf(fin(x.F, dx.F), flo, fhi)
		x.G = clampf(fin(x.G, 0), -15, 15)
		x.Q = clampf(fin(x.Q, dx.Q), 0.3, 8)
	}
	c.EQ.HP = clampf(fin(c.EQ.HP, 20), 20, 500)
	c.EQ.LP = clampf(fin(c.EQ.LP, 20000), 2000, 20000)
	b(&c.EQ.LS, d.EQ.LS, 30, 500)
	b(&c.EQ.P1, d.EQ.P1, 80, 4000)
	b(&c.EQ.P2, d.EQ.P2, 300, 12000)
	b(&c.EQ.HS, d.EQ.HS, 2000, 16000)
	c.Comp.Thresh = clampf(fin(c.Comp.Thresh, -18), -50, 0)
	c.Comp.Ratio = clampf(fin(c.Comp.Ratio, 3), 1, 20)
	c.Comp.Attack = clampf(fin(c.Comp.Attack, 10), 0.5, 100)
	c.Comp.Release = clampf(fin(c.Comp.Release, 160), 20, 1000)
	c.Comp.MakeupGain = clampf(fin(c.Comp.MakeupGain, 0), 0, 24)
	c.Chorus.Rate = clampf(fin(c.Chorus.Rate, 0.8), 0.1, 6)
	c.Chorus.Depth = clampf(fin(c.Chorus.Depth, 0.5), 0, 1)
	c.Chorus.Mix = clampf(fin(c.Chorus.Mix, 0.4), 0, 1)
	c.Delay.Time = clampf(fin(c.Delay.Time, 350), 20, 1500)
	c.Delay.Feedback = clampf(fin(c.Delay.Feedback, 0.35), 0, 0.9)
	c.Delay.Mix = clampf(fin(c.Delay.Mix, 0.25), 0, 1)
	c.Delay.Tone = clampf(fin(c.Delay.Tone, 5000), 500, 16000)
	c.Trem.Rate = clampf(fin(c.Trem.Rate, 5), 0.2, 14)
	c.Trem.Depth = clampf(fin(c.Trem.Depth, 0.5), 0, 1)
}

func (c *FXCfg) anyOn() bool {
	return c.EQ.On || c.Comp.On || c.Chorus.On || c.Delay.On || c.Trem.On
}

// ---------------------------------------------------------------- filtres biquad

type bqc struct{ b0, b1, b2, a1, a2 float64 }

func bqPeak(f, g, q float64) bqc {
	A := math.Pow(10, g/40)
	w := 2 * math.Pi * f / SR
	s, c := math.Sin(w), math.Cos(w)
	al := s / (2 * q)
	a0 := 1 + al/A
	return bqc{(1 + al*A) / a0, -2 * c / a0, (1 - al*A) / a0, -2 * c / a0, (1 - al/A) / a0}
}

func bqShelf(f, g, q float64, high bool) bqc {
	A := math.Pow(10, g/40)
	w := 2 * math.Pi * f / SR
	s, c := math.Sin(w), math.Cos(w)
	al := s / (2 * q)
	k := 2 * math.Sqrt(A) * al
	if !high {
		a0 := (A + 1) + (A-1)*c + k
		return bqc{A * ((A + 1) - (A-1)*c + k) / a0, 2 * A * ((A - 1) - (A+1)*c) / a0, A * ((A + 1) - (A-1)*c - k) / a0,
			-2 * ((A - 1) + (A+1)*c) / a0, ((A + 1) + (A-1)*c - k) / a0}
	}
	a0 := (A + 1) - (A-1)*c + k
	return bqc{A * ((A + 1) + (A-1)*c + k) / a0, -2 * A * ((A - 1) + (A+1)*c) / a0, A * ((A + 1) + (A-1)*c - k) / a0,
		2 * ((A - 1) - (A+1)*c) / a0, ((A + 1) - (A-1)*c - k) / a0}
}

func bqPass(f, q float64, high bool) bqc {
	w := 2 * math.Pi * f / SR
	s, c := math.Sin(w), math.Cos(w)
	al := s / (2 * q)
	a0 := 1 + al
	if high {
		return bqc{(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0}
	}
	return bqc{(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0}
}

// module de la réponse d'un biquad à la fréquence f (pour dessiner la courbe)
func (q bqc) mag(f float64) float64 {
	w := 2 * math.Pi * f / SR
	c1, s1, c2, s2 := math.Cos(w), math.Sin(w), math.Cos(2*w), math.Sin(2*w)
	nr := q.b0 + q.b1*c1 + q.b2*c2
	ni := -(q.b1*s1 + q.b2*s2)
	dr := 1 + q.a1*c1 + q.a2*c2
	di := -(q.a1*s1 + q.a2*s2)
	return math.Sqrt((nr*nr + ni*ni) / (dr*dr + di*di))
}

// les sections actives de l'égaliseur (dans l'ordre du traitement)
func (e EQCfg) sections() []bqc {
	var s []bqc
	if e.HP > 25 { // 24 dB par octave : deux sections
		s = append(s, bqPass(e.HP, 0.5412, true), bqPass(e.HP, 1.3066, true))
	}
	if math.Abs(e.LS.G) > 0.05 {
		s = append(s, bqShelf(e.LS.F, e.LS.G, e.LS.Q, false))
	}
	if math.Abs(e.P1.G) > 0.05 {
		s = append(s, bqPeak(e.P1.F, e.P1.G, e.P1.Q))
	}
	if math.Abs(e.P2.G) > 0.05 {
		s = append(s, bqPeak(e.P2.F, e.P2.G, e.P2.Q))
	}
	if math.Abs(e.HS.G) > 0.05 {
		s = append(s, bqShelf(e.HS.F, e.HS.G, e.HS.Q, true))
	}
	if e.LP < 19500 {
		s = append(s, bqPass(e.LP, 0.5412, false), bqPass(e.LP, 1.3066, false))
	}
	return s
}

// réponse totale de l'égaliseur en dB à la fréquence f
func (e EQCfg) responseDB(f float64) float64 {
	m := 1.0
	for _, s := range e.sections() {
		m *= s.mag(f)
	}
	return 20 * math.Log10(m+1e-12)
}

// ---------------------------------------------------------------- moteur d'effets

type fxState struct{ z1, z2 [2]float64 }

type fxChain struct {
	eqCfg  EQCfg
	eqSec  []bqc
	eqSt   [10]fxState
	env    float64 // compresseur
	cPhase float64
	cBuf   [2][]float64
	cIdx   int
	tPhase float64
	dBuf   [2][]float64
	dIdx   int
	dTime  float64
	dLP    [2]float64
	last   *FXCfg
}

func newFXChain() *fxChain {
	f := &fxChain{}
	n := int(0.06*SR) + 8
	f.cBuf[0], f.cBuf[1] = make([]float64, n), make([]float64, n)
	d := int(1.6*SR) + 8
	f.dBuf[0], f.dBuf[1] = make([]float64, d), make([]float64, d)
	f.dTime = 0.35 * SR
	return f
}

func (f *fxChain) process(l, r []float64, cfg *FXCfg) {
	B := len(l)
	if cfg.Comp.On {
		f.compress(l, r, &cfg.Comp)
	} else {
		f.env = 0
	}
	if cfg.EQ.On {
		if f.eqSec == nil || f.eqCfg != cfg.EQ {
			f.eqCfg = cfg.EQ
			f.eqSec = cfg.EQ.sections() // (une seule petite allocation, seulement quand un réglage change)
		}
		for si, q := range f.eqSec {
			st := &f.eqSt[si]
			for i := 0; i < B; i++ {
				x := l[i]
				y := q.b0*x + st.z1[0]
				st.z1[0] = q.b1*x - q.a1*y + st.z2[0]
				st.z2[0] = q.b2*x - q.a2*y
				l[i] = y
				x = r[i]
				y = q.b0*x + st.z1[1]
				st.z1[1] = q.b1*x - q.a1*y + st.z2[1]
				st.z2[1] = q.b2*x - q.a2*y
				r[i] = y
			}
		}
	}
	if cfg.Chorus.On {
		f.chorus(l, r, &cfg.Chorus)
	}
	if cfg.Trem.On {
		f.tremolo(l, r, &cfg.Trem)
	}
	if cfg.Delay.On {
		f.delay(l, r, &cfg.Delay)
	}
	f.last = cfg
}

func (f *fxChain) compress(l, r []float64, c *CompCfg) {
	aA := 1 - math.Exp(-1/(c.Attack*0.001*SR))
	aR := 1 - math.Exp(-1/(c.Release*0.001*SR))
	const knee = 6.0
	mk := c.MakeupGain
	for i := range l {
		lvl := math.Max(math.Abs(l[i]), math.Abs(r[i]))
		if lvl > f.env {
			f.env += (lvl - f.env) * aA
		} else {
			f.env += (lvl - f.env) * aR
		}
		db := 20 * math.Log10(f.env+1e-9)
		over := db - c.Thresh
		var gr float64
		switch {
		case over <= -knee/2:
			gr = 0
		case over < knee/2:
			gr = (1/c.Ratio - 1) * (over + knee/2) * (over + knee/2) / (2 * knee)
		default:
			gr = over/c.Ratio - over
		}
		g := math.Pow(10, (gr+mk)/20)
		l[i] *= g
		r[i] *= g
	}
}

func (f *fxChain) chorus(l, r []float64, c *ChorusCfg) {
	n := len(f.cBuf[0])
	inc := c.Rate / SR
	base := 0.012 * SR
	dep := (0.002 + 0.006*c.Depth) * SR
	for i := range l {
		f.cBuf[0][f.cIdx], f.cBuf[1][f.cIdx] = l[i], r[i]
		for ch, out := range [2]*float64{&l[i], &r[i]} {
			ph := f.cPhase + 0.25*float64(ch)
			d := base + dep*math.Sin(2*math.Pi*ph)
			p := float64(f.cIdx) - d
			for p < 0 {
				p += float64(n)
			}
			i0 := int(p)
			fr := p - float64(i0)
			i1 := i0 + 1
			if i0 >= n {
				i0 -= n
			}
			if i1 >= n {
				i1 -= n
			}
			w := f.cBuf[ch][i0]*(1-fr) + f.cBuf[ch][i1]*fr
			*out += w * c.Mix * 0.8
		}
		f.cIdx++
		if f.cIdx >= n {
			f.cIdx = 0
		}
		f.cPhase += inc
		if f.cPhase >= 1 {
			f.cPhase -= 1
		}
	}
}

func (f *fxChain) tremolo(l, r []float64, t *TremCfg) {
	inc := t.Rate / SR
	for i := range l {
		s := 0.5 + 0.5*math.Sin(2*math.Pi*f.tPhase)
		gl := 1 - t.Depth*s
		gr := gl
		if t.Pan {
			gr = 1 - t.Depth*(1-s)
		}
		l[i] *= gl
		r[i] *= gr
		f.tPhase += inc
		if f.tPhase >= 1 {
			f.tPhase -= 1
		}
	}
}

func (f *fxChain) delay(l, r []float64, d *DelayCfg) {
	n := len(f.dBuf[0])
	target := d.Time * 0.001 * SR
	lp := 1 - math.Exp(-2*math.Pi*d.Tone/SR)
	for i := range l {
		f.dTime += (target - f.dTime) * 0.0005 // glisse douce quand on change la durée
		p := float64(f.dIdx) - f.dTime
		for p < 0 {
			p += float64(n)
		}
		i0 := int(p)
		fr := p - float64(i0)
		i1 := i0 + 1
		if i0 >= n {
			i0 -= n
		}
		if i1 >= n {
			i1 -= n
		}
		in := [2]float64{l[i], r[i]}
		for ch := 0; ch < 2; ch++ {
			w := f.dBuf[ch][i0]*(1-fr) + f.dBuf[ch][i1]*fr
			f.dLP[ch] += (w - f.dLP[ch]) * lp
			f.dBuf[ch][f.dIdx] = in[ch] + f.dLP[ch]*d.Feedback
			if ch == 0 {
				l[i] += w * d.Mix
			} else {
				r[i] += w * d.Mix
			}
		}
		f.dIdx++
		if f.dIdx >= n {
			f.dIdx = 0
		}
	}
}

// ---------------------------------------------------------------- liaison avec le moteur

func (e *Engine) SetFX(c FXCfg) {
	c.sanitize()
	atomic.StorePointer(&e.fxp, unsafePtr(&c))
}

// FX renvoie une copie des réglages d'effets actuels.
func (e *Engine) FX() FXCfg {
	if p := atomic.LoadPointer(&e.fxp); p != nil {
		return *(*FXCfg)(p)
	}
	return defaultFX()
}
