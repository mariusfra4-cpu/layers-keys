package main

import (
	"bytes"
	"encoding/binary"
	"math"
	"os"
	"path/filepath"
	"testing"
)

// tinySF2 : SoundFont minimale (sinus en boucle) pour les tests.
func tinySF2(t *testing.T) string {
	t.Helper()
	const n = 4410
	var smpl bytes.Buffer
	for i := 0; i < n; i++ {
		v := int16(16000 * math.Sin(2*math.Pi*float64(i)*100/44100)) // 100 Hz : boucle parfaite
		binary.Write(&smpl, binary.LittleEndian, v)
	}
	for i := 0; i < 46; i++ {
		smpl.Write([]byte{0, 0})
	}
	chunk := func(id string, d []byte) []byte {
		var b bytes.Buffer
		b.WriteString(id)
		binary.Write(&b, binary.LittleEndian, uint32(len(d)))
		b.Write(d)
		if len(d)%2 == 1 {
			b.WriteByte(0)
		}
		return b.Bytes()
	}
	list := func(typ string, parts ...[]byte) []byte {
		d := []byte(typ)
		for _, p := range parts {
			d = append(d, p...)
		}
		return chunk("LIST", d)
	}
	name := func(s string) []byte { b := make([]byte, 20); copy(b, s); return b }
	le16 := func(v ...int) []byte {
		var b bytes.Buffer
		for _, x := range v {
			binary.Write(&b, binary.LittleEndian, uint16(x))
		}
		return b.Bytes()
	}
	le32 := func(v ...int) []byte {
		var b bytes.Buffer
		for _, x := range v {
			binary.Write(&b, binary.LittleEndian, uint32(x))
		}
		return b.Bytes()
	}
	// préréglages : piano (0:0) et batterie (128:0), tous deux → instrument 0
	var phdr []byte
	phdr = append(phdr, append(name("Sine"), le16(0, 0, 0)...)...)
	phdr = append(phdr, le32(0, 0, 0)...)
	phdr = append(phdr, append(name("Drum"), le16(0, 128, 1)...)...)
	phdr = append(phdr, le32(0, 0, 0)...)
	phdr = append(phdr, append(name("EOP"), le16(0, 0, 2)...)...)
	phdr = append(phdr, le32(0, 0, 0)...)
	pbag := le16(0, 0, 1, 0, 2, 0)
	pgen := append(le16(gInstrument, 0, gInstrument, 0), le16(0, 0)...)
	inst := append(append(name("SineInst"), le16(0)...), append(name("EOI"), le16(1)...)...)
	ibag := le16(0, 0, 3, 0)
	igen := le16(gReleaseVolEnv, uint16ToInt(-2400), gSampleModes, 1, gSampleID, 0, 0, 0)
	var shdr []byte
	shdr = append(shdr, name("sine")...)
	shdr = append(shdr, le32(0, n, 0, n, 44100)...)
	shdr = append(shdr, 43, 0) // la note 43 ≈ 98 Hz (proche de 100 Hz)
	shdr = append(shdr, le16(0, 1)...)
	shdr = append(shdr, make([]byte, 46)...)
	mod0 := make([]byte, 10)
	body := []byte("sfbk")
	body = append(body, list("INFO", chunk("ifil", le16(2, 1)), chunk("INAM", []byte("Test\x00")))...)
	body = append(body, list("sdta", chunk("smpl", smpl.Bytes()))...)
	body = append(body, list("pdta", chunk("phdr", phdr), chunk("pbag", pbag), chunk("pmod", mod0),
		chunk("pgen", pgen), chunk("inst", inst), chunk("ibag", ibag), chunk("imod", mod0),
		chunk("igen", igen), chunk("shdr", shdr))...)
	p := filepath.Join(t.TempDir(), "tiny.sf2")
	if err := os.WriteFile(p, chunk("RIFF", body), 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

func uint16ToInt(v int) int { return int(uint16(int16(v))) }

func TestParseStyle(t *testing.T) {
	st, err := LoadStyle("testdata/TestPop.sty")
	if err != nil {
		t.Fatal(err)
	}
	if st.Name != "Test Pop 8Beat" || st.PPQN != 1920 || math.Abs(st.BPM()-120) > 0.01 {
		t.Fatalf("en-tête : %q %d %.1f", st.Name, st.PPQN, st.BPM())
	}
	want := []string{"Intro A", "Main A", "Fill In AA", "Fill In AB", "Main B", "Fill In BB", "Fill In BA", "Ending A"}
	if len(st.Sections) != len(want) {
		t.Fatalf("%d sections, attendu %d", len(st.Sections), len(want))
	}
	for i, s := range st.Sections {
		if s.Name != want[i] {
			t.Errorf("section %d = %q, attendu %q", i, s.Name, want[i])
		}
	}
	if b := st.Sections[1].End - st.Sections[1].Start; b != 4*4*1920 {
		t.Errorf("Main A : %d tics, attendu 4 mesures", b)
	}
	r := st.Routing("Main B")
	if r[2].Dest != 10 {
		t.Errorf("Main B : canal source 3 devrait aller vers la basse (canal 11), obtenu %+v", r[2])
	}
	if r[9].NTT != 0 || !r[9].Drum {
		t.Errorf("batterie mal lue : %+v", r[9])
	}
	chain := st.DemoChain()
	var names []string
	for _, s := range chain {
		names = append(names, s.Name)
	}
	if len(names) != 5 || names[0] != "Intro A" || names[2] != "Fill In AB" || names[4] != "Ending A" {
		t.Errorf("enchaînement : %v", names)
	}
}

func TestNotAStyle(t *testing.T) {
	if _, err := ParseStyle([]byte("bonjour, ceci n'est pas un style"), "x.sty"); err == nil {
		t.Fatal("erreur attendue")
	}
}

func TestParseKey(t *testing.T) {
	cases := map[string]int{"Do": 0, "ré": 2, "Re#": 3, "Mib": 3, "sol": 7, "La": 9, "Sib": 10, "C": 0, "F#": 6, "Bb": 10, "B": 11}
	for s, v := range cases {
		if k, ok := parseKey(s); !ok || k != v {
			t.Errorf("parseKey(%q) = %d,%v ; attendu %d", s, k, ok, v)
		}
	}
	if _, ok := parseKey("X"); ok {
		t.Error("X ne devrait pas être reconnu")
	}
}

func TestRenderAndWrite(t *testing.T) {
	sf, err := LoadSoundFont(tinySF2(t))
	if err != nil {
		t.Fatal(err)
	}
	st, _ := LoadStyle("testdata/TestPop.sty")
	opt := Options{Rate: 44100, Repeats: 1, FillRepeats: 1, Reverb: 1, TailMax: 2}
	a, _ := RenderChain(st, sf, []Section{st.Sections[1]}, opt)
	// Main A : 4 mesures à 120 BPM = 8 s, plus la queue
	sec := float64(a.Len()) / 44100
	if sec < 8 || sec > 11 {
		t.Fatalf("durée %.2f s", sec)
	}
	if pk := a.Peak(); pk < 0.01 || math.IsNaN(pk) {
		t.Fatalf("crête anormale %v", pk)
	}
	// tempo forcé à 60 BPM : deux fois plus long
	opt.BPM = 60
	b, _ := RenderChain(st, sf, []Section{st.Sections[1]}, opt)
	if d := float64(b.Len()) / 44100; d < 16 || d > 19 {
		t.Fatalf("à 60 BPM : %.2f s", d)
	}

	dir := t.TempDir()
	wav := filepath.Join(dir, "a.wav")
	if err := WriteWAV(wav, a, 44100, 16, 0.9/a.Peak()); err != nil {
		t.Fatal(err)
	}
	fi, _ := os.Stat(wav)
	if fi.Size() != int64(44+4*a.Len()) {
		t.Errorf("taille WAV %d", fi.Size())
	}
	mp3 := filepath.Join(dir, "a.mp3")
	if err := WriteMP3(mp3, a, 44100, 320, 0.9/a.Peak(), "", "titre", "artiste"); err != nil {
		t.Fatal(err)
	}
	d, _ := os.ReadFile(mp3)
	if !bytes.HasPrefix(d, []byte("ID3")) {
		t.Fatal("balise ID3 absente")
	}
	// première trame MP3 juste après la balise : synchro 0xFFF, MPEG-1 couche III, 320 kbit/s
	sz := int(d[6])<<21 | int(d[7])<<14 | int(d[8])<<7 | int(d[9])
	h := d[10+sz:]
	if h[0] != 0xFF || h[1]&0xFE != 0xFA || h[2]>>4 != 14 {
		t.Fatalf("en-tête MP3 inattendu % x", h[:4])
	}
	// débit moyen proche de 320 kbit/s
	kbps := float64(len(d)) * 8 / (float64(a.Len()) / 44100) / 1000
	if kbps < 300 || kbps > 330 {
		t.Errorf("débit %.0f kbit/s", kbps)
	}
}

func TestTranspose(t *testing.T) {
	sf, _ := LoadSoundFont(tinySF2(t))
	st, _ := LoadStyle("testdata/TestPop.sty")
	p := newPlayer(st, sf, Options{Rate: 44100, KeySet: true, Key: 2})
	if x := p.transposeFor(0); x != 2 {
		t.Errorf("Do → Ré : %d", x)
	}
	p.opt.Key = 10
	if x := p.transposeFor(0); x != -2 {
		t.Errorf("Do → Sib : %d (au plus près attendu)", x)
	}
}
