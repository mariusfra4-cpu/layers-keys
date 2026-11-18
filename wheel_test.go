package main

import (
	"os"
	"testing"
)

// la molette de modulation n'agit que sur les pistes dont le bouton MOLETTE est allumé
func TestWheelPerTrack(t *testing.T) {
	u, _, e := newTestUI(t, "")
	if !e.ch[0].Wheel || e.ch[1].Wheel {
		t.Fatalf("au départ : molette sur la piste 1 seulement (1=%v 2=%v)", e.ch[0].Wheel, e.ch[1].Wheel)
	}
	e.Control(1, 127)
	e.modS = 1
	if e.vibWheel(e.ch[0]) == 0 {
		t.Fatal("piste 1 : la molette devrait agir")
	}
	if v := e.vibWheel(e.ch[1]); v != 0 {
		t.Fatalf("piste 2 : la molette ne devrait pas agir (%v)", v)
	}
	e.ch[1].ModFX = true
	if m := e.openFor(&vbase{ch: e.ch[1]}); m != 0 {
		t.Fatalf("piste 2 avec MOD FX : la molette ne devrait pas ouvrir le son (%v)", m)
	}
	// le réglage est gardé dans les presets
	e.ch[1].Wheel = true
	s := u.snapshot()
	if s[1].WheelOff || !s[2].WheelOff {
		t.Fatal("réglage MOLETTE mal enregistré")
	}
	e.ch[1].Wheel = false
	u.applySetup(s)
	if !e.ch[1].Wheel {
		t.Fatal("réglage MOLETTE mal relu")
	}
	if dir := os.Getenv("LOOKSHOTS"); dir != "" {
		u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
		u.loadSource(1, SrcCfg{Kind: "builtin", Builtin: iPad})
		u.labels[0], u.labels[1] = "Grand Piano", "Nappe Gospel"
		e.ch[1].Wheel = false
		u.sel = 0
		shotPNG(t, dir, "molette_piste1_piano_allumee", u)
		u.sel = 1
		shotPNG(t, dir, "molette_piste2_pad_eteinte", u)
		e.ch[1].Wheel = true
		shotPNG(t, dir, "molette_piste2_pad_allumee", u)
	}
}

// orgue : sur la piste avec LESLIE, la molette règle la vitesse et ne fait pas de vibrato
func TestWheelLeslie(t *testing.T) {
	u, _, e := newTestUI(t, "")
	u.fxSetAt(2, func(c *FXCfg) { c.Leslie.On = true })
	e.ch[2].Wheel = true
	e.Control(1, 127)
	if !e.LeslieFast() {
		t.Fatal("molette en haut : le Leslie devrait être rapide")
	}
	e.modS = 1
	if v := e.vibWheel(e.ch[2]); v != 0 {
		t.Fatalf("piste Leslie : pas de vibrato attendu (%v)", v)
	}
	if e.vibWheel(e.ch[0]) == 0 {
		t.Fatal("piste 1 (molette allumée, sans Leslie) : la molette devrait garder son effet")
	}
	e.Control(1, 60)
	if !e.LeslieFast() {
		t.Fatal("au milieu : la vitesse ne doit pas changer (pas de va-et-vient)")
	}
	e.Control(1, 0)
	if e.LeslieFast() {
		t.Fatal("molette en bas : le Leslie devrait être lent")
	}
	// sans aucune piste Leslie, la molette ne change rien à la vitesse
	u.fxSetAt(2, func(c *FXCfg) { c.Leslie.On = false })
	e.Control(1, 127)
	if e.LeslieFast() {
		t.Fatal("sans Leslie, la molette ne doit pas changer la vitesse")
	}
	if dir := os.Getenv("LOOKSHOTS"); dir != "" {
		u.fxSetAt(2, func(c *FXCfg) { c.Leslie.On = true })
		u.loadSource(2, SrcCfg{Kind: "builtin", Builtin: iSquare})
		u.labels[2] = "Orgue Gospel"
		u.sel = 2
		e.Control(1, 0)
		shotPNG(t, dir, "leslie_lent", u)
		e.Control(1, 127)
		shotPNG(t, dir, "leslie_rapide", u)
	}
}
