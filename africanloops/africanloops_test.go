package africanloops

import (
	"math"
	"math/rand"
	"os"
	"path/filepath"
	"testing"
)

const testRate = 44100

type voice func(t float64) float64

func kickV(t float64) float64 {
	f := 50 + 70*math.Exp(-t/0.03)
	ph := 2 * math.Pi * (50*t + 70*0.03*(1-math.Exp(-t/0.03)))
	_ = f
	return 0.8 * math.Sin(ph) * math.Exp(-t/0.12)
}

func congaV(t float64) float64 {
	return 0.5 * (math.Sin(2*math.Pi*330*t) + 0.3*math.Sin(2*math.Pi*660*t)) * math.Exp(-t/0.09)
}

func talkingV(t float64) float64 {
	// note qui monte de 160 à 260 Hz
	ph := 2 * math.Pi * (160*t + 100*t*t/(2*0.25))
	if t > 0.25 {
		ph = 2*math.Pi*(160*0.25+100*0.25/2) + 2*math.Pi*260*(t-0.25)
	}
	return 0.55 * math.Sin(ph) * math.Exp(-t/0.2)
}

// pattern : temps (en doubles-croches) de chaque instrument, par mesure
var pattern = map[string][]int{
	"kick":  {0, 8, 10},
	"snare": {4, 12},
	"hat":   {0, 2, 4, 6, 8, 10, 12, 14},
	"conga": {3, 7, 11},
	"talk":  {13},
}

// synthLoop fabrique une loop de batterie : bpm, nombre de mesures, silence ajouté à la fin.
func synthLoop(bpm float64, bars int, tail float64) *Audio {
	rng := rand.New(rand.NewSource(1))
	sixteenth := 60 / bpm / 4
	total := int(math.Round(float64(bars*16)*sixteenth*testRate)) + int(tail*testRate)
	a := &Audio{Rate: testRate, L: make([]float32, total), R: make([]float32, total)}
	add := func(start int, dur float64, gain float64, f func(i int, t float64) float64) {
		n := int(dur * testRate)
		for i := 0; i < n && start+i < total; i++ {
			v := float32(gain * f(i, float64(i)/testRate))
			a.L[start+i] += v
			a.R[start+i] += v
		}
	}
	for bar := 0; bar < bars; bar++ {
		for name, steps := range pattern {
			for _, s := range steps {
				start := int(math.Round(float64(bar*16+s) * sixteenth * testRate))
				switch name {
				case "kick":
					add(start, 0.5, 1, func(_ int, t float64) float64 { return kickV(t) })
				case "snare":
					add(start, 0.3, 1, func(_ int, t float64) float64 {
						return 0.45*(rng.Float64()*2-1)*math.Exp(-t/0.06) + 0.35*math.Sin(2*math.Pi*190*t)*math.Exp(-t/0.05)
					})
				case "hat":
					var p1, p2 float64
					add(start, 0.12, 1, func(_ int, t float64) float64 {
						x := rng.Float64()*2 - 1
						h := x - 2*p1 + p2 // passe-haut : ne garde que les aigus
						p2, p1 = p1, x
						return 0.12 * h * math.Exp(-t/0.025)
					})
				case "conga":
					add(start, 0.5, 1, func(_ int, t float64) float64 { return congaV(t) })
				case "talk":
					add(start, 0.8, 1, func(_ int, t float64) float64 { return talkingV(t) })
				}
			}
		}
	}
	return a
}

func TestTempoFromLength(t *testing.T) {
	a := synthLoop(96, 4, 0)
	info := Analyze(a, "Worship_Groove.wav")
	if math.Abs(info.Tempo.BPM-96) > 0.01 || info.Tempo.Bars != 4 || info.Tempo.Source != TempoFromLength {
		t.Fatalf("tempo = %+v, attendu 96 BPM, 4 mesures, source durée", info.Tempo)
	}
}

func TestTempoVariousLoops(t *testing.T) {
	for _, bpm := range []float64{72, 88, 100, 118, 124, 140} {
		a := synthLoop(bpm, 2, 0)
		info := Analyze(a, "loop.wav")
		if math.Abs(info.Tempo.BPM-bpm) > 0.01 {
			t.Errorf("%v BPM : trouvé %+v", bpm, info.Tempo)
		}
	}
}

