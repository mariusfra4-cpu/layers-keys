package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

// voix vivantes de chaque piste
func voicesPerTrack(e *Engine) [nCh]int {
	var n [nCh]int
	e.mu.Lock()
	defer e.mu.Unlock()
	for _, v := range e.voices {
		if b := v.base(); !b.dead && !b.released {
			n[b.ch.idx]++
		}
	}
	return n
}

func TestSplitBassLeftPianoRight(t *testing.T) {
	u, _, e := newTestUI(t, "")
	u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iBass})
	u.loadSource(1, SrcCfg{Kind: "builtin", Builtin: iGrand})
	u.loadSource(2, SrcCfg{Kind: "builtin", Builtin: iPad})
	waitLoaded(t, u, 0)
	waitLoaded(t, u, 1)
	waitLoaded(t, u, 2)
	u.setZone(0, zoneLeft)
	u.setZone(1, zoneRight)
	// piste 3 : TOUT (joue des deux côtés)
	if u.splitNote() != defaultSplit {
		t.Fatalf("point de split par défaut : %d", u.splitNote())
	}
	e.NoteOn(48, 100, 1) // do grave : basse + pad, pas le piano
	n := voicesPerTrack(e)
	if n[0] == 0 || n[1] != 0 || n[2] == 0 {
		t.Fatalf("note à gauche : %v", n)
	}
	e.NoteOff(1)
	e.Panic()
	e.NoteOn(64, 100, 2) // mi aigu : piano + pad, pas la basse
	n = voicesPerTrack(e)
	if n[0] != 0 || n[1] == 0 || n[2] == 0 {
		t.Fatalf("note à droite : %v", n)
	}
	e.Panic()
	// la note du split elle-même appartient à la zone de droite
	e.NoteOn(60, 100, 3)
	if n = voicesPerTrack(e); n[0] != 0 || n[1] == 0 {
		t.Fatalf("note du split : %v", n)
	}
	e.Panic()
	// l'octave de la piste ne change pas sa zone (la zone suit les touches jouées)
	u.with(func() { e.ch[0].Oct = -1 })
	e.NoteOn(59, 100, 4)
	if n = voicesPerTrack(e); n[0] == 0 || n[1] != 0 {
		t.Fatalf("basse à l'octave -1 : %v", n)
	}
	e.Panic()
}

func TestSplitLearnByPlaying(t *testing.T) {
	u, _, e := newTestUI(t, "")
	u.setZone(0, zoneLeft)
	u.setZone(1, zoneRight)
	u.splitLearnStart()
	if !u.splitLearning() {
		t.Fatal("mode « joue la note » attendu")
	}
	u.MidiFast(0x90 | 55<<8 | 100<<16) // sol joué sur le clavier MIDI (fil MIDI)
	u.MidiFast(0x80 | 55<<8)
	u.Tick()
	if u.splitNote() != 55 || e.ch[0].KeyHi != 54 || e.ch[1].KeyLo != 55 {
		t.Fatalf("split après la note : %d (gauche ..%d, droite %d..)", u.splitNote(), e.ch[0].KeyHi, e.ch[1].KeyLo)
	}
	if u.splitLearning() {
		t.Fatal("le mode « joue la note » aurait dû s'arrêter")
	}
	// une autre note ne change plus rien
	u.MidiFast(0x90 | 70<<8 | 100<<16)
	u.Tick()
	if u.splitNote() != 55 {
		t.Fatal("le split a bougé sans qu'on le demande")
	}
	// avec le clavier à l'écran aussi
	u.splitLearnStart()
	u.press(99, 50, 100)
	u.Tick()
	if u.splitNote() != 50 {
		t.Fatalf("split par le clavier à l'écran : %d", u.splitNote())
	}
	// zone qui revient à TOUT
	u.cycleZone(1) // DROITE → TOUT
	if chanZone(e.ch[1].KeyLo, e.ch[1].KeyHi) != zoneAll {
		t.Fatal("la piste 2 devrait jouer sur tout le clavier")
	}
	drawUI(u) // panneau et repère du split : dessin sans plantage
}

func TestSplitSavedInPresetsProjectsAndPlugin(t *testing.T) {
	dir := t.TempDir()
	u, h, e := newTestUI(t, filepath.Join(dir, "config.json"))
	u.setZone(0, zoneLeft)
	u.setZone(1, zoneRight)
	u.setSplit(53)
	u.savePreset()
	// le preset garde le split
	u.setZone(0, zoneAll)
	u.setZone(1, zoneAll)
	u.applyPreset(u.cfg.Cur)
	if e.ch[0].KeyHi != 52 || e.ch[1].KeyLo != 53 {
		t.Fatalf("preset : gauche ..%d, droite %d..", e.ch[0].KeyHi, e.ch[1].KeyLo)
	}
	// projet
	h.save = filepath.Join(dir, "chant")
	u.SaveProjectAs()
	c, err := loadProject(h.save + projectExt)
	if err != nil {
		t.Fatal(err)
	}
	if lo, hi := zoneRange(c.Setup[0].Zone); lo != 0 || hi != 52 {
		t.Fatalf("projet : zone de la piste 1 = %d..%d", lo, hi)
	}
	if lo, hi := zoneRange(c.Setup[2].Zone); lo != 0 || hi != 127 || c.Setup[2].Zone != [2]int{} {
		t.Fatalf("projet : piste 3 devrait être sur tout le clavier (%d..%d)", lo, hi)
	}
	// état du plugin (DAW)
	b := u.StateBytes()
	u2, _, e2 := newTestUI(t, "")
	if !u2.SetStateBytes(b) || e2.ch[0].KeyHi != 52 || e2.ch[1].KeyLo != 53 {
		t.Fatal("le split n'est pas gardé dans le projet du DAW")
	}
	// un ancien fichier (sans zone) joue sur tout le clavier
	var old ChanCfg
	_ = json.Unmarshal([]byte(`{"vol":0,"pan":0,"oct":0,"trans":0,"p":[1,0,0,0,0,0.5,0,0.45]}`), &old)
	old.sanitize()
	if lo, hi := zoneRange(old.Zone); lo != 0 || hi != 127 {
		t.Fatalf("ancien fichier : %d..%d", lo, hi)
	}
	// valeur abîmée : tout le clavier
	bad := ChanCfg{Zone: [2]int{90, 10}}
	bad.sanitize()
	if bad.Zone != [2]int{} {
		t.Fatalf("zone abîmée non corrigée : %v", bad.Zone)
	}
}

func TestLibraryMigratesToBassColumn(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "bibliotheque.json")
	old := `{"version":1,"dir":"/sons","sounds":[{"n":"Finger Bass","f":"/sons/Basses/Finger Bass.wav","k":"audio","c":5},{"n":"Grand","f":"/sons/Grand.wav","k":"audio","c":0}],"pads":{"vol":0.5}}`
	if err := os.WriteFile(p, []byte(old), 0o644); err != nil {
		t.Fatal(err)
	}
	l := loadLibrary(p)
	if l.Version != libVersion || l.Dir != "/sons" || len(l.Sounds) != 2 {
		t.Fatalf("ancienne liste mal relue : %+v", l)
	}
	cats := map[string]int{}
	for _, s := range l.Sounds {
		cats[s.Name] = s.Cat
	}
	if cats["Finger Bass"] != catBass || cats["Grand"] != catPiano {
		t.Fatalf("sons mal rangés après la mise à jour : %v", cats)
	}
	if l.Pads.Vol != 0.5 {
		t.Fatal("réglages des pads perdus")
	}
}
