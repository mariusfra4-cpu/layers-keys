package main

import (
	"fmt"
	"math"
	"testing"
)

func tailSec(e *Engine, src Source, relP float64, hold float64) float64 {
	e.SetSource(0, src, "t", "t")
	e.ch[0].P[pRelease] = relP
	e.NoteOn(60, 100, 1)
	out := make([]int16, 256)
	nb := int(hold * SR / 128)
	for i := 0; i < nb; i++ {
		e.Render(out)
	}
	e.NoteOff(1)
	t := 0.0
	last := 0.0
	for i := 0; i < int(12*SR/128); i++ {
		e.Render(out)
		t += 128 / SR
		pk := 0.0
		for _, v := range out {
			pk = math.Max(pk, math.Abs(float64(v))/32768)
		}
		if pk > 0.001 { // -60 dB
			last = t
		}
	}
	return last
}

func TestReleaseKnobAllSounds(t *testing.T) {
	e := NewEngine()
	SR = 48000
	e.SetSampleRate(48000)
	e.revAmt = 0
	for i := 1; i < nCh; i++ {
		e.ch[i].Mute = true
	}
	data := make([]int16, 48000*6)
	for i := range data {
		tt := float64(i) / 48000
		data[i] = int16(9000 * math.Sin(2*math.Pi*220*tt) * math.Exp(-tt*0.7))
	}
	mk := func(rel float64, loop int) Source {
		r := newPlainRegion(data, 48000, 57)
		r.Release = rel
		if loop == 1 {
			r.LoopMode, r.LoopStart, r.LoopEnd = 1, 40000, 200000
		}
		return &sampleSource{regions: []*Region{r}}
	}
	cases := map[string]Source{
		"SF2 boucle rel=0.08": mk(0.08, 1), "SF2 sans boucle rel=0.08": mk(0.08, 0), "SF2 boucle rel=0.4": mk(0.4, 1),
		"piano intégré": builtinSource{iGrand}, "nappe intégrée": builtinSource{iPad},
	}
	for name, s := range cases {
		t0, t5, t1 := tailSec(e, s, 0, 0.5), tailSec(e, s, 0.5, 0.5), tailSec(e, s, 1, 0.5)
		t.Log(fmt.Sprintf("%s : 0 → %.2f s · 0,5 → %.2f s · 1 → %.2f s", name, t0, t5, t1))
		if !(t5 > t0+0.8 && t1 > t5+0.8) {
			t.Errorf("%s : le relâchement ne se fait pas assez sentir (%.2f / %.2f / %.2f s)", name, t0, t5, t1)
		}
	}
}
