package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestSoundIcon(t *testing.T) {
	cases := []struct {
		name, file string
		sf         bool
		bank, prog int
		want       int
	}{
		{"Lead Squash", "", true, 8, 3, icWave},
		{"Slow Lead", "", true, 0, 5, icWave},
		{"Square Lead", "", true, 0, 80, icSquare},
		{"Warm Pad", "", true, 0, 89, icCloud},
		{"Nappe Gospel", "", false, 0, 0, icCloud},
		{"Electric Bass (finger)", "", true, 0, 33, icBass},
		{"Guitare Basse", "", true, 0, 0, icBass},
		{"Bass Drum 1", "", true, 0, 0, icDrum},
		{"Nylon Guitar", "", true, 0, 24, icGuitar},
		{"Steinway Grand", "", true, 0, 0, icKeys},
		{"Yamaha C7 Concert", "", false, 0, 0, icKeys},
		{"Rhodes Mark I", "", true, 0, 4, icUpright},
		{"Hammond B3", "", true, 0, 16, icOrgan},
		{"Church Organ", "", true, 0, 19, icOrgan},
		{"String Ensemble 1", "", true, 0, 48, icStrings},
		{"Gospel Choir", "", true, 0, 52, icVoice},
		{"Trumpet", "", true, 0, 56, icBrass},
		{"Flute", "", true, 0, 73, icWind},
		{"Glockenspiel", "", true, 0, 9, icBell},
		{"Preset 12", "", true, 0, 90, icCloud}, // nom vide de sens : programme GM = pad
		{"Preset 3", "", true, 0, 56, icBrass},  // idem : cuivres
		{"Bank 5", "Gospel Pads.sf2", true, 3, 7, icCloud},
		{"Kit", "", true, 128, 0, icDrum},
		{"Piste 1", "mon_son.wav", false, 0, 0, icBars},
		{"intro", "Cinematic Strings.mp3", false, 0, 0, icStrings},
	}
	for _, c := range cases {
		if got := soundIcon(c.name, c.file, c.sf, c.bank, c.prog); got != c.want {
			t.Errorf("%q (%q, banque %d, prog %d) : icône %d, attendu %d", c.name, c.file, c.bank, c.prog, got, c.want)
		}
	}
}

// planche des icônes (aperçu visuel, seulement si PREVIEW_DIR est défini)
func TestIconGallery(t *testing.T) {
	dir := os.Getenv("PREVIEW_DIR")
	if dir == "" {
		t.Skip("PREVIEW_DIR non défini")
	}
	c := NewCanvas(900, 200)
	c.VGrad(0, 0, 900, 200, 0x131d2f, 0x0a101b)
	for k := 0; k <= icDownload; k++ {
		if k == icPlus || k == icLayers && false {
			continue
		}
		cx := float64(50 + (k%10)*85)
		cy := float64(50 + (k/10)*90)
		drawIcon(c, k, cx, cy, 44, 0xdbe6fb, 1)
	}
	raw := make([]byte, 0, len(c.Pix)*3)
	for _, p := range c.Pix {
		raw = append(raw, byte(p>>16), byte(p>>8), byte(p))
	}
	rgb := filepath.Join(dir, "icons.rgb")
	os.WriteFile(rgb, raw, 0o644)
	exec.Command("python3", "/tmp/rgb2png.py", rgb, "900", "200", filepath.Join(dir, "icons.png")).Run()
}
