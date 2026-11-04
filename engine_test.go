package main

import (
	"math"
	"math/rand"
	"testing"
	"time"
)

// moteur avec les sons intégrés demandés sur les canaux 0, 1, 2...
func eng(insts ...int) *Engine {
	e := NewEngine()
	e.revAmt = 0
	for i, in := range insts {
		e.SetSource(i, builtinSource{in}, instLabels[in], instCats[in])
	}
	return e
}

// rend `sec` secondes et renvoie (gauche, droite)
func runLR(e *Engine, sec float64) ([]float64, []float64) {
	B := 256
	n := int(sec * SR / float64(B))
	var l, r []float64
	buf := make([]int16, 2*B)
	for i := 0; i < n; i++ {
		e.Render(buf)
		for j := 0; j < B; j++ {
			l = append(l, float64(buf[2*j])/32000)
			r = append(r, float64(buf[2*j+1])/32000)
		}
	}
	return l, r
}

func run(e *Engine, sec float64) []float64 {
	l, _ := runLR(e, sec)
	return l
}

func power(y []float64, f float64) float64 {
	a, b := int(0.3*SR), len(y)
	var re, im float64
	for i := a; i < b; i++ {
		w := 0.5 - 0.5*math.Cos(twoPi*float64(i-a)/float64(b-a))
		ph := twoPi * f * float64(i) / SR
		re += y[i] * w * math.Cos(ph)
		im += y[i] * w * math.Sin(ph)
	}
	return re*re + im*im
}

// écart (en cents) entre la fréquence dominante autour de f0 et f0
func peakCents(y []float64, f0 float64) float64 {
	best, bf := 0.0, f0
	for c := -150.0; c <= 150; c += 2 {
		f := f0 * math.Pow(2, c/1200)
		if p := power(y, f); p > best {
			best, bf = p, f
		}
	}
	return 1200 * math.Log2(bf/f0)
}

func rms(y []float64) float64 {
	s := 0.0
	for _, v := range y {
		s += v * v
	}
	return math.Sqrt(s / float64(len(y)))
}

func peakAbs(y []float64) float64 {
	p := 0.0
	for _, v := range y {
		p = math.Max(p, math.Abs(v))
	}
	return p
}

func TestPitchBuiltin(t *testing.T) {
	rand.Seed(3)
	for inst := 0; inst < nInst; inst++ {
		for _, n := range []int{36, 48, 60, 72, 84, 96} {
			e := eng(inst)
			e.NoteOn(n, 100, 1)
			y := run(e, 1.3)
			ne := n
			if inst == iGuitar {
				ne = fold(n, 40, 88)
			} else if inst == iBass {
				ne = fold(n-12, 28, 67)
			}
			c := peakCents(y, midiFreq(ne))
			for _, v := range y {
				if math.IsNaN(v) {
					t.Fatalf("NaN %s %d", instLabels[inst], n)
				}
			}
			lim := 14.0
			if inst >= iUpright && inst <= iSquare {
				lim = 26
			}
			if math.Abs(c) > lim {
				t.Errorf("%s note %d hors ton: %.1f cents", instLabels[inst], n, c)
			}
		}
	}
}

func TestLevelsBuiltin(t *testing.T) {
	for inst := 0; inst < nInst; inst++ {
		e := eng(inst)
		e.NoteOn(60, 100, 1)
		y := run(e, 0.5)
		t.Logf("%-14s rms=%.3f pic=%.3f", instLabels[inst], rms(y), peakAbs(y))
		if rms(y) < 0.03 || rms(y) > 0.12 {
			t.Errorf("niveau inattendu pour %s: %.3f", instLabels[inst], rms(y))
		}
	}
}