func TestTempoAnalysisOnly(t *testing.T) {
	// loop mal coupée (bout de silence en trop) : seule l'analyse des coups marche
	a := synthLoop(110, 4, 0.37)
	info := Analyze(a, "loop.wav")
	if math.Abs(info.Tempo.BPM-110) > 1 {
		t.Fatalf("tempo = %+v, attendu ≈ 110", info.Tempo)
	}
}

func TestTempoFromFilename(t *testing.T) {
	cases := []struct {
		name   string
		bpm    float64
		strong bool
	}{
		{"Worship_96bpm.wav", 96, true},
		{"Love Groove 87 BPM Am.wav", 87, true},
		{"BPM120_afro.wav", 120, true},
		{"Coupe_Decale_118.5bpm.mp3", 118.5, true},
		{"Worship_96_Am.wav", 96, false},
		{"Kit 2024 v3.wav", 0, false},
		{"Djembe_Solo.wav", 0, false},
	}
	for _, c := range cases {
		b, s := TempoFromFilename(c.name)
		if b != c.bpm || s != c.strong {
			t.Errorf("%s : %v %v, attendu %v %v", c.name, b, s, c.bpm, c.strong)
		}
	}
}

func TestTempoFromFileAndWAV(t *testing.T) {
	a := synthLoop(100, 1, 0)
	b, err := ParseWAV(EncodeWAV(a, 87.5, 4))
	if err != nil {
		t.Fatal(err)
	}
	if b.MetaBPM != 87.5 || b.MetaBeats != 4 || b.Frames() != a.Frames() || b.Rate != testRate {
		t.Fatalf("relecture : bpm %v temps %v images %v", b.MetaBPM, b.MetaBeats, b.Frames())
	}
	for i := 0; i < a.Frames(); i += 997 {
		if math.Abs(float64(a.L[i]-b.L[i])) > 1e-6 && math.Abs(float64(a.L[i])) < 1 {
			t.Fatalf("échantillon %d : %v ≠ %v", i, a.L[i], b.L[i])
		}
	}
	if info := Analyze(b, "x.wav"); info.Tempo.BPM != 87.5 || info.Tempo.Source != TempoFromFile {
		t.Fatalf("tempo du fichier ignoré : %+v", info.Tempo)
	}
}

func TestPercussionsDetected(t *testing.T) {
	info := Analyze(synthLoop(96, 4, 0), "x.wav")
	got := map[string]bool{}
	for _, p := range info.Percussions {
		got[p.Kind] = true
	}
	for _, k := range PercOrder {
		if !got[k] {
			t.Errorf("%s non reconnu ; trouvé %+v", k, info.Percussions)
		}
	}
}

func TestPercussionsSubset(t *testing.T) {
	// une loop sans talking drum ni conga ne doit pas en inventer
	saved := map[string][]int{}
	for k, v := range pattern {
		saved[k] = v
	}
	defer func() { pattern = saved }()
	delete(pattern, "talk")
	delete(pattern, "conga")
	info := Analyze(synthLoop(100, 4, 0), "x.wav")
	for _, p := range info.Percussions {
		if p.Kind == PercTalking || p.Kind == PercHand {
			t.Errorf("percussion inventée : %+v", info.Percussions)
		}
	}
}

func energy(x []float32, from, to int) float64 {
	s := 0.0
	for i := from; i < to && i < len(x); i++ {
		if i >= 0 {
			s += float64(x[i]) * float64(x[i])
		}
	}
	return s
}

