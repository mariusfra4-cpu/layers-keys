package main

import (
	"os"
	"path/filepath"
	"testing"
)

func regionOf(u *UI, id string) (int, int) {
	for _, r := range u.regions {
		if r.id == id {
			return r.r.x + r.r.w/2, r.r.y + r.r.h/2
		}
	}
	return -1, -1
}

// clic droit sur un réglage, « Apprendre », puis un CC arrive
func learnVia(t *testing.T, u *UI, id string, cc int) {
	t.Helper()
	c := NewCanvas(WinW, WinH)
	u.Draw(c)
	x, y := regionOf(u, id)
	if x < 0 {
		t.Fatalf("réglage %s introuvable", id)
	}
	u.RightDown(x, y)
	if u.ov == nil || u.ov.title != T("MIDI Learn") {
		t.Fatalf("pas de menu MIDI Learn sur %s", id)
	}
	idx := -1
	for i, it := range u.ov.items {
		if !it.header && it.action != nil {
			idx = i
			break
		}
	}
	u.overlayChoose(idx)
	if u.learnTarget() != id {
		t.Fatalf("apprentissage non démarré : %q", u.learnTarget())
	}
	u.MidiMsg(0xB0 | uint32(cc)<<8 | 90<<16)
}

func TestMidiLearn(t *testing.T) {
	u, _, _ := newTestUI(t, "")
	learnVia(t, u, "vol0", 74)
	if u.learnTarget() != "" || u.learnCCOf("vol0") != 74 {
		t.Fatal("association non faite")
	}
	ch := u.eng.ch[0]
	u.MidiMsg(0xB0 | 74<<8 | 127<<16)
	if ch.Vol != 6 {
		t.Fatalf("CC 127 → volume %v (attendu +6 dB)", ch.Vol)
	}
	u.MidiMsg(0xB0 | 74<<8 | 0)
	if ch.Vol != -60 {
		t.Fatalf("CC 0 → volume %v", ch.Vol)
	}

	// un bouton : chaque appui bascule
	learnVia(t, u, "mute1", 20)
	m := u.eng.ch[1]
	u.MidiMsg(0xB0 | 20<<8 | 0) // on relâche le bouton qui a servi à apprendre
	u.MidiMsg(0xB0 | 20<<8 | 127<<16)
	u.MidiMsg(0xB0 | 20<<8 | 0)
	if !m.Mute {
		t.Fatal("le bouton aurait dû couper la piste 2")
	}
	u.MidiMsg(0xB0 | 20<<8 | 127<<16)
	if m.Mute {
		t.Fatal("le second appui aurait dû réactiver la piste 2")
	}

	// un CC déjà pris par le logiciel (volume, 7) est refusé ; l'apprentissage continue
	learnVia(t, u, "pan2", 7)
	if u.learnTarget() != "pan2" || u.learnCCOf("pan2") != -1 {
		t.Fatal("le CC 7 est réservé")
	}
	u.MidiMsg(0xB0 | 21<<8 | 127<<16)
	if u.learnCCOf("pan2") != 21 || u.eng.ch[2].Pan != 0 { // la première valeur sert seulement à apprendre
		t.Fatalf("pan2 : cc=%d pan=%v", u.learnCCOf("pan2"), u.eng.ch[2].Pan)
	}
	u.MidiMsg(0xB0 | 21<<8 | 127<<16)
	if u.eng.ch[2].Pan != 1 {
		t.Fatalf("pan = %v", u.eng.ch[2].Pan)
	}

	// un CC réassigné ailleurs quitte son ancien réglage
	learnVia(t, u, "master", 74)
	if u.learnCCOf("vol0") != -1 || u.learnCCOf("master") != 74 {
		t.Fatal("un CC ne commande qu'un seul réglage")
	}

	// Échap annule
	c := NewCanvas(WinW, WinH)
	u.Draw(c)
	u.learnStart("reverb")
	u.KeyDown(vkEscape, 0, false, false)
	if u.learnTarget() != "" {
		t.Fatal("Échap doit annuler l'apprentissage")
	}

	// les associations survivent à l'état du plugin et à la liste des Paramètres
	b := u.StateBytes()
	u2 := NewUI(NewEngine(), &fakeHost{}, "")
	u2.SetStateBytes(b)
	if u2.learnCCOf("master") != 74 || u2.learnCCOf("mute1") != 20 {
		t.Fatal("associations non restaurées dans le plugin")
	}
	u.OpenSettings()
	found := false
	for _, it := range u.ov.items {
		if it.action != nil && it.sub != "" && len(it.text) > 10 && it.text[:10] == "MIDI Learn" {
			found = true
		}
	}
	if !found {
		t.Fatal("pas d'entrée MIDI Learn dans les Paramètres")
	}
	u.openLearnList()
	u.Draw(c)
	n := len(u.learnMap())
	u.learnClear()
	if len(u.learnMap()) != 0 || n == 0 {
		t.Fatal("Tout effacer")
	}

	// pas dans les fichiers projet
	u.learnLoad([]LearnCfg{{30, "vol3"}, {7, "vol4"}, {31, "nimportequoi"}})
	if len(u.learnMap()) != 1 || u.learnCCOf("vol3") != 30 {
		t.Fatalf("vérification des lignes : %v", u.learnMap())
	}
	u.cfg.Learn = u.learnList()
	p := filepath.Join(t.TempDir(), "x.lkeys")
	if err := saveProject(p, u.cfg); err != nil {
		t.Fatal(err)
	}
	raw, _ := os.ReadFile(p)
	if string(raw) == "" || containsStr(string(raw), `"learn"`) {
		t.Fatal("les associations ne vont pas dans un projet")
	}
}

func containsStr(s, sub string) bool {
	for i := 0; i+len(sub) <= len(s); i++ {
		if s[i:i+len(sub)] == sub {
			return true
		}
	}
	return false
}
