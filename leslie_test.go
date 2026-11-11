package main

import (
	"math"
	"testing"
)

// enveloppe d'amplitude du canal gauche, par tranches de 10 ms
func lesEnvelope(x []float64) []float64 {
	w := int(0.01 * SR)
	var out []float64
	for o := 0; o+w <= len(x); o += w {
		out = append(out, fxRMS(x[o:o+w]))
	}
	return out
}

func lesDepth(env []float64) float64 {
	lo, hi := math.Inf(1), 0.0
	for _, v := range env {
		lo, hi = math.Min(lo, v), math.Max(hi, v)
	}
	return hi / math.Max(lo, 1e-9)
}

func runLeslie(fast bool, freq float64, secs float64) (l, r []float64) {
	c := defaultFX()
	c.Leslie.On = true
	ch := newFXChain()
	n := int(secs * SR)
	n -= n % 512
	l, r = fxSine(freq, n)
	for o := 0; o < n; o += 512 {
		ch.process(l[o:o+512], r[o:o+512], &c, fast)
	}
	return
}

func TestLeslieCleanAndModulated(t *testing.T) {
	for _, fast := range []bool{false, true} {
		l, r := runLeslie(fast, 2000, 3)
		for i := range l {
			if math.IsNaN(l[i]) || math.IsInf(l[i], 0) || math.Abs(l[i]) > 1 || math.Abs(r[i]) > 1 {
				t.Fatalf("fast=%v : valeur aberrante %v à %d", fast, l[i], i)
			}
		}
		in, _ := fxSine(2000, len(l))
		g := 20 * math.Log10(fxRMS(l[len(l)/2:])/fxRMS(in[len(l)/2:]))
		if g < -6 || g > 3 {
			t.Errorf("fast=%v : niveau %.1f dB (attendu proche de 0)", fast, g)
		}
		d := lesDepth(lesEnvelope(l[len(l)/2:]))
		if d < 1.2 {
			t.Errorf("fast=%v : pas assez de mouvement (rapport %.2f)", fast, d)
		}
		// gauche et droite doivent bouger différemment (effet stéréo de la cabine)
		diff := 0.0
		for i := len(l) / 2; i < len(l); i++ {
			diff += math.Abs(l[i] - r[i])
		}
		if diff/float64(len(l)/2) < 0.01 {
			t.Errorf("fast=%v : gauche et droite identiques", fast)
		}
	}
}

func TestLeslieGraves(t *testing.T) {
	// les graves (tambour) passent aussi, sans disparaître
	l, _ := runLeslie(true, 120, 3)
	in, _ := fxSine(120, len(l))
	g := 20 * math.Log10(fxRMS(l[len(l)/2:])/fxRMS(in[len(l)/2:]))
	if g < -6 || g > 3 {
		t.Errorf("graves : niveau %.1f dB", g)
	}
}

func TestLeslieSpeedRamp(t *testing.T) {
	// passage de lent à rapide : le pavillon accélère progressivement (ni instantané, ni trop long)
	s := newLeslieState()
	l, r := fxSine(2000, 512)
	s.process(l, r, false)
	if math.Abs(s.hornSp-leslieHornSlow) > 0.01 {
		t.Fatalf("départ en lent attendu, vitesse %.2f", s.hornSp)
	}
	blocks := int(0.25 * SR / 512)
	for i := 0; i < blocks; i++ {
		l, r := fxSine(2000, 512)
		s.process(l, r, true)
	}
	if s.hornSp >= leslieHornFast*0.95 || s.hornSp <= leslieHornSlow*1.5 {
		t.Errorf("après 0,25 s : vitesse du pavillon %.2f (montée progressive attendue)", s.hornSp)
	}
	if s.drumSp >= s.hornSp {
		t.Errorf("le tambour (lourd) doit accélérer moins vite que le pavillon : %.2f >= %.2f", s.drumSp, s.hornSp)
	}
	for i := 0; i < int(4*SR/512); i++ {
		l, r := fxSine(2000, 512)
		s.process(l, r, true)
	}
	if s.hornSp < leslieHornFast*0.97 {
		t.Errorf("après 4 s : pavillon à %.2f, rapide pas atteint", s.hornSp)
	}
}

func TestLeslieSavedAndLearnable(t *testing.T) {
	if !learnable("lesFast") {
		t.Fatal("la vitesse du Leslie doit être réglable par MIDI Learn")
	}
	u, _, e := newTestUI(t, "")
	u.learnApply("lesFast", 127, true)
	if !e.LeslieFast() {
		t.Fatal("MIDI Learn n'a pas passé le Leslie en rapide")
	}
	b := u.StateBytes()
	u2, _, e2 := newTestUI(t, "")
	if !u2.SetStateBytes(b) || !e2.LeslieFast() {
		t.Fatal("la vitesse du Leslie n'est pas gardée dans l'état du plugin")
	}
	u.fxSetAt(0, func(c *FXCfg) { c.Leslie.On = true })
	if !u.anyLeslie() || e.fxOf(0) == nil || !e.fxOf(0).Leslie.On {
		t.Fatal("le Leslie de la piste 1 n'est pas transmis au moteur")
	}
}
