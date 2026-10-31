package main

import (
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"testing"
)

func shotPNG(t *testing.T, dir, name string, u *UI) {
	c := NewCanvas(WinW, WinH)
	SharedTextRenderer().Attach(c)
	u.Draw(c)
	img := image.NewRGBA(image.Rect(0, 0, c.W, c.H))
	for i, p := range c.Pix {
		img.Set(i%c.W, i/c.W, color.RGBA{uint8(p >> 16), uint8(p >> 8), uint8(p), 255})
	}
	f, _ := os.Create(filepath.Join(dir, name+".png"))
	png.Encode(f, img)
	f.Close()
}

func TestVideoShots(t *testing.T) {
	dir := os.Getenv("VSHOTS")
	if dir == "" {
		t.Skip()
	}
	u, _, e := newTestUI(t, "")
	u.midiOK, u.midiStatus = true, "MIDI connecté : clavier"
	u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
	u.loadSource(1, SrcCfg{Kind: "builtin", Builtin: iPad})
	u.loadSource(2, SrcCfg{Kind: "builtin", Builtin: iPad})
	u.loadSource(3, SrcCfg{Kind: "builtin", Builtin: iLead})
	u.labels[0], u.labels[1], u.labels[2], u.labels[3] = "Grand Piano", "Nappe Gospel", "Strings", "Lead Worship"
	e.ch[0].Vol, e.ch[1].Vol, e.ch[2].Vol, e.ch[3].Vol = -2, -8, -6, -12
	for i, p := range []float64{0.12, 0.09, 0.07, 0.05} {
		e.ch[i].peak = p
	}
	e.ch[0].Pan, e.ch[3].Pan = -0.1, 0.3
	u.sel = 1
	for _, n := range []int{53, 57, 60, 64} { // F A C E : F Maj7
		u.counts[n] = 1
	}
	shotPNG(t, dir, "main_fmaj7", u)
	for _, n := range []int{53, 57, 60, 64} {
		u.counts[n] = 0
	}
	for _, n := range []int{52, 55, 59, 62, 66} { // Em9 / E
		u.counts[n] = 1
	}
	shotPNG(t, dir, "main_em", u)
	for n := range u.counts {
		u.counts[n] = 0
	}
	shotPNG(t, dir, "main_idle", u)
	u.openSoundMenu(0)
	shotPNG(t, dir, "menu", u)
	u.closeOverlay()
	u.OpenSettings()
	shotPNG(t, dir, "settings", u)
	u.lastVel = [2]int32{50, 61}
	u.OpenVelocity()
	shotPNG(t, dir, "velocity", u)
}