func TestPedals(t *testing.T) {
	e := eng(iGrand)
	e.NoteOn(60, 100, 1)
	run(e, 0.1)
	e.SetSustain(true)
	e.NoteOff(1)
	run(e, 1.0)
	if len(e.voices) != 1 {
		t.Fatalf("avec pédale: %d voix", len(e.voices))
	}
	e.SetSustain(false)
	run(e, 0.6)
	if len(e.voices) != 0 {
		t.Fatalf("pédale relâchée: %d voix", len(e.voices))
	}
	e.NoteOn(60, 100, 2)
	run(e, 0.1)
	e.NoteOff(2)
	run(e, 0.6)
	if len(e.voices) != 0 {
		t.Fatalf("sans pédale: %d voix", len(e.voices))
	}
	// sostenuto
	e = eng(iPad)
	e.NoteOn(60, 100, 1)
	run(e, 0.1)
	e.Control(66, 127)
	e.NoteOn(64, 100, 2)
	e.NoteOff(1)
	e.NoteOff(2)
	run(e, 1.5)
	if len(e.voices) != 1 || e.voices[0].base().tag != 1 {
		t.Fatalf("sostenuto: %d voix", len(e.voices))
	}
	e.Control(66, 0)
	run(e, 1.5)
	if len(e.voices) != 0 {
		t.Fatalf("sostenuto relâché: %d voix", len(e.voices))
	}
	// all notes off, limite de voix
	e = eng(iLead)
	for n := 60; n < 70; n++ {
		e.NoteOn(n, 90, n)
	}
	run(e, 0.1)
	e.Control(123, 0)
	run(e, 0.4)
	if len(e.voices) != 0 {
		t.Fatalf("CC123: %d", len(e.voices))
	}
	e = eng(iGrand, iUpright, iPad)
	for n := 21; n < 109; n++ {
		e.NoteOn(n, 100, 100+n)
	}
	run(e, 1)
	if len(e.voices) > maxVoice {
		t.Fatalf("trop de voix: %d", len(e.voices))
	}
}

func TestChannelControls(t *testing.T) {
	// pan : tout à gauche => droite silencieuse
	e := eng(iPad)
	e.ch[0].Pan = -1
	e.NoteOn(60, 100, 1)
	l, r := runLR(e, 0.5)
	if rms(l) < 0.02 || rms(r) > 0.001 {
		t.Fatalf("pan gauche: L=%.4f R=%.4f", rms(l), rms(r))
	}
	// pan centre : égal
	e = eng(iPad)
	e.NoteOn(60, 100, 1)
	l, r = runLR(e, 0.5)
	if math.Abs(rms(l)-rms(r)) > 0.002 {
		t.Fatalf("pan centre: L=%.4f R=%.4f", rms(l), rms(r))
	}
	// fader -6 dB => environ moitié
	e = eng(iGrand)
	e.NoteOn(60, 100, 1)
	a := rms(run(e, 0.5))
	e = eng(iGrand)
	e.ch[0].Vol = -6.02
	e.NoteOn(60, 100, 1)
	b := rms(run(e, 0.5))
	if math.Abs(b/a-0.5) > 0.05 {
		t.Fatalf("fader: rapport %.3f (attendu 0.5)", b/a)
	}
	// mute / solo
	e = eng(iGrand, iPad)
	e.ch[0].Mute = true
	e.NoteOn(60, 100, 1)
	if len(e.voices) != 1 {
		t.Fatalf("mute: %d voix", len(e.voices))
	}
	e = eng(iGrand, iPad)
	e.ch[1].Solo = true
	e.NoteOn(60, 100, 1)
	if len(e.voices) != 1 || e.voices[0].base().ch.idx != 1 {
		t.Fatalf("solo: %d voix", len(e.voices))
	}
	// octave + transposition par canal
	e = eng(iPad)
	e.ch[0].Oct = -1
	e.ch[0].Trans = 2
	e.NoteOn(60, 100, 1)
	y := run(e, 1.3)
	f := midiFreq(60 - 12 + 2)
	if c := peakCents(y, f); math.Abs(c) > 26 {
		t.Fatalf("oct/trans: %.1f cents", c)
	}
	// hold : la note reste tenue après relâchement
	e = eng(iPad)
	e.SetHold(0, true)
	e.NoteOn(60, 100, 1)
	run(e, 0.1)
	e.NoteOff(1)
	run(e, 1.5)
	if len(e.voices) != 1 {
		t.Fatalf("hold: %d voix", len(e.voices))
	}
	e.SetHold(0, false)
	run(e, 1.5)
	if len(e.voices) != 0 {
		t.Fatalf("hold relâché: %d voix", len(e.voices))
	}
	// pédale ignorée par le canal
	e = eng(iPad)
	e.SetPedalAware(0, false)
	e.SetSustain(true)
	e.NoteOn(60, 100, 1)
	run(e, 0.1)
	e.NoteOff(1)
	run(e, 1.5)
	if len(e.voices) != 0 {
		t.Fatalf("canal sans pédale: %d voix", len(e.voices))
	}
}

