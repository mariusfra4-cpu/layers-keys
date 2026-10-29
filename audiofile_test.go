package main

import (
	"bytes"
	"encoding/binary"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

// fabrique un WAV : bits 8/16/24/32 (PCM) ou 32 (float), canaux 1 ou 2
func makeWAV(freq float64, rate, ch, bits int, float bool, secs float64, loop bool, root int) []byte {
	n := int(float64(rate) * secs)
	var data bytes.Buffer
	for i := 0; i < n; i++ {
		v := 0.5 * math.Sin(twoPi*freq*float64(i)/float64(rate))
		for c := 0; c < ch; c++ {
			x := v
			if c == 1 {
				x = v * 0.5
			}
			switch {
			case float:
				binary.Write(&data, binary.LittleEndian, float32(x))
			case bits == 8:
				data.WriteByte(byte(int(x*127) + 128))
			case bits == 16:
				binary.Write(&data, binary.LittleEndian, int16(x*32767))
			case bits == 24:
				s := int32(x * 8388607)
				data.Write([]byte{byte(s), byte(s >> 8), byte(s >> 16)})
			case bits == 32:
				binary.Write(&data, binary.LittleEndian, int32(x*2147483647))
			}
		}
	}
	var f bytes.Buffer
	tag := uint16(1)
	if float {
		tag = 3
	}
	fmtc := new(bytes.Buffer)
	binary.Write(fmtc, binary.LittleEndian, tag)
	binary.Write(fmtc, binary.LittleEndian, uint16(ch))
	binary.Write(fmtc, binary.LittleEndian, uint32(rate))
	binary.Write(fmtc, binary.LittleEndian, uint32(rate*ch*bits/8))
	binary.Write(fmtc, binary.LittleEndian, uint16(ch*bits/8))
	binary.Write(fmtc, binary.LittleEndian, uint16(bits))
	f.Write(chunk("fmt ", fmtc.Bytes()))
	f.Write(chunk("LIST", []byte("INFOxxxx"))) // bloc inconnu à ignorer (taille paire)
	if loop || root != 60 {
		s := make([]byte, 36+24)
		binary.LittleEndian.PutUint32(s[12:], uint32(root))
		if loop {
			binary.LittleEndian.PutUint32(s[28:], 1)
			binary.LittleEndian.PutUint32(s[44:], uint32(n/4))
			binary.LittleEndian.PutUint32(s[48:], uint32(n*3/4))
		}
		f.Write(chunk("smpl", s))
	}
	f.Write(chunk("data", data.Bytes()))
	body := append([]byte("WAVE"), f.Bytes()...)
	return append(append([]byte("RIFF"), le32(uint32(len(body)))...), body...)
}

func playFile(t *testing.T, path string, note int, secs float64) (*Engine, []float64, []float64) {
	src, _, err := LoadAudioFile(path)
	if err != nil {
		t.Fatalf("%s : %v", filepath.Base(path), err)
	}
	e := NewEngine()
	e.revAmt = 0
	e.SetSource(0, src, "t", "AUDIO")
	e.NoteOn(note, 100, 1)
	l, r := runLR(e, secs)
	return e, l, r
}

func TestWAVFormats(t *testing.T) {
	dir := t.TempDir()
	cases := []struct {
		name  string
		bits  int
		float bool
		ch    int
		rate  int
	}{
		{"8bit", 8, false, 1, 22050},
		{"16bit", 16, false, 1, 44100},
		{"16bit_st", 16, false, 2, 48000},
		{"24bit", 24, false, 1, 44100},
		{"32bit", 32, false, 1, 44100},
		{"float", 32, true, 2, 44100},
	}
	for _, c := range cases {
		p := filepath.Join(dir, c.name+".wav")
		os.WriteFile(p, makeWAV(440, c.rate, c.ch, c.bits, c.float, 1.5, false, 60), 0o644)
		// note 60 = hauteur d'origine ; note 72 = une octave plus haut
		_, y, _ := playFile(t, p, 60, 1.3)
		if cents := peakCents(y, 440); math.Abs(cents) > 12 {
			t.Errorf("%s : %.1f cents", c.name, cents)
		}
		if rms(y[int(0.3*SR):]) < 0.02 {
			t.Errorf("%s : trop faible", c.name)
		}
		_, y, _ = playFile(t, p, 72, 1.0)
		if cents := peakCents(y, 880); math.Abs(cents) > 12 {
			t.Errorf("%s octave : %.1f cents", c.name, cents)
		}
	}
}

func TestWAVStereoAndRoot(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "st.wav")
	os.WriteFile(p, makeWAV(440, 44100, 2, 16, false, 1.5, false, 69), 0o644)
	// racine = 69 : jouer la note 69 donne 440 Hz ; canal gauche plus fort (x1 contre x0,5)
	_, l, r := playFile(t, p, 69, 1.3)
	if cents := peakCents(l, 440); math.Abs(cents) > 12 {
		t.Errorf("note racine : %.1f cents", cents)
	}
	if !(rms(l) > 0) || !(rms(r) > 0) {
		t.Fatal("stéréo : un côté est muet")
	}
	// gauche = canal 0 (x1), droite = canal 1 (x0,5) : le rapport doit être ~2
	if ratio := rms(l) / rms(r); ratio < 1.6 || ratio > 2.5 {
		t.Errorf("stéréo : rapport G/D = %.2f (attendu ~2)", ratio)
	}
}

