package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestProjectsAndRename(t *testing.T) {
	dir := t.TempDir()
	u, h, e := newTestUI(t, filepath.Join(dir, "cfg", "config.json"))
	// pistes vides au départ
	for i := 0; i < nCh; i++ {
		if srcOf(e, i) != nil || u.labels[i] != "" {
			t.Fatal("la piste doit être vide au départ")
		}
	}
	if u.changed || !strings.HasPrefix(u.windowTitle(), "Sans titre") {
		t.Fatalf("titre : %q", u.windowTitle())
	}
	// charger un son de base, renommer la piste par double-clic sur son nom
	u.loadSource(1, SrcCfg{Kind: "builtin", Builtin: iPad})
	if !u.changed {
		t.Fatal("charger un son doit marquer le projet comme modifié")
	}
	drawUI(u)
	r := u.find("name1")
	x, y := centre(r.r)
	u.MouseDown(x, y)
	u.MouseUp(x, y)
	u.MouseDown(x, y)
	u.MouseUp(x, y)
	if u.ov == nil || !u.ov.input {
		t.Fatal("renommage de piste : fenêtre absente")
	}
	u.ov.text = ""
	for _, c := range "Nappe Gospel" {
		u.Char(c)
	}
	u.KeyDown(vkReturn, 0, false, false)
	if u.labels[1] != "Nappe Gospel" || u.trackTitle(1) != "Nappe Gospel" {
		t.Fatalf("nom de piste : %q", u.labels[1])
	}
	e.ch[1].Vol = -9
	// enregistrer sous
	h.save = filepath.Join(dir, "Culte")
	h.ctrl, h.shift = true, true
	u.KeyDown('S', 0, false, false)
	h.ctrl, h.shift = false, false
	p := filepath.Join(dir, "Culte.lkeys")
	if _, err := os.Stat(p); err != nil || u.projPath != p || u.changed {
		t.Fatalf("enregistrer sous : %v %q %v", err, u.projPath, u.changed)
	}
	u.Tick()
	if !strings.HasPrefix(h.title, "Culte  —") {
		t.Fatalf("titre : %q", h.title)
	}
	// modifier puis Ctrl+S
	e.ch[1].Vol = -3
	u.markDirty()
	if !u.changed {
		t.Fatal("modification non détectée")
	}
	h.ctrl = true
	u.KeyDown('S', 0, false, false)
	h.ctrl = false
	if u.changed {
		t.Fatal("Ctrl+S n'enregistre pas")
	}
	// nouveau projet : confirmation si modifié
	u.markDirty()
	u.NewProject()
	if u.ov == nil || len(u.ov.items) != 3 {
		t.Fatal("la confirmation avant de quitter le projet est absente")
	}
	u.overlayChoose(2) // Annuler
	if u.projPath != p {
		t.Fatal("annuler doit garder le projet")
	}
	u.NewProject()
	u.overlayChoose(1) // ne pas enregistrer
	if u.projPath != "" || srcOf(e, 1) != nil || u.labels[1] != "" || u.changed {
		t.Fatal("nouveau projet : pistes non vidées")
	}
	// ouvrir le projet enregistré
	u.openProjectFile(p)
	waitLoaded(t, u, 1)
	if nameOf(e, 1) != "Synth Pad" || u.labels[1] != "Nappe Gospel" || e.ch[1].Vol != -3 || u.changed || u.projPath != p {
		t.Fatalf("ouverture : %q %q vol=%.1f", nameOf(e, 1), u.labels[1], e.ch[1].Vol)
	}
	// ouvrir un fichier qui n'est pas un projet
	bad := filepath.Join(dir, "x.lkeys")
	os.WriteFile(bad, []byte("{\"a\":1}"), 0o644)
	u.openProjectFile(bad)
	if !strings.Contains(u.toast, "projet") {
		t.Fatalf("message : %q", u.toast)
	}
	// fermeture : rien à enregistrer -> ok ; modifié -> question
	if !u.RequestClose() {
		t.Fatal("fermeture refusée sans modification")
	}
	u.markDirty()
	if u.RequestClose() || u.ov == nil {
		t.Fatal("la fermeture doit demander confirmation")
	}
	u.overlayChoose(1)
	if h.quit != 1 {
		t.Fatal("quitter sans enregistrer ne ferme pas le logiciel")
	}
	// redémarrage : la session (piste, nom) est retrouvée
	u.Shutdown()
	u2, _, e2 := newTestUI(t, filepath.Join(dir, "cfg", "config.json"))
	waitLoaded(t, u2, 1)
	if u2.labels[1] != "Nappe Gospel" || nameOf(e2, 1) != "Synth Pad" {
		t.Fatalf("session non retrouvée : %q %q", u2.labels[1], nameOf(e2, 1))
	}
}

