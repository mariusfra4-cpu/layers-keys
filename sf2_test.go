package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// ---- fabrique de SoundFont de test ------------------------------------------------

type tGen struct {
	op  uint16
	amt int16
	lo  uint8
	hi  uint8
	rng bool
}

func gAmt(op uint16, v int) tGen { return tGen{op: op, amt: int16(v)} }
func gRange(op uint16, lo, hi int) tGen {
	return tGen{op: op, lo: uint8(lo), hi: uint8(hi), rng: true}
}

type tZone []tGen

type tSample struct {
	name         string
	data         []int16
	rate         uint32
	pitch        uint8
	loopS, loopE uint32
	typ          uint16
	raw          []byte
}

type tInst struct {
	name  string
	zones []tZone
}
type tPreset struct {
	name      string
	num, bank uint16
	zones     []tZone
}

func le16(v uint16) []byte { b := make([]byte, 2); binary.LittleEndian.PutUint16(b, v); return b }
func le32(v uint32) []byte { b := make([]byte, 4); binary.LittleEndian.PutUint32(b, v); return b }
func name20(s string) []byte {
	b := make([]byte, 20)
	copy(b, s)
	return b
}
func chunk(id string, data []byte) []byte {
	var b bytes.Buffer
	b.WriteString(id)
	b.Write(le32(uint32(len(data))))
	b.Write(data)
	if len(data)&1 == 1 {
		b.WriteByte(0)
	}
	return b.Bytes()
}
func list(typ string, parts ...[]byte) []byte {
	var b bytes.Buffer
	b.WriteString(typ)
	for _, p := range parts {
		b.Write(p)
	}
	return chunk("LIST", b.Bytes())
}
func genBytes(g tGen) []byte {
	b := le16(g.op)
	if g.rng {
		return append(b, g.lo, g.hi)
	}
	return append(b, le16(uint16(g.amt))...)
}

func buildSF2(version uint16, samples []tSample, insts []tInst, presets []tPreset) []byte {
	var smpl, shdr bytes.Buffer
	for _, s := range samples {
		var start, end uint32
		if s.typ&0x10 != 0 { // SF3 : octets d'un flux Ogg Vorbis
			start = uint32(smpl.Len())
			smpl.Write(s.raw)
			end = uint32(smpl.Len())
		} else {
			start = uint32(smpl.Len() / 2)
			for _, v := range s.data {
				smpl.Write(le16(uint16(v)))
			}
			end = uint32(smpl.Len() / 2)
			for i := 0; i < 46; i++ {
				smpl.Write([]byte{0, 0})
			}
		}
		shdr.Write(name20(s.name))
		shdr.Write(le32(start))
		shdr.Write(le32(end))
		if s.typ&0x10 != 0 {
			shdr.Write(le32(s.loopS))
			shdr.Write(le32(s.loopE))
		} else {
			shdr.Write(le32(start + s.loopS))
			shdr.Write(le32(start + s.loopE))
		}
		shdr.Write(le32(s.rate))
		shdr.WriteByte(s.pitch)
		shdr.WriteByte(0)
		shdr.Write(le16(0))
		typ := s.typ
		if typ == 0 {
			typ = 1
		}
		shdr.Write(le16(typ))
	}
	shdr.Write(name20("EOS"))
	shdr.Write(make([]byte, 26))

	var inst, ibag, imod, igen bytes.Buffer
	bag, gen := 0, 0
	for _, in := range insts {
		inst.Write(name20(in.name))
		inst.Write(le16(uint16(bag)))
		for _, z := range in.zones {
			ibag.Write(le16(uint16(gen)))
			ibag.Write(le16(0))
			bag++
			for _, g := range z {
				igen.Write(genBytes(g))
				gen++
			}
		}
	}
	inst.Write(name20("EOI"))
	inst.Write(le16(uint16(bag)))
	ibag.Write(le16(uint16(gen)))
	ibag.Write(le16(0))
	igen.Write(make([]byte, 4))
	imod.Write(make([]byte, 10))

	var phdr, pbag, pmod, pgen bytes.Buffer
	bag, gen = 0, 0
	for _, p := range presets {
		phdr.Write(name20(p.name))
		phdr.Write(le16(p.num))
		phdr.Write(le16(p.bank))
		phdr.Write(le16(uint16(bag)))
		phdr.Write(make([]byte, 12))
		for _, z := range p.zones {
			pbag.Write(le16(uint16(gen)))
			pbag.Write(le16(0))
			bag++
			for _, g := range z {
				pgen.Write(genBytes(g))
				gen++
			}
		}
	}
	phdr.Write(name20("EOP"))
	phdr.Write(le16(255))
	phdr.Write(le16(255))
	phdr.Write(le16(uint16(bag)))
	phdr.Write(make([]byte, 12))
	pbag.Write(le16(uint16(gen)))
	pbag.Write(le16(0))
	pgen.Write(make([]byte, 4))
	pmod.Write(make([]byte, 10))

	info := list("INFO", chunk("ifil", append(le16(version), le16(1)...)), chunk("INAM", []byte("Test\x00")))
	sdta := list("sdta", chunk("smpl", smpl.Bytes()))
	pdta := list("pdta",
		chunk("phdr", phdr.Bytes()), chunk("pbag", pbag.Bytes()), chunk("pmod", pmod.Bytes()), chunk("pgen", pgen.Bytes()),
		chunk("inst", inst.Bytes()), chunk("ibag", ibag.Bytes()), chunk("imod", imod.Bytes()), chunk("igen", igen.Bytes()),
		chunk("shdr", shdr.Bytes()))
	body := append([]byte("sfbk"), append(info, append(sdta, pdta...)...)...)
	return append(append([]byte("RIFF"), le32(uint32(len(body)))...), body...)
}

