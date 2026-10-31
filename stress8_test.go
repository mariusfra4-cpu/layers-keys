package main

import (
	"fmt"
	"math"
	"sort"
	"sync/atomic"
	"testing"
	"time"
)

// Cas réel : 8 pistes SF2 stéréo (2 zones par note), accords de 6 notes, pédale de sustain.
func stressEngine(tracks int) *Engine {
	e := NewEngine()
	SR = 48000
	e.SetSampleRate(48000)
	e.revAmt = 0.15
	data := make([]int16, 48000*6)
	for i := range data {
		tt := float64(i) / 48000
		data[i] = int16(9000 * (math.Sin(2*math.Pi*220*tt) + 0.5*math.Sin(2*math.Pi*440*tt) + 0.3*math.Sin(2*math.Pi*660*tt)) * math.Exp(-tt*0.7))
	}
	for c := 0; c < tracks; c++ {
		var regs []*Region
		for l := 0; l < 2; l++ { // gauche et droite
			r := newPlainRegion(data, 48000, 57)
			r.Release = 0.4
			r.LoopMode, r.LoopStart, r.LoopEnd = 1, 40000, 200000
			r.FcHz, r.QdB = 9000, 3
			regs = append(regs, r)
		}
		e.SetSource(c, &sampleSource{regions: regs}, "SF2", "SF2")
	}
	e.SetSustain(true)
	return e
}

type stressResult struct {
	p50, p95, worst time.Duration
	cutHeld         int64
	live, limit     int
	over1           float64 // part des échantillons qui dépassent 1,0 avant limitation
	peakOut         float64
}

func runStress(tracks int, secs int) stressResult {
	e := stressEngine(tracks)
	const B = 128
	out := make([]int16, B*2)
	var times []time.Duration
	var maxLive int
	chords := [][]int{{48, 55, 60, 64, 67, 72}, {45, 52, 57, 60, 64, 69}, {41, 53, 57, 60, 65, 69}, {43, 50, 55, 59, 62, 67}}
	blocksPerChord := int(0.6 * 48000 / B)
	nb := secs * 48000 / B
	var over, total int
	var peak float64
	for blk := 0; blk < nb; blk++ {
		if blk%blocksPerChord == 0 {
			e.SetSustain(false) // le pianiste relève puis rebaisse la pédale à chaque accord
			e.SetSustain(true)
			for k, n := range chords[(blk/blocksPerChord)%4] {
				e.NoteOn(n, 100, 1000+k)
			}
		}
		if blk%blocksPerChord == blocksPerChord/2 {
			for k := range chords[0] {
				e.NoteOff(1000 + k)
			}
		}
		a := time.Now()
		e.Render(out)
		times = append(times, time.Since(a))
		for _, v := range out {
			x := math.Abs(float64(v)) / 32768
			if x > peak {
				peak = x
			}
		}
		if l, _ := e.Voices(); l > maxLive {
			maxLive = l
		}
		for i := range e.mixL[:B] {
			total++
			if math.Abs(e.mixL[i]*e.master*1.414) > 1 {
				over++
			}
		}
	}
	sort.Slice(times, func(i, j int) bool { return times[i] < times[j] })
	_, lim := e.Voices()
	return stressResult{times[len(times)/2], times[len(times)*95/100], times[len(times)-1], atomic.LoadInt64(&e.cutHeld), maxLive, lim, float64(over) / float64(total), peak}
}

func TestStress8(t *testing.T) {
	for _, n := range []int{2, 4, 8} {
		r := runStress(n, 12)
		budget := 2666667 * time.Nanosecond
		fmt.Printf("%d pistes : rendu médian %v, 95%% %v, pire %v (budget %v) ; voix max %d, limite finale %d ; notes tenues coupées : %d ; échantillons >1,0 : %.1f%%\n",
			n, r.p50, r.p95, r.worst, budget, r.live, r.limit, r.cutHeld, r.over1*100)
	}
}

// Le limiteur doit baisser le volume en douceur (sans écraser les crêtes d'un coup) et ne jamais dépasser 1.
func TestLimiterSmooth(t *testing.T) {
	e := NewEngine()
	const B = 128
	e.mixL, e.mixR = make([]float64, B), make([]float64, B)
	var out []float64
	for blk := 0; blk < 60; blk++ {
		for i := 0; i < B; i++ {
			n := blk*B + i
			amp := 0.3
			switch { // un accord très fort : 10 fois plus que la limite, avec une attaque de 5 ms puis un retour progressif
			case n >= 1280 && n < 1280+240:
				amp = 0.3 + 2.7*float64(n-1280)/240
			case n >= 1520 && n < 5120:
				amp = 3.0
			case n >= 5120 && n < 5120+2400:
				amp = 3.0 - 2.7*float64(n-5120)/2400
			}
			x := amp * math.Sin(2*math.Pi*220*float64(n)/48000)
			e.mixL[i], e.mixR[i] = x, x
		}
		e.limitBlock(B, 1)
		for i := 0; i < B; i++ {
			out = append(out, softLimit(e.mixL[i]))
		}
	}
	pk, maxStep := 0.0, 0.0
	for i, v := range out {
		if math.Abs(v) > pk {
			pk = math.Abs(v)
		}
		if i > 0 && math.Abs(v-out[i-1]) > maxStep {
			maxStep = math.Abs(v - out[i-1])
		}
	}
	if pk > 0.999 {
		t.Fatalf("dépasse 1 : %v", pk)
	}
	// un sinus de 220 Hz à 48 kHz change d'au plus 2π·220/48000 ≈ 0,029 par échantillon (à amplitude 1)
	if maxStep > 0.06 {
		t.Fatalf("saut brutal dans le signal (%.3f) : le limiteur écrase les crêtes", maxStep)
	}
	// la fin (sons faibles) revient au volume normal
	tail := 0.0
	for _, v := range out[len(out)-B:] {
		if math.Abs(v) > tail {
			tail = math.Abs(v)
		}
	}
	t.Logf("crête %.3f, plus grand saut %.3f (fin de test %.2f)", pk, maxStep, tail)
}