func TestFilterAndEnvelope(t *testing.T) {
	hi := func(cut float64) float64 {
		e := eng(iLead)
		e.ch[0].P[pCutoff] = cut
		e.NoteOn(48, 100, 1)
		y := run(e, 0.6)
		d := make([]float64, len(y)-1)
		for i := range d {
			d[i] = y[i+1] - y[i]
		}
		return rms(d)
	}
	open, closed := hi(1.0), hi(0.25)
	t.Logf("énergie aiguë ouvert=%.4f fermé=%.4f", open, closed)
	if closed > open*0.5 {
		t.Fatalf("le cutoff n'atténue pas assez")
	}
	e := eng(iSquare)
	e.NoteOn(60, 100, 1)
	a := rms(run(e, 0.05))
	e = eng(iSquare)
	e.ch[0].P[pAttack] = 0.7
	e.NoteOn(60, 100, 1)
	b := rms(run(e, 0.05))
	if b > a*0.3 {
		t.Fatalf("attaque: %.4f vs %.4f", b, a)
	}
	for _, rel := range []float64{0, 0.8} {
		e = eng(iSquare)
		e.ch[0].P[pRelease] = rel
		e.NoteOn(60, 100, 1)
		run(e, 0.2)
		e.NoteOff(1)
		run(e, 0.5)
		if rel == 0 && len(e.voices) != 0 || rel > 0 && len(e.voices) != 1 {
			t.Fatalf("release %.1f: %d voix", rel, len(e.voices))
		}
	}
}

func TestReverbTail(t *testing.T) {
	e := eng(iGuitar)
	e.revAmt = 0.5
	e.NoteOn(60, 100, 1)
	run(e, 0.3)
	e.Panic()
	l, _ := runLR(e, 0.5)
	if rms(l) < 0.0005 {
		t.Fatalf("pas de queue de réverbération: %.5f", rms(l))
	}
	for _, v := range l {
		if math.IsNaN(v) {
			t.Fatal("NaN")
		}
	}
}

func TestBend(t *testing.T) {
	e := eng(iGrand)
	e.SetBend(8191)
	e.NoteOn(69, 100, 1)
	y := run(e, 1.3)
	c := peakCents(y, 440*math.Pow(2, 2.0/12))
	if math.Abs(c) > 10 {
		t.Errorf("bend %.1f cents", c)
	}
}

func TestPerf(t *testing.T) {
	cases := []struct {
		name  string
		insts []int
		notes int
	}{
		{"piano 10 notes", []int{iGrand}, 10},
		{"piano 30 notes", []int{iGrand}, 30},
		{"7 sons x 10 notes", []int{0, 1, 2, 3, 4, 5, 6}, 10},
	}
	for _, c := range cases {
		e := eng(c.insts...)
		e.revAmt = 0.2
		for i := 0; i < c.notes; i++ {
			e.NoteOn(40+(i*7)%50, 100, i)
		}
		buf := make([]int16, 768)
		for i := 0; i < 20; i++ {
			e.Render(buf)
		}
		t0 := time.Now()
		N := 300
		for i := 0; i < N; i++ {
			e.Render(buf)
		}
		per := time.Since(t0).Seconds() / float64(N) * 1000
		budget := 384.0 / SR * 1000
		t.Logf("%-20s voix=%3d  %.2f ms pour 384 images (budget %.1f ms) -> %.0f%%", c.name, e.VoiceCount(), per, budget, per/budget*100)
	}
}

func TestDiagTone(t *testing.T) {
	e := NewEngine()
	e.SetLicenseGate(true, time.Time{}) // son verrouillé : le bip doit sortir quand même
	e.StartTone()
	out := make([]int16, 2*4800)
	e.Render(out)
	pk := 0
	for _, v := range out {
		if int(v) > pk {
			pk = int(v)
		}
	}
	if pk < 3000 {
		t.Fatalf("le bip de test doit être audible, crête %d", pk)
	}
	pulled, _ := e.DiagRead()
	if pulled != 4800 {
		t.Fatalf("images demandées : %d", pulled)
	}
}
