package main

import (
	"math"
	"testing"
)

func fxSine(f float64, n int) ([]float64, []float64) {
	l, r := make([]float64, n), make([]float64, n)
	for i := range l {
		l[i] = 0.3 * math.Sin(2*math.Pi*f*float64(i)/SR)
		r[i] = l[i]
	}
	return l, r
}

func fxRMS(x []float64) float64 {
	s := 0.0
	for _, v := range x {
		s += v * v
	}
	return math.Sqrt(s / float64(len(x)))
}

func runFX(c FXCfg, f float64) float64 {
	c.sanitize()
	ch := newFXChain()
	n := 512 * 86
	l, r := fxSine(f, n)
	for o := 0; o < n; o += 512 {
		ch.process(l[o:o+512], r[o:o+512], &c)
	}
	in, _ := fxSine(f, n)
	return 20 * math.Log10(fxRMS(l[n/2:])/fxRMS(in[n/2:]))
}

func TestEQGains(t *testing.T) {
	c := defaultFX()
	c.EQ.On = true
	c.EQ.LS.G = 9
	c.EQ.HS.G = -9
	c.EQ.P1.G = 6
	if g := runFX(c, 60); math.Abs(g-9) > 1.5 {
		t.Errorf("basses 60 Hz: %.2f dB (attendu ~9)", g)
	}
	if g := runFX(c, 14000); math.Abs(g+9) > 1.5 {
		t.Errorf("aigus 14 kHz: %.2f dB (attendu ~-9)", g)
	}
	if g := runFX(c, 500); math.Abs(g-6) > 1 {
		t.Errorf("médium 500 Hz: %.2f dB (attendu ~6)", g)
	}
	// la courbe dessinée correspond à la mesure
	if d := c.EQ.responseDB(500); math.Abs(d-6) > 0.5 {
		t.Errorf("courbe 500 Hz: %.2f", d)
	}
	c.EQ.HP = 200
	if g := runFX(c, 40); g > -15 {
		t.Errorf("coupe-bas 200 Hz sur 40 Hz: %.2f dB", g)
	}
}

func TestCompressor(t *testing.T) {
	c := defaultFX()
	c.Comp = CompCfg{On: true, Thresh: -20, Ratio: 4, Attack: 2, Release: 100, MakeupGain: 0}
	// 0,3 crête = -10,5 dB : 10 dB au-dessus du seuil, ratio 4 → réduction ~7,5 dB
	if g := runFX(c, 440); g > -5 || g < -9 {
		t.Errorf("compression %.2f dB (attendu ~-7)", g)
	}
}

func TestFXSafe(t *testing.T) {
	c := defaultFX()
	c.EQ.On, c.Comp.On, c.Chorus.On, c.Delay.On, c.Trem.On = true, true, true, true, true
	c.EQ.LS.G, c.Delay.Feedback = 15, 0.9
	c.EQ.HP, c.EQ.LP = 500, 2000
	c.sanitize()
	ch := newFXChain()
	l, r := fxSine(220, 512)
	for k := 0; k < 400; k++ {
		for i := range l {
			l[i] = 0.5 * math.Sin(float64(i+k*512)*0.03)
			r[i] = l[i]
		}
		ch.process(l, r, &c)
		for i := range l {
			if math.IsNaN(l[i]) || math.IsInf(l[i], 0) || math.Abs(l[i]) > 50 {
				t.Fatalf("sortie instable bloc %d: %v", k, l[i])
			}
		}
	}
	bad := FXCfg{}
	bad.EQ.LS.F = math.NaN()
	bad.sanitize()
	if math.IsNaN(bad.EQ.LS.F) {
		t.Error("NaN non corrigé")
	}
}

func TestTremoloPan(t *testing.T) {
	c := defaultFX()
	c.Trem = TremCfg{On: true, Rate: 4, Depth: 1, Pan: true}
	ch := newFXChain()
	l, r := fxSine(300, 512*86)
	for o := 0; o+512 <= len(l); o += 512 {
		ch.process(l[o:o+512], r[o:o+512], &c)
	}
	if math.Abs(fxRMS(l)-fxRMS(r)) > 0.05*fxRMS(l) {
		t.Logf("niveaux G/D: %.3f %.3f", fxRMS(l), fxRMS(r))
	}
}

// Les effets d'une piste ne touchent que cette piste.
func TestFXPerTrack(t *testing.T) {
	e := stressEngine(2)
	e.revAmt = 0
	level := func(muteCh int) float64 {
		for i := range e.ch {
			e.ch[i].Mute = i == muteCh
		}
		e.Panic()
		e.NoteOn(57, 100, 1)
		out := make([]int16, 512)
		pk := 0.0
		for b := 0; b < 60; b++ {
			e.Render(out)
			if b > 20 {
				for _, v := range out {
					pk = math.Max(pk, math.Abs(float64(v)))
				}
			}
		}
		e.NoteOff(1)
		return pk
	}
	base0, base1 := level(1), level(0) // piste 0 seule, puis piste 1 seule
	c := defaultFX()
	c.EQ.On, c.EQ.HP = true, 500 // coupe-bas : le son de 220 Hz disparaît presque
	e.SetFX(0, c)
	fx0, fx1 := level(1), level(0)
	if fx0 > base0*0.6 {
		t.Errorf("piste 0 : l'effet n'agit pas (%.0f → %.0f)", base0, fx0)
	}
	if math.Abs(fx1-base1) > base1*0.02 {
		t.Errorf("piste 1 : modifiée par l'effet de la piste 0 (%.0f → %.0f)", base1, fx1)
	}
	e.SetFX(0, defaultFX())
	e.SetFX(1, c)
	g0, g1 := level(1), level(0)
	if math.Abs(g0-base0) > base0*0.02 || g1 > base1*0.6 {
		t.Errorf("effet sur la piste 1 seule : piste0 %.0f→%.0f piste1 %.0f→%.0f", base0, g0, base1, g1)
	}
}