func TestSettingsLanguageAudioChords(t *testing.T) {
	dir := t.TempDir()
	cp := filepath.Join(dir, "config.json")
	u, h, e := newTestUI(t, cp)
	defer SetLang(langFR)
	u.StartAudio()
	if len(h.audio) != 1 || u.audioInfo != "test audio" {
		t.Fatal("l'audio n'est pas démarré")
	}
	// accords : Do majeur 7 au clavier PC / MIDI
	u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
	for _, n := range []int{60, 64, 67, 71} {
		u.MidiMsg(0x90 | uint32(n)<<8 | 100<<16)
	}
	drawUI(u)
	if u.chord.Name != "Do maj7" {
		t.Fatalf("accord affiché : %q", u.chord.Name)
	}
	for _, n := range []int{60, 64, 67, 71} {
		u.MidiMsg(0x80 | uint32(n)<<8)
	}
	drawUI(u)
	if u.chordLive || u.chord.Name != "Do maj7" {
		t.Fatal("le dernier accord doit rester affiché")
	}
	_ = e
	// paramètres : langue anglaise + notation lettres
	clickID(t, u, "settings")
	if u.ov == nil {
		t.Fatal("paramètres : fenêtre absente")
	}
	pick := func(sub string) {
		for k, it := range u.ov.items {
			if strings.Contains(it.text, sub) && !it.header {
				u.overlayChoose(k)
				return
			}
		}
		t.Fatalf("choix %q introuvable", sub)
	}
	pick("English")
	if CurLang() != langEN || u.cfg.Lang != "en" {
		t.Fatal("langue non changée")
	}
	if u.namer.Names[0] != "C" {
		t.Fatalf("en anglais, les accords s'écrivent par défaut avec des lettres : %q", u.namer.Names[0])
	}
	pick("Chord notation")
	if u.namer.Names[0] != "Do" {
		t.Fatalf("notation : %q", u.namer.Names[0])
	}
	pick("Chord notation")
	if u.namer.Names[0] != "C" {
		t.Fatalf("notation : %q", u.namer.Names[0])
	}
	c := NewCanvas(WinW, WinH)
	u.ov = nil
	u.Draw(c)
	found := map[string]bool{}
	for _, tx := range c.AllTexts {
		found[tx.S] = true
	}
	for _, w := range []string{"Settings", "New", "Open", "Save", "Empty track", "for church musician", "By JB Praise"} {
		if !found[w] {
			t.Errorf("texte anglais absent : %q", w)
		}
	}
	SetLang(langES)
	c = NewCanvas(WinW, WinH)
	u.Draw(c)
	found = map[string]bool{}
	for _, tx := range c.AllTexts {
		found[tx.S] = true
	}
	for _, w := range []string{"Ajustes", "Nuevo", "Abrir", "Guardar", "Pista vacía"} {
		if !found[w] {
			t.Errorf("texte espagnol absent : %q", w)
		}
	}
	SetLang(langFR)
	// audio : choisir ASIO, la carte, 48000 Hz, tampon 128, puis appliquer
	u.openAudioMenu()
	pick("Pilote")
	for k, it := range u.ov.items {
		if strings.Contains(it.text, "ASIO") {
			u.overlayChoose(k)
			break
		}
	}
	if u.cfg.Audio.API != apiASIO || u.cfg.Audio.Buffer != 128 {
		t.Fatalf("ASIO : %+v", u.cfg.Audio)
	}
	pick("Carte son")
	pick("Carte B")
	pick("Fréquence")
	pick("96000")
	pick("Appliquer")
	last := h.audio[len(h.audio)-1]
	if last.API != apiASIO || last.Device != "Carte B" || last.Rate != 96000 || last.Buffer != 128 {
		t.Fatalf("réglages audio envoyés : %+v", last)
	}
	// toutes les pages de réglages se dessinent
	drawUI(u)
	u.Shutdown()
	u2, _, _ := newTestUI(t, cp)
	if u2.cfg.Audio.Device != "Carte B" || u2.cfg.Lang != "fr" {
		t.Fatalf("réglages non retrouvés : %+v %q", u2.cfg.Audio, u2.cfg.Lang)
	}
	// plantage détecté : le drapeau reste levé -> retour à WASAPI
	u2.cfg.AudioPending = true
	u2.StartAudio()
	if h2 := u2.host.(*fakeHost); h2.audio[len(h2.audio)-1].API != apiAuto {
		t.Fatal("après un plantage, le pilote doit revenir au mode standard")
	}
}

func TestTrackBarToggle(t *testing.T) {
	u, _, e := newTestUI(t, "")
	u.loadSource(2, SrcCfg{Kind: "builtin", Builtin: iPad})
	if e.ch[2].Mute {
		t.Fatal("piste muette au départ")
	}
	clickID(t, u, "act2")
	if !e.ch[2].Mute || u.sel != 2 {
		t.Fatal("un clic sur la barre doit désactiver la piste")
	}
	clickID(t, u, "act2")
	if e.ch[2].Mute {
		t.Fatal("un second clic doit la réactiver")
	}
}
