package main

import (
	"math"
	"os"
	"path/filepath"
	"testing"
)

func TestPlayer(t *testing.T) {
	// WAV mono 16 bits, 8 000 Hz, 1 s de sinus
	n := 8000
	b := make([]byte, 44+2*n)
	copy(b, "RIFF")
	le := func(o int, v uint32, w int) {
		for i := 0; i < w; i++ {
			b[o+i] = byte(v >> (8 * i))
		}
	}
	le(4, uint32(36+2*n), 4)
	copy(b[8:], "WAVEfmt ")
	le(16, 16, 4)
	le(20, 1, 2)
	le(22, 1, 2)
	le(24, 8000, 4)
	le(28, 16000, 4)
	le(32, 2, 2)
	le(34, 16, 2)
	copy(b[36:], "data")
	le(40, uint32(2*n), 4)
	for i := 0; i < n; i++ {
		le(44+2*i, uint32(uint16(int16(12000*math.Sin(float64(i)*0.3)))), 2)
	}
	p := filepath.Join(t.TempDir(), "t.wav")
	os.WriteFile(p, b, 0o644)
	e := NewEngine()
	if err := e.Player.Load(p); err != nil {
		t.Fatal(err)
	}
	out := make([]int16, 2*512)
	e.Render(out)
	for _, v := range out {
		if v != 0 {
			t.Fatal("le lecteur arrêté fait du bruit")
		}
	}
	e.Player.Play()
	peak := 0
	for k := 0; k < 8; k++ {
		e.Render(out)
		for _, v := range out {
			if int(v) > peak {
				peak = int(v)
			}
		}
	}
	if peak < 500 {
		t.Fatalf("pas de son du lecteur (%d)", peak)
	}
	cur, tot := e.Player.Times()
	if cur <= 0 || math.Abs(tot-1) > 0.01 {
		t.Fatalf("temps %v / %v", cur, tot)
	}
	e.Player.Seek(0.5)
	if c, _ := e.Player.Times(); math.Abs(c-0.5) > 0.01 {
		t.Fatal("seek")
	}
	for k := 0; k < 100 && e.Player.Playing(); k++ {
		e.Render(out)
	}
	if e.Player.Playing() {
		t.Fatal("la lecture ne s'arrête pas à la fin")
	}
}

func TestStateRoundTrip(t *testing.T) {
	h := &fakeHost{}
	u := NewUI(NewEngine(), h, "")
	u.Plugin = true
	u.eng.SetMaster(0.55)
	u.with(func() { u.eng.ch[1].Vol = -7; u.eng.ch[1].ModFX = true })
	u.labels[1] = "Nappe test"
	b := u.StateBytes()
	u2 := NewUI(NewEngine(), h, "")
	if !u2.SetStateBytes(b) {
		t.Fatal("état refusé")
	}
	if math.Abs(u2.eng.master-0.55) > 1e-9 || u2.eng.ch[1].Vol != -7 || !u2.eng.ch[1].ModFX || u2.labels[1] != "Nappe test" {
		t.Fatal("état mal restauré")
	}
	if u2.SetStateBytes([]byte("n'importe quoi")) {
		t.Fatal("état invalide accepté")
	}
}

func TestRenderFloat(t *testing.T) {
	e := eng(0)
	e.NoteOn(60, 100, 1)
	l, r := make([]float32, 512), make([]float32, 512)
	peak := float32(0)
	for k := 0; k < 6; k++ {
		e.RenderF(l, r)
		for i := range l {
			if l[i] > peak {
				peak = l[i]
			}
			if l[i] > 1 || l[i] < -1 {
				t.Fatal("sortie hors limites")
			}
		}
	}
	if peak < 0.01 {
		t.Fatalf("pas de son en sortie flottante (%v)", peak)
	}
}
