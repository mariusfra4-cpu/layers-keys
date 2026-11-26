package main

import (
	"os"
	"testing"
)

// la molette de modulation n'agit que sur la piste sélectionnée
func TestWheelSelectedTrack(t *testing.T) {
	u, _, e := newTestUI(t, "")
	u.sel = 3 // par exemple le lead
	u.Tick()
	e.modS = 1
	if e.vibWheel(e.ch[3]) == 0 {
		t.Fatal("piste sélectionnée : la molette devrait agir")
	}
	e.ch[1].ModFX = true
	if e.vibWheel(e.ch[1]) != 0 || e.openFor(&vbase{ch: e.ch[1]}) != 0 {
		t.Fatal("autre piste (strings) : la molette ne doit pas la déranger")
	}
}

// orgue : sur la piste avec LESLIE, la molette règle la vitesse et ne fait pas de vibrato
func TestWheelLeslie(t *testing.T) {
	u, _, e := newTestUI(t, "")
	u.fxSetAt(2, func(c *FXCfg) { c.Leslie.On = true })
	u.sel = 2
	u.Tick()
	e.Control(1, 127)
	if !e.LeslieFast() {
		t.Fatal("molette en haut : le Leslie devrait être rapide")
	}
	e.modS = 1
	if v := e.vibWheel(e.ch[2]); v != 0 {
		t.Fatalf("piste Leslie : pas de vibrato attendu (%v)", v)
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

// réglage de la molette dans les Paramètres : toutes les pistes / désactivée / Leslie
func TestWheelSettings(t *testing.T) {
	u, _, e := newTestUI(t, "")
	u.sel = 0
	u.Tick()
	e.modS = 1
	u.setWheel(func(c *Config) { c.WheelMode = 1 })
	if e.vibWheel(e.ch[5]) == 0 {
		t.Fatal("toutes les pistes : la molette devrait agir partout")
	}
	u.setWheel(func(c *Config) { c.WheelMode = 2 })
	if e.vibWheel(e.ch[0]) != 0 {
		t.Fatal("désactivée : la molette ne devrait rien faire")
	}
	u.setWheel(func(c *Config) { c.WheelMode, c.WheelNoLes = 0, true })
	u.fxSetAt(0, func(c *FXCfg) { c.Leslie.On = true })
	e.Control(1, 127)
	if e.LeslieFast() {
		t.Fatal("Leslie à la télécommande seulement : la molette ne doit pas changer la vitesse")
	}
	if dir := os.Getenv("LOOKSHOTS"); dir != "" {
		u.setWheel(func(c *Config) { c.WheelMode, c.WheelNoLes = 0, false })
		u.fxSetAt(0, func(c *FXCfg) { c.Leslie.On = false })
		u.loadSource(3, SrcCfg{Kind: "builtin", Builtin: iLead})
		u.labels[3] = "Lead Worship"
		u.sel = 3
		e.Control(1, 90)
		u.Tick()
		u.OpenSettings()
		shotPNG(t, dir, "molette_parametres", u)
		u.OpenWheel()
		shotPNG(t, dir, "molette_page", u)
	}
}

// jouer le lead (piste sélectionnée) ne doit pas changer la vitesse du Leslie de l'orgue sur une autre piste
func TestWheelLeadDoesNotTouchOrgan(t *testing.T) {
	u, _, e := newTestUI(t, "")
	u.fxSetAt(2, func(c *FXCfg) { c.Leslie.On = true }) // orgue sur la piste 3
	u.sel = 3                                            // le lead
	u.Tick()
	e.Control(1, 127)
	if e.LeslieFast() {
		t.Fatal("la molette du lead a changé la vitesse du Leslie de l'orgue")
	}
	e.modS = 1
	if e.vibWheel(e.ch[3]) == 0 || e.vibWheel(e.ch[0]) != 0 || e.vibWheel(e.ch[2]) != 0 {
		t.Fatal("la molette doit agir sur le lead seulement")
	}
}