func TestWAVLoopSustains(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "loop.wav")
	os.WriteFile(p, makeWAV(440, 44000, 1, 16, false, 0.5, true, 60), 0o644)
	e, l, _ := playFile(t, p, 60, 2.0)
	if rms(l[int(1.8*SR):]) < 0.03 {
		t.Fatal("la boucle du WAV (chunk smpl) n'est pas lue")
	}
	e.NoteOff(1)
	run(e, 0.6)
	if e.VoiceCount() != 0 {
		t.Fatalf("pas de relâchement après boucle: %d", e.VoiceCount())
	}
	// sans boucle : le son s'arrête tout seul
	p2 := filepath.Join(dir, "one.wav")
	os.WriteFile(p2, makeWAV(440, 44000, 1, 16, false, 0.3, false, 60), 0o644)
	e, _, _ = playFile(t, p2, 60, 0.7)
	if e.VoiceCount() != 0 {
		t.Fatalf("WAV sans boucle toujours actif: %d", e.VoiceCount())
	}
}

func TestAudioErrors(t *testing.T) {
	dir := t.TempDir()
	bad := map[string][]byte{
		"vide.wav":    {},
		"texte.wav":   []byte("ceci n'est pas un wav"),
		"tronque.wav": makeWAV(440, 44100, 1, 16, false, 0.5, false, 60)[:30],
		"faux.mp3":    []byte("ID3 pas du tout du mp3 ..................."),
		"vide.mp3":    {},
		"autre.ogg":   []byte("OggS"),
		"alea.mp3":    bytes.Repeat([]byte{0xFF, 0xFB, 0x90, 0x00, 0x12, 0x34}, 3000),
	}
	for n, b := range bad {
		p := filepath.Join(dir, n)
		os.WriteFile(p, b, 0o644)
		if _, _, err := LoadAudioFile(p); err == nil {
			t.Errorf("%s : aucune erreur", n)
		}
	}
	// WAV compressé (ADPCM = tag 2)
	w := makeWAV(440, 44100, 1, 16, false, 0.5, false, 60)
	i := bytes.Index(w, []byte("fmt ")) + 8
	binary.LittleEndian.PutUint16(w[i:], 2)
	p := filepath.Join(dir, "adpcm.wav")
	os.WriteFile(p, w, 0o644)
	if _, _, err := LoadAudioFile(p); err == nil {
		t.Error("WAV compressé accepté")
	}
	if _, _, err := LoadAudioFile(filepath.Join(dir, "absent.wav")); err == nil {
		t.Error("fichier absent accepté")
	}
}

func TestMP3(t *testing.T) {
	ff, err := exec.LookPath("ffmpeg")
	if err != nil {
		t.Skip("ffmpeg absent : test MP3 ignoré")
	}
	dir := t.TempDir()
	for _, c := range []struct {
		name string
		args []string
	}{
		{"mono44", []string{"-ac", "1", "-ar", "44100", "-b:a", "128k"}},
		{"stereo48", []string{"-ac", "2", "-ar", "48000", "-b:a", "192k"}},
		{"mono22", []string{"-ac", "1", "-ar", "22050", "-b:a", "64k"}},
		{"vbr32", []string{"-ac", "2", "-ar", "32000", "-q:a", "4"}},
	} {
		p := filepath.Join(dir, c.name+".mp3")
		args := append([]string{"-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=2"}, c.args...)
		args = append(args, "-id3v2_version", "3", "-metadata", "title=Test", p)
		if out, err := exec.Command(ff, args...).CombinedOutput(); err != nil {
			t.Fatalf("ffmpeg : %v %s", err, out)
		}
		_, y, _ := playFile(t, p, 60, 1.4)
		if cents := peakCents(y, 440); math.Abs(cents) > 12 {
			t.Errorf("%s : %.1f cents", c.name, cents)
		}
		if rms(y[int(0.3*SR):]) < 0.01 {
			t.Errorf("%s : trop faible", c.name)
		}
	}
}
