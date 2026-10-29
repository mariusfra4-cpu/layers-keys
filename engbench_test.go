package main

import "testing"

func benchEngine(nch, notes int) *Engine {
	e := NewEngine()
	for i := 0; i < nch; i++ {
		e.SetSource(i, builtinSource{iGrand}, "P", "x")
	}
	for k := 0; k < notes; k++ {
		e.NoteOn(36+k*3, 100, 1000+k)
	}
	return e
}

func BenchmarkRender128(b *testing.B) {
	e := benchEngine(8, 10)
	out := make([]int16, 256)
	for i := 0; i < 50; i++ {
		e.Render(out)
	}
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		e.Render(out)
	}
	l, _ := e.Load()
	b.Logf("voix=%d charge=%.3f", e.VoiceCount(), l)
}

func BenchmarkNoteOn(b *testing.B) {
	e := benchEngine(8, 0)
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		e.NoteOn(60+i%24, 100, 1000+i%24)
		e.NoteOff(1000 + i%24)
		if i%64 == 0 {
			e.Panic()
		}
	}
}

func sampEngine(nch, notes, layers int) *Engine {
	e := NewEngine()
	data := make([]int16, 48000*4)
	for i := range data {
		data[i] = int16(8000 * float64((i*7)%200-100) / 100)
	}
	for i := 0; i < nch; i++ {
		var regs []*Region
		for l := 0; l < layers; l++ {
			r := newPlainRegion(data, 48000, 60)
			r.Release = 0.4
			r.LoopMode, r.LoopStart, r.LoopEnd = 1, 20000, 150000
			r.FcHz, r.QdB = 9000, 3
			regs = append(regs, r)
		}
		e.SetSource(i, &sampleSource{regions: regs}, "P", "x")
	}
	for k := 0; k < notes; k++ {
		e.NoteOn(36+k*3, 100, 1000+k)
	}
	return e
}

func BenchmarkSamplerRender128(b *testing.B) {
	e := sampEngine(8, 10, 2)
	out := make([]int16, 256)
	for i := 0; i < 50; i++ {
		e.Render(out)
	}
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		e.Render(out)
	}
	b.Logf("voix=%d", e.VoiceCount())
}

func BenchmarkSamplerNoteOn(b *testing.B) {
	e := sampEngine(8, 0, 2)
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		e.NoteOn(60+i%24, 100, 1000+i%24)
		e.NoteOff(1000 + i%24)
		if i%64 == 0 {
			e.Panic()
		}
	}
}

func TestSoftLimit(t *testing.T) {
	for _, x := range []float64{0, 0.1, 0.5, 0.7} {
		if softLimit(x) != x || softLimit(-x) != -x {
			t.Fatalf("le son faible doit passer intact : %v", x)
		}
	}
	prev := 0.7
	for x := 0.71; x < 20; x += 0.37 {
		y := softLimit(x)
		if y <= prev || y >= 1 || softLimit(-x) != -y {
			t.Fatalf("limiteur non monotone / dépasse 1 : %v -> %v", x, y)
		}
		prev = y
	}
	// continuité à la jonction
	if d := softLimit(0.7001) - 0.7001; d > 1e-4 || d < -1e-4 {
		t.Fatalf("saut à la jonction : %v", d)
	}
}