func sine(freq, rate float64, n int, amp float64) []int16 {
	d := make([]int16, n)
	for i := range d {
		d[i] = int16(amp * 32767 * math.Sin(twoPi*freq*float64(i)/rate))
	}
	return d
}

func writeTmp(t *testing.T, b []byte) string {
	p := filepath.Join(t.TempDir(), "test.sf2")
	if err := os.WriteFile(p, b, 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

func testBank() []byte {
	samples := []tSample{
		{name: "A440", data: sine(440, 44000, 4000, 0.8), rate: 44000, pitch: 69, loopS: 1000, loopE: 3000},
		{name: "A880", data: sine(880, 44000, 4000, 0.8), rate: 44000, pitch: 81, loopS: 1000, loopE: 3000},
		{name: "L440", data: sine(440, 44000, 4000, 0.8), rate: 44000, pitch: 69, loopS: 1000, loopE: 3000, typ: 4},
		{name: "R660", data: sine(660, 44000, 4000, 0.8), rate: 44000, pitch: 69, loopS: 1000, loopE: 3000, typ: 2},
		{name: "ONESHOT", data: sine(440, 44000, 4000, 0.8), rate: 44000, pitch: 69},
	}
	rel := gAmt(38, -2786) // 0,2 s
	insts := []tInst{
		{"Inst1", []tZone{
			{gAmt(54, 1), rel},
			{gRange(43, 0, 71), gAmt(53, 0)},
			{gRange(43, 72, 127), gAmt(53, 1)},
		}},
		{"Vel", []tZone{
			{gAmt(54, 1), rel},
			{gRange(44, 0, 63), gAmt(53, 0)},
			{gRange(44, 64, 127), gAmt(58, 69), gAmt(53, 1)},
		}},
		{"Stereo", []tZone{
			{gAmt(54, 1), rel},
			{gAmt(53, 2)},
			{gAmt(53, 3)},
		}},
		{"Excl", []tZone{
			{gAmt(54, 1), rel, gAmt(57, 1)},
			{gAmt(53, 0)},
		}},
		{"OneShot", []tZone{
			{gAmt(53, 4)},
		}},
		{"Filt", []tZone{
			{gAmt(54, 1), rel, gAmt(8, 3600)},
			{gAmt(53, 0)},
		}},
	}
	presets := []tPreset{
		{"TestA", 0, 0, []tZone{{gAmt(41, 0)}}},
		{"TestB", 1, 0, []tZone{{gAmt(51, 12)}, {gAmt(41, 0)}}},
		{"VelSplit", 2, 0, []tZone{{gAmt(41, 1)}}},
		{"Stereo", 3, 0, []tZone{{gAmt(41, 2)}}},
		{"Excl", 4, 0, []tZone{{gAmt(41, 3)}}},
		{"OneShot", 5, 0, []tZone{{gAmt(41, 4)}}},
		{"Filt", 6, 0, []tZone{{gAmt(41, 5)}}},
		{"Drums", 0, 128, []tZone{{gAmt(41, 0)}}},
	}
	return buildSF2(2, samples, insts, presets)
}

func loadPreset(t *testing.T, sf *SF2, name string) *sampleSource {
	for _, p := range sf.Presets() {
		if p.Name == name {
			regs, err := sf.LoadPreset(p.Index)
			if err != nil {
				t.Fatal(err)
			}
			return &sampleSource{regions: regs}
		}
	}
	t.Fatalf("preset %q introuvable", name)
	return nil
}

func sfEngine(t *testing.T, preset string) (*Engine, *SF2) {
	sf, err := OpenSF2(writeTmp(t, testBank()))
	if err != nil {
		t.Fatal(err)
	}
	e := NewEngine()
	e.revAmt = 0
	e.SetSource(0, loadPreset(t, sf, preset), preset, "SF2")
	return e, sf
}

// ---- tests ------------------------------------------------------------------------

func TestSF2List(t *testing.T) {
	sf, err := OpenSF2(writeTmp(t, testBank()))
	if err != nil {
		t.Fatal(err)
	}
	ps := sf.Presets()
	if len(ps) != 8 {
		t.Fatalf("%d presets (attendu 8)", len(ps))
	}
	if ps[0].Name != "TestA" || ps[len(ps)-1].Name != "Drums" || ps[len(ps)-1].Bank != 128 {
		t.Fatalf("ordre/noms inattendus: %+v", ps)
	}
	if sf.Name != "Test" {
		t.Fatalf("nom du bank: %q", sf.Name)
	}
}

func TestSF2Pitch(t *testing.T) {
	cases := []struct {
		preset string
		note   int
		vel    int
		freq   float64
	}{
		{"TestA", 69, 100, 440},
		{"TestA", 60, 100, 261.63},
		{"TestA", 81, 100, 880},
		{"TestA", 84, 100, 1046.5},
		{"TestB", 69, 100, 880},
		{"VelSplit", 69, 30, 440},
		{"VelSplit", 69, 110, 880},
	}
	for _, c := range cases {
		e, _ := sfEngine(t, c.preset)
		e.NoteOn(c.note, c.vel, 1)
		y := run(e, 1.3)
		cents := peakCents(y, c.freq)
		if math.Abs(cents) > 10 {
			t.Errorf("%s note %d vel %d : %.1f cents d'écart", c.preset, c.note, c.vel, cents)
		}
		if rms(y[int(0.3*SR):]) < 0.02 {
			t.Errorf("%s note %d : son trop faible", c.preset, c.note)
		}
	}
}

func TestSF2LoopRelease(t *testing.T) {
	e, _ := sfEngine(t, "TestA")
	e.NoteOn(69, 100, 1)
	y := run(e, 2.0)
	if r := rms(y[int(1.8*SR):]); r < 0.03 {
		t.Fatalf("la boucle ne tient pas la note: %.4f", r)
	}
	e.NoteOff(1)
	run(e, 0.5)
	if e.VoiceCount() != 0 {
		t.Fatalf("la voix ne s'arrête pas après le relâchement: %d", e.VoiceCount())
	}
	e, _ = sfEngine(t, "OneShot")
	e.NoteOn(69, 100, 1)
	run(e, 0.3)
	if e.VoiceCount() != 0 {
		t.Fatalf("échantillon sans boucle : %d voix", e.VoiceCount())
	}
}

func TestSF2StereoAndExcl(t *testing.T) {
	e, _ := sfEngine(t, "Stereo")
	e.NoteOn(69, 100, 1)
	l, r := runLR(e, 1.3)
	pl440, pl660 := power(l, 440), power(l, 660)
	pr440, pr660 := power(r, 440), power(r, 660)
	if !(pl440 > 50*pl660 && pr660 > 50*pr440) {
		t.Fatalf("séparation gauche/droite incorrecte: L440=%g L660=%g R440=%g R660=%g", pl440, pl660, pr440, pr660)
	}
	e, _ = sfEngine(t, "Excl")
	e.NoteOn(60, 100, 1)
	run(e, 0.2)
	e.NoteOn(62, 100, 2)
	run(e, 0.5)
	if e.VoiceCount() != 1 {
		t.Fatalf("classe exclusive : %d voix (attendu 1)", e.VoiceCount())
	}
}

func TestSF2Filter(t *testing.T) {
	e, _ := sfEngine(t, "Filt")
	e.NoteOn(69, 100, 1)
	y := run(e, 1.0)
	e2, _ := sfEngine(t, "TestA")
	e2.NoteOn(69, 100, 1)
	y2 := run(e2, 1.0)
	if rms(y[int(0.3*SR):]) > 0.2*rms(y2[int(0.3*SR):]) {
		t.Fatalf("le filtre du SoundFont n'est pas appliqué: %.4f vs %.4f", rms(y[int(0.3*SR):]), rms(y2[int(0.3*SR):]))
	}
}

func TestSF2Errors(t *testing.T) {
	if _, err := OpenSF2(filepath.Join(t.TempDir(), "absent.sf2")); err == nil {
		t.Fatal("fichier absent accepté")
	}
	p := filepath.Join(t.TempDir(), "x.sf2")
	os.WriteFile(p, []byte("pas un soundfont du tout"), 0o644)
	if _, err := OpenSF2(p); err == nil {
		t.Fatal("fichier invalide accepté")
	}
	b := testBank()
	b3 := buildSF2(3, []tSample{{name: "A", data: sine(440, 44000, 100, 0.5), rate: 44000, pitch: 69}},
		[]tInst{{"I", []tZone{{gAmt(53, 0)}}}}, []tPreset{{"P", 0, 0, []tZone{{gAmt(41, 0)}}}})
	if sf, err := OpenSF2(writeTmp(t, b3)); err != nil {
		t.Fatalf("SF3 : %v", err)
	} else {
		sf.LoadPreset(0) // échantillon non compressé dans un SF3 : ignoré, sans plantage
	}
	for _, cut := range []int{20, 100, len(b) / 2, len(b) - 10} {
		p := filepath.Join(t.TempDir(), "cut.sf2")
		os.WriteFile(p, b[:cut], 0o644)
		if sf, err := OpenSF2(p); err == nil {
			for _, pi := range sf.Presets() {
				sf.LoadPreset(pi.Index)
			}
		}
	}
}

func TestSF3Vorbis(t *testing.T) {
	raw, err := os.ReadFile("testdata/test.ogg")
	if err != nil {
		t.Skip("fichier Ogg de test absent")
	}
	b := buildSF2(3, []tSample{{name: "OGG", raw: raw, rate: 44100, pitch: 60, typ: 0x11}},
		[]tInst{{"I", []tZone{{gAmt(53, 0)}}}}, []tPreset{{"P", 0, 0, []tZone{{gAmt(41, 0)}}}})
	sf, err := OpenSF2(writeTmp(t, b))
	if err != nil {
		t.Fatal(err)
	}
	regs, err := sf.LoadPreset(0)
	if err != nil || len(regs) == 0 {
		t.Fatalf("SF3 : pas de région (%v)", err)
	}
	r := regs[0]
	if len(r.Data) < 1000 || r.End < 1000 {
		t.Fatalf("SF3 : échantillon trop court (%d, fin %d)", len(r.Data), r.End)
	}
	peak := 0
	for _, v := range r.Data {
		if int(v) > peak {
			peak = int(v)
		}
	}
	if peak < 500 {
		t.Fatalf("SF3 : signal vide (pic %d)", peak)
	}
}

// chargement d'un gros SoundFont : 60 échantillons de 2 millions de points (240 Mo)
func TestSF2BigLoad(t *testing.T) {
	if os.Getenv("BIGSF2") == "" {
		t.Skip("BIGSF2=1 pour mesurer")
	}
	var samples []tSample
	var izones []tZone
	for i := 0; i < 60; i++ {
		d := make([]int16, 2_000_000)
		for k := range d {
			d[k] = int16((k * (i + 3)) % 20000)
		}
		samples = append(samples, tSample{name: fmt.Sprintf("s%02d", i), data: d, rate: 44100, pitch: uint8(30 + i)})
		izones = append(izones, tZone{gRange(43, i, i), gAmt(53, i)})
	}
	var pz []tZone
	pz = append(pz, tZone{gAmt(41, 0)})
	b := buildSF2(2, samples, []tInst{{name: "I", zones: izones}}, []tPreset{{name: "Big", zones: pz}})
	path := writeTmp(t, b)
	sf, err := OpenSF2(path)
	if err != nil {
		t.Fatal(err)
	}
	defer sf.Close()
	t0 := time.Now()
	regs := loadPreset(t, sf, "Big").regions
	t.Logf("%d régions chargées en %v (%d Mo)", len(regs), time.Since(t0), len(b)>>20)
}
