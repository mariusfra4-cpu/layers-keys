package main

import (
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func mkFile(t *testing.T, p string, b []byte) string {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, b, 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestLibraryScanAndCategories(t *testing.T) {
	dir := t.TempDir()
	wav := makeWAV(440, 44100, 1, 16, false, 0.2, false, 69)
	mkFile(t, filepath.Join(dir, "Pianos", "Concert Grand.wav"), wav)
	mkFile(t, filepath.Join(dir, "Pianos", "Rhodes Mk1.wav"), wav)
	mkFile(t, filepath.Join(dir, "Hammond B3 full.wav"), wav)
	mkFile(t, filepath.Join(dir, "Synth", "Saw Lead.wav"), wav)
	mkFile(t, filepath.Join(dir, "Mes Pads", "Warm Worship.wav"), wav) // le dossier dit « pad »
	mkFile(t, filepath.Join(dir, "Strings Ensemble.wav"), wav)
	mkFile(t, filepath.Join(dir, "Divers", "bruit.wav"), wav)
	mkFile(t, filepath.Join(dir, "Basses", "Finger Bass.wav"), wav)
	mkFile(t, filepath.Join(dir, ".cache", "Piano cache.wav"), wav) // dossier caché : ignoré
	mkFile(t, filepath.Join(dir, "notes.txt"), []byte("pas un son"))
	mkFile(t, filepath.Join(dir, "Banques", "test.sf2"), testBank())
	mkFile(t, filepath.Join(dir, "abime.sf2"), []byte("RIFF....abîmé"))

	sounds, err := scanLibrary(dir, nil)
	if err != nil {
		t.Fatal(err)
	}
	cat := map[string]int{}
	for _, s := range sounds {
		cat[s.Name] = s.Cat
	}
	want := map[string]int{
		"Concert Grand": catPiano, "Rhodes Mk1": catPiano, "Hammond B3 full": catOrgan, "Saw Lead": catSynth,
		"Warm Worship": catPad, "Strings Ensemble": catStrings, "bruit": catOther, "Finger Bass": catBass,
		"TestA": catPiano, "Drums": catOther, // presets du SoundFont : numéro General MIDI, banque 128 = batterie
	}
	for n, c := range want {
		got, ok := cat[n]
		if !ok {
			t.Errorf("%q absent de la bibliothèque", n)
			continue
		}
		if got != c {
			t.Errorf("%q rangé dans %s (attendu %s)", n, catNames[got], catNames[c])
		}
	}
	if _, ok := cat["Piano cache"]; ok {
		t.Error("le dossier caché n'aurait pas dû être lu")
	}
	if len(sounds) != 8+8 {
		t.Errorf("%d sons trouvés (attendu 16 : 8 fichiers + 8 presets)", len(sounds))
	}
	// rangés par famille
	for i := 1; i < len(sounds); i++ {
		if sounds[i].Cat < sounds[i-1].Cat {
			t.Fatal("la liste n'est pas rangée par famille")
		}
	}
	// un son de la bibliothèque se charge dans une piste
	u, _, e := newTestUI(t, filepath.Join(t.TempDir(), "config.json"))
	for _, s := range sounds {
		if s.Name == "TestB" {
			u.loadSource(2, s.src())
			waitLoaded(t, u, 2)
			if nameOf(e, 2) != "TestB" {
				t.Fatalf("son de la bibliothèque non chargé : %q", nameOf(e, 2))
			}
		}
	}
}

func TestLibrarySavedAndShared(t *testing.T) {
	dir := t.TempDir()
	mkFile(t, filepath.Join(dir, "Pianos", "Grand.wav"), makeWAV(440, 44100, 1, 16, false, 0.2, false, 69))
	cp := filepath.Join(t.TempDir(), "config.json")
	u, h, _ := newTestUI(t, cp)
	h.open = dir
	u.libChooseDir()
	for i := 0; i < 200; i++ {
		if s, _ := u.libScanning(); !s {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if n := len(u.library().Sounds); n != 1 {
		t.Fatalf("%d sons après l'analyse", n)
	}
	// la fenêtre se dessine avec des sons
	u.openSoundMenu(0)
	drawUI(u)
	// relu au prochain démarrage (même dossier de réglages)
	u2, _, _ := newTestUI(t, cp)
	l := u2.library()
	if l.Dir != dir || len(l.Sounds) != 1 || l.Sounds[0].Cat != catPiano {
		t.Fatalf("bibliothèque non relue : %+v", l)
	}
	if !strings.HasSuffix(libraryPath(cp), "bibliotheque.json") {
		t.Fatal("chemin de la bibliothèque")
	}
}

func TestPadKeyFromName(t *testing.T) {
	cases := map[string]int{
		"C Major Pad.wav":         0,
		"C Pad.wav":               0,
		"C#.wav":                  1,
		"Db Major.mp3":            1,
		"D minor pad.wav":         14,
		"Dm.wav":                  14,
		"Eb.wav":                  3,
		"E_min_Pad.wav":           16,
		"F Warm Pad.wav":          5,
		"F#m.wav":                 18,
		"Gb Major.wav":            6,
		"G sharp minor.wav":       20,
		"Ab.wav":                  8,
		"A Major.wav":             9,
		"Am.wav":                  21,
		"Bb.wav":                  10,
		"Bbm Pad.wav":             22,
		"B Major.wav":             11,
		"Worship Pad - E.wav":     4,
		"Ambient Pad in G.wav":    7,
		"Pad La mineur.wav":       21,
		"Sib majeur.wav":          10,
		"Do.wav":                  0,
		"Fa# mineur.wav":          18,
		"Pad C Minor Ambient.wav": 12,
		"Warm Pad.wav":            -1, // aucune tonalité écrite
	}
	for name, want := range cases {
		if got := padKeyFromName(name); got != want {
			t.Errorf("%q → %d (attendu %d)", name, got, want)
		}
	}
	// un dossier « Minor » rend mineurs les fichiers sans précision
	if got := padKeyFromName("A.wav", "Minor Pads"); got != 21 {
		t.Errorf("dossier Minor : %d", got)
	}
}

func TestPadScanFolder(t *testing.T) {
	dir := t.TempDir()
	wav := makeWAV(220, 44100, 2, 16, false, 0.5, false, 57)
	mkFile(t, filepath.Join(dir, "C Major.wav"), wav)
	mkFile(t, filepath.Join(dir, "Am.wav"), wav)
	mkFile(t, filepath.Join(dir, "Mineur", "D.wav"), wav)
	mkFile(t, filepath.Join(dir, "lisez-moi.txt"), []byte("x"))
	files, n, err := scanPads(dir)
	if err != nil || n != 3 {
		t.Fatalf("scan des pads : %d, %v", n, err)
	}
	if files[0] == "" || files[21] == "" || files[14] == "" {
		t.Fatalf("pads mal rangés : %v", files)
	}
}

func TestPadPlayerCrossfadeAndLoop(t *testing.T) {
	// deux pads : un « do » (262 Hz) puis un « la » (440 Hz) ; fondu enchaîné, puis boucle sans coupure
	mk := func(f float64, secs float64) []float32 {
		n := int(secs * SR)
		d := make([]float32, 2*n)
		for i := 0; i < n; i++ {
			v := float32(0.5 * math.Sin(2*math.Pi*f*float64(i)/SR))
			d[2*i], d[2*i+1] = v, v
		}
		return d
	}
	p := newPadPlayer()
	p.SetVol(1)
	block := func() []float64 {
		l, r := make([]float64, 512), make([]float64, 512)
		p.mix(l, r)
		return l
	}
	p.start(mk(262, 3), SR)
	var rms []float64
	for i := 0; i < int(4*SR/512); i++ {
		rms = append(rms, fxRMS(block()))
	}
	if rms[0] > rms[len(rms)/2]*0.5 {
		t.Error("le pad doit entrer en fondu")
	}
	// après 4 s (fichier de 3 s) : la boucle continue sans trou
	for i := len(rms) / 2; i < len(rms); i++ {
		if rms[i] < 0.2 {
			t.Fatalf("trou dans le son à %d (boucle) : %.3f", i, rms[i])
		}
	}
	p.start(mk(440, 3), SR)
	if len(p.old) != 1 {
		t.Fatal("l'ancien pad doit disparaître en fondu, pas d'un coup")
	}
	for i := 0; i < int(3*SR/512); i++ {
		if v := fxRMS(block()); v < 0.15 {
			t.Fatalf("creux pendant le fondu enchaîné : %.3f", v)
		}
	}
	if len(p.old) != 0 {
		t.Error("l'ancien pad aurait dû finir de disparaître")
	}
	p.stop()
	for i := 0; i < int(3*SR/512); i++ {
		block()
	}
	if p.active() {
		t.Error("les pads devraient s'être tus après l'arrêt en fondu")
	}
	// licence : silence si la porte est fermée
	p.gate = func() bool { return false }
	p.start(mk(262, 1), SR)
	for i := 0; i < 50; i++ {
		if fxRMS(block()) != 0 {
			t.Fatal("les pads jouent sans licence")
		}
	}
}

func TestPadFollowChords(t *testing.T) {
	dir := t.TempDir()
	wav := makeWAV(220, 44100, 2, 16, false, 0.5, false, 57)
	mkFile(t, filepath.Join(dir, "C.wav"), wav)
	mkFile(t, filepath.Join(dir, "Am.wav"), wav)
	u, h, _ := newTestUI(t, filepath.Join(t.TempDir(), "config.json"))
	now := time.Date(2026, 1, 1, 12, 0, 0, 0, time.UTC)
	u.now = func() time.Time { return now }
	h.open = dir
	u.padChooseDir()
	if c := u.padCfg(); c.count() != 2 {
		t.Fatalf("%d pads", c.count())
	}
	u.padToggle() // allume : joue la tonalité choisie (do majeur)
	waitPad := func(key int) {
		t.Helper()
		for i := 0; i < 300 && u.padPlaying() != key; i++ {
			time.Sleep(5 * time.Millisecond)
		}
		if u.padPlaying() != key {
			t.Fatalf("pad attendu %d, joue %d", key, u.padPlaying())
		}
	}
	waitPad(0)
	// accord de la mineur tenu : le pad change après un court instant (pas tout de suite)
	for _, n := range []int{57, 60, 64} {
		u.counts[n] = 1
	}
	u.Tick()
	if u.padPlaying() != 0 {
		t.Fatal("le pad ne doit pas changer au premier instant (notes de passage)")
	}
	now = now.Add(500 * time.Millisecond)
	u.Tick()
	waitPad(21)
	if u.padCfg().Key != 21 {
		t.Fatal("la tonalité suivie doit être gardée")
	}
	// mode manuel : l'accord ne change plus rien
	for n := range u.counts {
		u.counts[n] = 0
	}
	u.Tick()
	u.padSet(func(p *PadCfg) { p.Manual = true })
	for _, n := range []int{48, 52, 55} {
		u.counts[n] = 1
	}
	u.Tick()
	now = now.Add(time.Second)
	u.Tick()
	time.Sleep(30 * time.Millisecond)
	if u.padPlaying() != 21 {
		t.Fatal("en mode manuel, le pad ne doit pas suivre les accords")
	}
	// bouton d'une tonalité
	u.padChoose(0)
	waitPad(0)
	// une tonalité absente du dossier : message, rien ne change
	u.padChoose(5)
	if u.padPlaying() != 0 {
		t.Fatal("tonalité absente : le pad aurait dû rester")
	}
	// fenêtre des pads et bouton du bas : dessin sans plantage
	u.OpenPads()
	drawUI(u)
	u.padToggle()
	if u.padCfg().On || u.padPlaying() != -1 {
		t.Fatal("les pads auraient dû s'éteindre")
	}
}

func TestPadsOffAtStartup(t *testing.T) {
	cp := filepath.Join(t.TempDir(), "config.json")
	u, _, _ := newTestUI(t, cp)
	u.padSet(func(p *PadCfg) { p.On, p.Vol, p.Key = true, 0.4, 7 })
	u2, _, e2 := newTestUI(t, cp)
	c := u2.padCfg()
	if c.On {
		t.Error("au démarrage, les pads doivent attendre qu'on les rallume")
	}
	if c.Vol != 0.4 || c.Key != 7 {
		t.Errorf("réglages des pads non gardés : %+v", c)
	}
	if e2.Pads.active() {
		t.Error("aucun pad ne doit jouer au démarrage")
	}
}
