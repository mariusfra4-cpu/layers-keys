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
