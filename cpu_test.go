package main

import (
	"math"
	"testing"
	"time"
)

func TestCPUProtection(t *testing.T) {
	e := NewEngine()
	if _, lim := e.Voices(); lim != maxVoice {
		t.Fatalf("limite de départ : %d", lim)
	}
	// calcul trop lent simulé : 3 ms pour un bloc qui dure 2,7 ms
	for i := 0; i < 30; i++ {
		e.measure(time.Now().Add(-3*time.Millisecond), 128)
	}
	if _, lim := e.Voices(); lim >= maxVoice || lim < 24 {
		t.Fatalf("la limite de notes ne baisse pas : %d", lim)
	}
	avg, _ := e.Load()
	if avg < 0.5 {
		t.Fatalf("charge mesurée : %.2f", avg)
	}
	// retour au calme : la limite remonte
	low := func() {
		for i := 0; i < 8000; i++ {
			e.measure(time.Now(), 128)
		}
	}
	low()
	if _, lim := e.Voices(); lim != maxVoice {
		t.Fatalf("la limite ne remonte pas : %d", lim)
	}
}

func hfEnergy(e *Engine, n int) float64 {
	out := make([]int16, 256)
	sum := 0.0
	for b := 0; b < n; b++ {
		e.Render(out)
		for i := 1; i < len(out)/2; i++ {
			d := float64(out[2*i]) - float64(out[2*(i-1)])
			sum += d * d
		}
	}
	return sum
}

func TestModFX(t *testing.T) {
	run := func(fx bool, mod float64) (hf float64, rev float64) {
		e := NewEngine()
		e.revAmt = 0
		e.SetSource(0, builtinSource{iSquare}, "Sq", "x")
		e.ch[0].ModFX = fx
		e.SetMod(mod)
		e.NoteOn(60, 100, 1)
		hf = hfEnergy(e, 40)
		e.NoteOff(1)
		e.AllNotesOff()
		e.Panic() // coupe les voix : seule la queue de réverbération peut encore sonner
		out := make([]int16, 256)
		e.Render(out)
		for _, v := range out {
			rev += math.Abs(float64(v))
		}
		return
	}
	hf0, _ := run(true, 0)
	hf1, r1 := run(true, 1)
	if hf1 < hf0*1.4 {
		t.Fatalf("la molette n'ouvre pas le son : %.0f -> %.0f", hf0, hf1)
	}
	if r1 <= 0 {
		t.Fatal("pas de réverbération envoyée avec la molette")
	}
}

func TestAftertouch(t *testing.T) {
	run := func(mode int, at int, off bool) float64 {
		e := NewEngine()
		e.revAmt = 0
		e.SetSource(0, builtinSource{iSquare}, "Sq", "x")
		e.ch[0].ModFX = true // le son est doux au repos : la pression l'ouvre
		e.ch[0].AT = !off
		e.SetAftertouchMode(mode, 1)
		e.NoteOn(60, 100, 1)
		e.SetAftertouch(at)
		return hfEnergy(e, 40)
	}
	rest := run(0, 0, false)
	pressed := run(0, 127, false)
	if pressed < rest*1.4 {
		t.Fatalf("l'aftertouch n'ouvre pas le son : %.0f -> %.0f", rest, pressed)
	}
	mid := (rest + pressed) / 2
	if v := run(0, 127, true); v > mid {
		t.Fatalf("aftertouch actif alors qu'il est coupé sur la piste : %.0f (repos %.0f)", v, rest)
	}
	if v := run(3, 127, false); v > mid {
		t.Fatalf("aftertouch actif alors qu'il est désactivé : %.0f", v)
	}
	// par touche : seule la note pressée change
	e := NewEngine()
	e.SetSource(0, builtinSource{iSquare}, "Sq", "x")
	e.NoteOn(60, 100, 1)
	e.NoteOn(64, 100, 2)
	e.PolyPressure(1, 100)
	var p1, p2 float64
	for _, v := range e.voices {
		if b := v.base(); b.tag == 1 {
			p1 = b.press
		} else {
			p2 = b.press
		}
	}
	if p1 < 0.7 || p2 != 0 {
		t.Fatalf("pression par touche : %.2f / %.2f", p1, p2)
	}
}

func TestVelocityCurve(t *testing.T) {
	e := NewEngine()
	if e.MapVel(64) != 64 {
		t.Fatal("sans courbe, la vélocité doit rester intacte")
	}
	e.SetVelCurve(1, 1, 127)
	if e.MapVel(64) < 63 || e.MapVel(64) > 65 || e.MapVel(127) != 127 || e.MapVel(1) != 1 {
		t.Fatalf("courbe normale : %d %d %d", e.MapVel(1), e.MapVel(64), e.MapVel(127))
	}
	e.SetVelCurve(0.6, 1, 127)
	soft := e.MapVel(40)
	e.SetVelCurve(1.8, 1, 127)
	hard := e.MapVel(40)
	if !(soft > 40 && hard < 40) {
		t.Fatalf("douce %d / dure %d autour de 40", soft, hard)
	}
	e.SetVelCurve(1, 100, 100)
	if e.MapVel(5) != 100 || e.MapVel(127) != 100 {
		t.Fatal("vélocité fixe")
	}
	// de bout en bout : un message MIDI traverse la courbe
	u := NewUI(NewEngine(), &fakeHost{}, "")
	u.eng.SetSource(0, builtinSource{iGrand}, "P", "x")
	u.cfg.VelGamma, u.cfg.VelLo, u.cfg.VelHi = 1, 100, 100
	u.applyPlaying()
	u.MidiFast(0x90 | 60<<8 | 20<<16)
	if u.lastVel[0] != 20 || u.lastVel[1] != 100 {
		t.Fatalf("frappe %d -> %d", u.lastVel[0], u.lastVel[1])
	}
	u.MidiFast(0xD0 | 90<<8) // aftertouch de canal : accepté sans erreur
	if u.eng.at < 0.7 {
		t.Fatal("aftertouch de canal non reçu")
	}
}

func TestSettingsBackAndVelocity(t *testing.T) {
	u, h, _ := newTestUI(t, "")
	u.OpenSettings()
	u.openAudioMenu()
	if u.ov == nil || u.ov.back == nil {
		t.Fatal("pas de bouton Retour dans le menu audio")
	}
	u.ov.back()
	if u.ov == nil || u.ov.title != T("Paramètres de Layers Keys") {
		t.Fatal("Retour doit revenir aux Paramètres")
	}
	u.OpenVelocity()
	c := NewCanvas(WinW, WinH)
	u.Draw(c)
	u.MouseDown(500, 300) // dans la case de la courbe : la modifie
	u.MouseUp(500, 300)
	_ = h
	u.ov.back()
	if u.ov == nil || u.ov.custom != nil {
		t.Fatal("Retour depuis la courbe")
	}
	// la courbe est sauvegardée dans le plugin
	b := u.StateBytes()
	u2 := NewUI(NewEngine(), &fakeHost{}, "")
	u2.SetStateBytes(b)
	if u2.cfg.VelGamma != u.cfg.VelGamma {
		t.Fatal("courbe de vélocité non restaurée")
	}
}
