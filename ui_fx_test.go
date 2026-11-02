package main

import (
	"math"
	"os"
	"testing"
)

func TestUIFXWindows(t *testing.T) {
	u, _, e := newTestUI(t, "")
	drawUI(u)
	if u.find("btnEQ").r.w == 0 || u.find("btnFX").r.w == 0 {
		t.Fatal("boutons EQ / EFFETS absents")
	}
	// ouvrir l'EQ par le bouton, l'allumer, tirer le point des basses
	r := u.find("btnEQ")
	u.MouseDown(r.r.x+10, r.r.y+10)
	u.MouseUp(0, 0)
	if u.ov == nil || u.ov.title != "Égaliseur" {
		t.Fatal("fenêtre EQ non ouverte")
	}
	drawUI(u)
	r = u.find("eqOn")
	u.MouseDown(r.r.x+10, r.r.y+10)
	u.MouseUp(0, 0)
	if !e.FX().EQ.On {
		t.Fatal("EQ non activé dans le moteur")
	}
	drawUI(u)
	p := u.find("eqpt0")
	u.MouseDown(p.r.x+12, p.r.y+12)
	u.MouseMove(p.r.x+12, p.r.y-30, true)
	u.MouseUp(0, 0)
	if g := e.FX().EQ.LS.G; g < 2 {
		t.Fatalf("gain des basses après glissé : %.1f", g)
	}
	// effets
	u.closeOverlay()
	u.OpenFXWin()
	drawUI(u)
	r = u.find("fxonCompresseur")
	u.MouseDown(r.r.x+10, r.r.y+8)
	u.MouseUp(0, 0)
	if !e.FX().Comp.On {
		t.Fatal("compresseur non activé")
	}
	// persistance : état → nouvelle interface
	st := u.StateBytes()
	u2, _, e2 := newTestUI(t, "")
	if !u2.SetStateBytes(st) {
		t.Fatal("rechargement de l'état")
	}
	f := e2.FX()
	if !f.EQ.On || !f.Comp.On || math.Abs(f.EQ.LS.G-e.FX().EQ.LS.G) > 1e-9 {
		t.Fatalf("effets non conservés : %+v", f.EQ.LS)
	}
	// un fichier abîmé ne doit rien casser
	u2.loadFX(&FXCfg{Delay: DelayCfg{On: true, Feedback: math.Inf(1), Time: -5}})
	if d := e2.FX().Delay; d.Feedback > 0.9 || d.Time < 20 {
		t.Fatalf("réglage non borné : %+v", d)
	}
	if dir := os.Getenv("VSHOTS"); dir != "" {
		u.closeOverlay()
		u.loadFX(nil)
		u.fxSet(func(c *FXCfg) {
			c.EQ.On, c.EQ.LS.G, c.EQ.P1.G, c.EQ.HS.G, c.EQ.P2.G = true, 6, -3, 4, 2
			c.Comp.On, c.Chorus.On = true, true
		})
		shotPNG(t, dir, "fx_main", u)
		u.OpenEQ()
		shotPNG(t, dir, "fx_eq", u)
		u.OpenFXWin()
		shotPNG(t, dir, "fx_fx", u)
	}
}