func TestSeparateSumsToOriginal(t *testing.T) {
	a := synthLoop(96, 2, 0)
	info, stems := Separate(a, "x.wav")
	if len(stems) < 4 {
		t.Fatalf("seulement %d pistes : %+v", len(stems), info.Percussions)
	}
	maxErr := 0.0
	for i := 0; i < a.Frames(); i++ {
		var s float32
		for _, st := range stems {
			s += st.Audio.L[i]
		}
		maxErr = math.Max(maxErr, math.Abs(float64(s-a.L[i])))
	}
	if maxErr > 1e-4 {
		t.Fatalf("la somme des pistes s'écarte de l'original : %g", maxErr)
	}
	// chaque piste doit surtout contenir son instrument
	sixteenth := 60.0 / 96 / 4 * testRate
	at := func(step int) (int, int) {
		s := int(float64(step) * sixteenth)
		return s, s + int(0.05*testRate)
	}
	byKind := map[string]*Audio{}
	for _, st := range stems {
		byKind[st.Kind] = st.Audio
	}
	check := func(kind string, own, other int) {
		x := byKind[kind]
		if x == nil {
			t.Errorf("piste %s absente", kind)
			return
		}
		o0, o1 := at(own)
		p0, p1 := at(other)
		if eo, ep := energy(x.L, o0, o1), energy(x.L, p0, p1); eo < 4*ep {
			t.Errorf("piste %s : énergie sur son coup %.3g, sur un autre coup %.3g", kind, eo, ep)
		}
	}
	check(PercKick, 8, 4)     // kick (temps 3) vs moment de la caisse claire
	check(PercSnare, 4, 3)    // caisse claire vs conga
	check(PercHand, 3, 4)     // conga vs caisse claire
	check(PercTalking, 13, 4) // talking drum vs caisse claire
}

