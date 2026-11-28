package main

import (
	"fmt"
	"os"
	"testing"
)

// chaque son à sa place : les leads ne vont plus dans les pianos
func TestLibCatFamilies(t *testing.T) {
	want := map[string]int{
		"Supersaw":             catSynth,
		"Saw Lead":             catLead,
		"Lead 1 (square)":      catLead,
		"Juno Pluck":           catSynth,
		"Synth Strings":        catSynth,
		"Synth Pad Warm":       catPad,
		"Synth Bass 1":         catBass,
		"Moog Bass":            catBass,
		"Trance Arp":           catSynth,
		"Rhodes Mk1":           catEPiano,
		"Wurlitzer":            catEPiano,
		"DX7 E.Piano":          catEPiano,
		"JF Royal Grand 3D XL": catPiano,
		"Yamaha C7":            catPiano,
		"Salamander":           catPiano,
		"Hammond B3":           catOrgan,
		"Gospel Choir Aahs":    catChoir,
		"Nylon Guitar":         catGuitar,
		"Alto Sax":             catBrass,
		"Pan Flute":            catBrass,
		"Glockenspiel":         catBells,
		"Bass Drum":            catDrums,
		"Harp":                 catStrings,
		"Violin Ensemble":      catStrings,
	}
	for n, c := range want {
		if got := libCat(n, "", false, 0, 0, 1, nil); got != c {
			t.Errorf("%q rangé dans %s (attendu %s)", n, catNames[got], catNames[c])
		}
	}
	// un SoundFont sans aucun indice ne va plus dans PIANO
	if got := libCat("Zorglub", "zz.sf2", true, 5, 3, 1, nil); got != catOther {
		t.Errorf("son inconnu rangé dans %s (attendu AUTRES)", catNames[got])
	}
	// General MIDI : les leads (80-87) dans LEAD
	if got := libCat("Preset 81", "gm.sf2", true, 0, 81, 128, nil); got != catLead {
		t.Errorf("programme GM 81 rangé dans %s (attendu LEAD)", catNames[got])
	}
}

// fenêtre des sons : seules les familles trouvées, 6 à l'écran, on fait glisser pour voir les autres
func TestLibrarySlide(t *testing.T) {
	u, _, _ := newTestUI(t, "")
	names := []string{"Concert Grand", "Yamaha C7", "Rhodes Suitcase", "Wurlitzer 200", "Hammond B3", "Church Organ", "Percussive Organ", "Organ Perc B3",
		"Saw Lead", "Square Lead", "Supersaw", "Juno Pluck", "Trance Arp", "Warm Pad", "Worship Pad", "String Ensemble",
		"Alto Sax", "Brass Section", "Finger Bass", "Moog Bass", "Nylon Guitar", "Gospel Choir", "Music Box", "Drum Kit"}
	var ss []LibSound
	for _, n := range names {
		ss = append(ss, LibSound{Name: n, File: "/sons/" + n + ".wav", Kind: "audio", Cat: libCat(n, "", false, 0, 0, 1, nil)})
	}
	sortLib(ss)
	u.libs.mu.Lock()
	u.libs.lib = &Library{Version: libVersion, Dir: "/sons", Sounds: ss, Pads: defaultPadCfg()}
	u.libs.mu.Unlock()
	u.openSoundMenu(0)
	cv := NewCanvas(WinW, WinH)
	u.Draw(cv)
	if dir := os.Getenv("LOOKSHOTS"); dir != "" {
		shotPNG(t, dir, "sons_1", u)
	}
	var next *region
	for i := range u.regions {
		if u.regions[i].id == "libNext" {
			r := u.regions[i]
			next = &r
		}
	}
	if next == nil {
		t.Fatal("bouton ▶ absent : il y a plus de 6 familles")
	}
	for i := 0; i < 10; i++ {
		u.MouseDown(next.r.x+5, next.r.y+5)
		u.MouseUp(next.r.x+5, next.r.y+5)
		u.Draw(cv)
	}
	if u.libs.colOff == 0 {
		t.Fatal("les familles auraient dû glisser")
	}
	if dir := os.Getenv("LOOKSHOTS"); dir != "" {
		shotPNG(t, dir, "sons_2", u)
	}
}

