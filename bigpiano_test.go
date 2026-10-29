package main

import (
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"
	"testing"
	"time"
)

// banc d'essai : un gros piano SF2 stéréo (88 touches x 3 nuances x 2 canaux).
// Lancer avec : BIGPIANO=/tmp/dossier go test -run BigPiano -v
func TestBigPiano(t *testing.T) {
	dir := os.Getenv("BIGPIANO")
	if dir == "" {
		t.Skip("BIGPIANO non défini")
	}
	path := filepath.Join(dir, "big.sf2")
	if _, err := os.Stat(path); err != nil {
		var samples []tSample
		var zones []tZone
		for key := 21; key <= 108; key++ {
			f := 440 * math.Pow(2, float64(key-69)/12)
			for layer := 0; layer < 3; layer++ {
				d := make([]int16, 5*44100)
				for i := range d {
					tt := float64(i) / 44100
					v := (math.Sin(2*math.Pi*f*tt) + 0.5*math.Sin(4*math.Pi*f*tt) + 0.25*math.Sin(6*math.Pi*f*tt)) * math.Exp(-tt*0.8) * (0.3 + 0.2*float64(layer))
					d[i] = int16(v * 12000)
				}
				id := len(samples)
				samples = append(samples,
					tSample{name: fmt.Sprintf("L%d_%d", key, layer), data: d, rate: 44100, pitch: uint8(key), loopS: 100000, loopE: 200000, typ: 4},
					tSample{name: fmt.Sprintf("R%d_%d", key, layer), data: d, rate: 44100, pitch: uint8(key), loopS: 100000, loopE: 200000, typ: 2})
				vlo, vhi := layer*43, layer*43+42
				if layer == 2 {
					vhi = 127
				}
				zones = append(zones,
					tZone{gRange(43, key, key), gRange(44, vlo, vhi), gAmt(54, 1), gAmt(38, -1200), gAmt(53, id)},
					tZone{gRange(43, key, key), gRange(44, vlo, vhi), gAmt(54, 1), gAmt(38, -1200), gAmt(53, id+1)})
			}
		}
		b := buildSF2(2, samples, []tInst{{"BigPiano", zones}}, []tPreset{{"Big Piano", 0, 0, []tZone{{gAmt(41, 0)}}}})
		if err := os.WriteFile(path, b, 0o644); err != nil {
			t.Fatal(err)
		}
	}
	st, _ := os.Stat(path)
	t0 := time.Now()
	sf, err := OpenSF2(path)
	if err != nil {
		t.Fatal(err)
	}
	t1 := time.Now()
	regs, err := sf.LoadPreset(sf.Presets()[0].Index)
	if err != nil {
		t.Fatal(err)
	}
	t2 := time.Now()
	fmt.Printf("fichier %.0f Mo : ouverture %v, chargement du son %v (%d zones)\n", float64(st.Size())/1e6, t1.Sub(t0), t2.Sub(t1), len(regs))

	for _, rate := range []int{48000} {
		e := NewEngine()
		e.SetSampleRate(rate)
		e.revAmt = 0.15
		e.SetSource(0, &sampleSource{regions: regs}, "Big", "SF2")
		e.SetSource(1, &sampleSource{regions: regs}, "Big", "SF2")
		e.SetSustain(true)
		const B = 128
		out := make([]int16, B*2)
		var times []time.Duration
		var noteOn []time.Duration
		for chord := 0; chord < 24; chord++ {
			for k := 0; k < 6; k++ {
				n := 40 + (chord*5+k*4)%48
				a := time.Now()
				e.NoteOn(n, 60+(chord*7+k*13)%60, 1)
				noteOn = append(noteOn, time.Since(a))
			}
			for blk := 0; blk < 60; blk++ {
				a := time.Now()
				e.Render(out)
				times = append(times, time.Since(a))
			}
		}
		sort.Slice(times, func(i, j int) bool { return times[i] < times[j] })
		sort.Slice(noteOn, func(i, j int) bool { return noteOn[i] < noteOn[j] })
		budget := time.Duration(float64(B) / float64(rate) * float64(time.Second))
		fmt.Printf("%d Hz, bloc de %d images (budget %v) : voix=%d  rendu médian=%v  95%%=%v  pire=%v ; NoteOn pire=%v\n",
			rate, B, budget, e.VoiceCount(), times[len(times)/2], times[len(times)*95/100], times[len(times)-1], noteOn[len(noteOn)-1])
	}
}