func TestLibraryScan(t *testing.T) {
	dir := t.TempDir()
	write := func(rel string, bpm float64) {
		p := filepath.Join(dir, filepath.FromSlash(rel))
		os.MkdirAll(filepath.Dir(p), 0o755)
		if err := os.WriteFile(p, EncodeWAV(synthLoop(bpm, 1, 0), 0, 0), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("Worship/Praise Drums.wav", 96)
	write("I Love/Soft 01.wav", 80)
	write("love_groove_90bpm.wav", 90)
	write("Djembe solo.wav", 100)
	os.WriteFile(filepath.Join(dir, "notes.txt"), []byte("x"), 0o644)

	calls := 0
	lib, err := Scan(dir, func(done, total int, cur string) { calls++ })
	if err != nil {
		t.Fatal(err)
	}
	if len(lib.Entries) != 4 || calls != 4 {
		t.Fatalf("%d loops, %d analyses", len(lib.Entries), calls)
	}
	want := map[string]string{
		"Worship/Praise Drums.wav": "Worship",
		"I Love/Soft 01.wav":       "I Love",
		"love_groove_90bpm.wav":    "Love",
		"Djembe solo.wav":          Uncategorized,
	}
	for _, e := range lib.Entries {
		if want[e.Path] != e.Category {
			t.Errorf("%s classé dans %q, attendu %q", e.Path, e.Category, want[e.Path])
		}
		if e.Path == "love_groove_90bpm.wav" && e.Info.Tempo.BPM != 90 {
			t.Errorf("BPM du nom ignoré : %+v", e.Info.Tempo)
		}
		if e.Path == "Worship/Praise Drums.wav" && math.Abs(e.Info.Tempo.BPM-96) > 0.01 {
			t.Errorf("BPM : %+v", e.Info.Tempo)
		}
	}
	// correction à la main, puis nouvelle analyse : rien n'est refait, la correction reste
	lib.SetCategory("Djembe solo.wav", "Afrobeats")
	lib.Save()
	calls = 0
	lib2, err := Scan(dir, func(done, total int, cur string) { calls++ })
	if err != nil {
		t.Fatal(err)
	}
	if calls != 0 {
		t.Errorf("%d loops réanalysées alors que rien n'a changé", calls)
	}
	if got := lib2.Search("Afrobeats", ""); len(got) != 1 || got[0].Path != "Djembe solo.wav" {
		t.Errorf("correction perdue : %+v", got)
	}
	if got := lib2.Search("", "praise"); len(got) != 1 {
		t.Errorf("recherche : %d résultats", len(got))
	}
}

func TestCategoryAccents(t *testing.T) {
	if c := CategoryOf("loops_coupé-décalé_01.wav"); c != "Coupé-Décalé" {
		t.Errorf("catégorie : %q", c)
	}
	if c := CategoryOf("Adoration_Talking.wav"); c != "Worship" {
		t.Errorf("catégorie : %q", c)
	}
}

func TestLooperOriginalTempoIsExact(t *testing.T) {
	a := synthLoop(96, 1, 0)
	info, stems := Separate(a, "x.wav")
	l := NewLooper(testRate)
	l.Load(info, stems)
	l.SetGlobalBPM(96)
	l.Play()
	n := a.Frames()
	outL, outR := make([]float32, n), make([]float32, n)
	l.Render(outL, outR)
	for i := 0; i < n; i++ {
		if math.Abs(float64(outL[i]-a.L[i])) > 1e-3 {
			t.Fatalf("échantillon %d : %v au lieu de %v", i, outL[i], a.L[i])
		}
	}
}

func TestLooperFollowsGlobalTempo(t *testing.T) {
	a := synthLoop(96, 1, 0)
	info, stems := Separate(a, "x.wav")
	l := NewLooper(testRate)
	l.Load(info, stems)
	l.SetGlobalBPM(120)
	l.Play()
	want := float64(a.Frames()) * 96 / 120
	if math.Abs(l.loopLen()-want) > 1 {
		t.Fatalf("longueur de boucle %v, attendu %v", l.loopLen(), want)
	}
	n := int(want)
	outL, outR := make([]float32, n), make([]float32, n)
	l.Render(outL, outR)
	// la caisse claire du 2e temps doit tomber au 2e temps du nouveau tempo
	beat := 60.0 / 120 * testRate
	s := int(beat)
	before := energy(outL, s-int(0.03*testRate), s-220)
	after := energy(outL, s, s+1100)
	if after < 5*before {
		t.Errorf("coup absent au 2e temps : avant %.3g après %.3g", before, after)
	}
	// Preset BPM : retour au tempo d'origine
	l.SetPresetBPM(true)
	if l.PlayingBPM() != 96 || math.Abs(l.loopLen()-float64(a.Frames())) > 1 {
		t.Errorf("preset BPM : %v BPM, longueur %v", l.PlayingBPM(), l.loopLen())
	}
}

func TestLooperMuteSolo(t *testing.T) {
	a := synthLoop(96, 1, 0)
	info, stems := Separate(a, "x.wav")
	l := NewLooper(testRate)
	l.Load(info, stems)
	l.SetGlobalBPM(96)
	l.Play()
	for _, tr := range l.Tracks {
		tr.Mute = true
	}
	out := make([]float32, 4096)
	out2 := make([]float32, 4096)
	l.Render(out, out2)
	if energy(out, 0, len(out)) != 0 {
		t.Error("son entendu alors que tout est coupé")
	}
	l.Stop()
	l.Play()
	l.Tracks[0].Mute = false // pas en solo : ne doit pas s'entendre
	l.Tracks[1].Mute = false
	l.Tracks[1].Solo = true
	l.Render(out, out2)
	ref := stems[1].Audio.L
	for i := range out {
		if math.Abs(float64(out[i]-ref[i])) > 1e-3 {
			t.Fatalf("solo : échantillon %d = %v au lieu de %v", i, out[i], ref[i])
		}
	}
}

func TestBadFiles(t *testing.T) {
	if _, err := ParseWAV([]byte("RIFF\x00\x00\x00\x00WAVE")); err == nil {
		t.Error("WAV vide accepté")
	}
	if _, err := ParseWAV([]byte("hello")); err == nil {
		t.Error("faux WAV accepté")
	}
	dir := t.TempDir()
	p := filepath.Join(dir, "x.mp3")
	os.WriteFile(p, []byte("pas un mp3"), 0o644)
	if _, err := Load(p); err == nil {
		t.Error("faux MP3 accepté")
	}
	info := Analyze(&Audio{Rate: testRate, L: make([]float32, testRate), R: make([]float32, testRate)}, "silence.wav")
	if len(info.Percussions) != 0 {
		t.Errorf("percussions trouvées dans le silence : %+v", info.Percussions)
	}
}
