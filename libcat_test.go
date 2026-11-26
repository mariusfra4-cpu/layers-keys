package main

import (
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
		if got := libCat(n, "", false, 0, 0, nil); got != c {
			t.Errorf("%q rangé dans %s (attendu %s)", n, catNames[got], catNames[c])
		}
	}
	// un SoundFont sans aucun indice ne va plus dans PIANO
	if got := libCat("Zorglub", "zz.sf2", true, 5, 3, nil); got != catOther {
		t.Errorf("son inconnu rangé dans %s (attendu AUTRES)", catNames[got])
	}
	// General MIDI : les leads (80-87) dans LEAD
	if got := libCat("Preset 81", "gm.sf2", true, 0, 81, nil); got != catLead {
		t.Errorf("programme GM 81 rangé dans %s (attendu LEAD)", catNames[got])
	}
}

// fenêtre des sons : seules les familles trouvées, 6 à l'écran, on fait glisser pour voir les autres
func TestLibrarySlide(t *testing.T) {
	u, _, _ := newTestUI(t, "")
	names := []string{"Concert Grand", "Yamaha C7", "Rhodes Suitcase", "Wurlitzer 200", "Hammond B3", "Church Organ",
		"Saw Lead", "Square Lead", "Supersaw", "Juno Pluck", "Trance Arp", "Warm Pad", "Worship Pad", "String Ensemble",
		"Alto Sax", "Brass Section", "Finger Bass", "Moog Bass", "Nylon Guitar", "Gospel Choir", "Music Box", "Drum Kit"}
	var ss []LibSound
	for _, n := range names {
		ss = append(ss, LibSound{Name: n, File: "/sons/" + n + ".wav", Kind: "audio", Cat: libCat(n, "", false, 0, 0, nil)})
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