// les problèmes vus chez le client : leads dans PIANO, orgue dans BATTERIE
func TestLibCatClientCases(t *testing.T) {
	cases := []struct {
		name, file string
		sf         bool
		bank, prog int
		presets    int
		dirs       []string
		want       int
	}{
		// SoundFont d'un seul son : son numéro 0 (« piano » en General MIDI) ne veut rien dire
		{"Nightmare", "/sons/Leads/Nightmare.sf2", true, 0, 0, 1, []string{"Leads"}, catLead},
		{"Preset 1", "/sons/Trance Lead.sf2", true, 0, 0, 1, nil, catLead},
		{"Init", "/sons/Synths/Bright Saw.sf2", true, 0, 0, 1, []string{"Synths"}, catSynth},
		{"Zorglub", "/sons/Zorglub.sf2", true, 0, 0, 1, nil, catOther},
		// orgue « percussif » : un orgue, pas une batterie
		{"Percussive Organ", "/sons/GM.sf2", true, 0, 17, 128, nil, catOrgan},
		{"Organ Perc B3", "/sons/B3.sf2", true, 0, 0, 1, nil, catOrgan},
		{"Perc Kit", "/sons/GM.sf2", true, 0, 0, 128, nil, catDrums},
		{"Steel Drums", "/sons/GM.sf2", true, 0, 114, 128, nil, catBells},
		// pack de leads (plusieurs sons, noms inventés, numéros 0, 1, 2…) : jamais dans PIANO
		{"Nebula", "/sons/Leads Collection.sf2", true, 0, 0, 24, nil, catLead},
		{"Arctic", "/sons/Leads Collection.sf2", true, 0, 1, 24, nil, catLead},
		{"Nebula", "/sons/Mes Leads/Pack 2.sf2", true, 0, 2, 24, []string{"Mes Leads"}, catLead},
		{"Nebula", "/sons/Pack 2.sf2", true, 0, 3, 24, nil, catOther},
		{"Ld Saw Bright", "/sons/Pack 2.sf2", true, 0, 0, 24, nil, catLead},
		{"Warm Pad", "/sons/Leads Collection.sf2", true, 0, 5, 24, nil, catPad}, // le nom du son reste prioritaire
		// grand SoundFont General MIDI : le numéro de programme compte
		{"Acoustic Grand Piano", "/sons/GM.sf2", true, 0, 0, 128, nil, catPiano},
		{"Calliope", "/sons/GM.sf2", true, 0, 82, 128, nil, catLead},
		{"Calliope", "/sons/FluidR3_GM.sf2", true, 0, 82, 40, nil, catLead},
	}
	for _, c := range cases {
		if got := libCat(c.name, c.file, c.sf, c.bank, c.prog, c.presets, c.dirs); got != c.want {
			t.Errorf("%q (%s) rangé dans %s (attendu %s)", c.name, c.file, catNames[got], catNames[c.want])
		}
	}
	if got := soundIcon("Percussive Organ", "", true, 0, 17); got != icOrgan {
		t.Errorf("icône de « Percussive Organ » : %d (attendu l'orgue)", got)
	}
}

// barres de défilement : on attrape celle d'une rangée de sons pour descendre ; images avec la souris dessus
func TestLibraryBars(t *testing.T) {
	u, _, _ := newTestUI(t, "")
	var ss []LibSound
	for i := 0; i < 40; i++ {
		n := fmt.Sprintf("Lead %02d", i+1)
		ss = append(ss, LibSound{Name: n, File: "/sons/" + n + ".wav", Kind: "audio", Cat: catLead})
	}
	for _, n := range []string{"Concert Grand", "Warm Pad", "Hammond B3", "Percussive Organ", "Drum Kit"} {
		ss = append(ss, LibSound{Name: n, File: "/sons/" + n + ".wav", Kind: "audio", Cat: libCat(n, "", false, 0, 0, 1, nil)})
	}
	sortLib(ss)
	u.libs.mu.Lock()
	u.libs.lib = &Library{Version: libVersion, Dir: "/sons", Sounds: ss, Pads: defaultPadCfg()}
	u.libs.mu.Unlock()
	u.openSoundMenu(0)
	cv := NewCanvas(WinW, WinH)
	u.Draw(cv)
	var bar *region
	for i := range u.regions {
		if u.regions[i].id == fmt.Sprintf("libbar%d", catLead) {
			r := u.regions[i]
			bar = &r
		}
	}
	if bar == nil {
		t.Fatal("la rangée LEAD (40 sons) devrait avoir une barre de défilement")
	}
	x := bar.r.x + bar.r.w/2
	u.MouseMove(x, bar.r.y+10, false)
	u.MouseDown(x, bar.r.y+10)
	u.MouseMove(x, bar.r.y+bar.r.h/2, true)
	if u.libs.scroll[catLead] == 0 {
		t.Fatal("en tirant la barre, la rangée LEAD devrait descendre")
	}
	if dir := os.Getenv("LOOKSHOTS"); dir != "" {
		shotPNG(t, dir, "barre_sons_souris_dessus", u)
	}
	u.MouseUp(x, bar.r.y+bar.r.h/2)
	if dir := os.Getenv("LOOKSHOTS"); dir != "" {
		u.closeOverlay()
		u.OpenSettings()
		u.Draw(cv)
		for i := range u.regions {
			if u.regions[i].id == "ovscroll" {
				r := u.regions[i].r
				u.MouseMove(r.x+r.w/2, r.y+60, false)
			}
		}
		shotPNG(t, dir, "barre_parametres_souris_dessus", u)
	}
}
