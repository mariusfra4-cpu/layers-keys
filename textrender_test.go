//go:build !windows

package main

import (
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"testing"
)

func writePNG(t *testing.T, c *Canvas, path string) {
	img := image.NewRGBA(image.Rect(0, 0, c.W, c.H))
	for i, p := range c.Pix {
		img.Set(i%c.W, i/c.W, color.RGBA{uint8(p >> 16), uint8(p >> 8), uint8(p), 255})
	}
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	png.Encode(f, img)
}

func TestTextRenderer(t *testing.T) {
	r := SharedTextRenderer()
	c := NewCanvas(400, 120)
	for i := range c.Pix {
		c.Pix[i] = 0x0a101b
	}
	r.Attach(c)
	c.Text(10, 10, 380, 20, "Layers Keys — Do Ré Mi Fa Sol  ◀ Retour  ▶ ■ ● →", fontMd, 0xffffff, 0)
	c.Text(10, 40, 100, 20, "Un texte beaucoup trop long pour la case", fontMdB, 0xffd24a, 0)
	c.Text(10, 70, 380, 30, "CENTRÉ", fontLg, 0xffffff, 1)
	c.FlushText()
	n := 0
	for _, p := range c.Pix {
		if p != 0x0a101b {
			n++
		}
	}
	if n < 800 {
		t.Fatalf("le texte n'a presque rien dessiné (%d pixels)", n)
	}
	if dir := os.Getenv("PREVIEW_DIR"); dir != "" {
		writePNG(t, c, filepath.Join(dir, "text.png"))
	}
}

// l'écran complet avec le rendu de texte intégré (celui du Mac)
func TestMacScreens(t *testing.T) {
	dir := os.Getenv("PREVIEW_DIR")
	if dir == "" {
		t.Skip()
	}
	u, _, _ := newTestUI(t, "")
	c := NewCanvas(WinW, WinH)
	SharedTextRenderer().Attach(c)
	u.Draw(c)
	writePNG(t, c, filepath.Join(dir, "mac_main.png"))
	u.OpenSettings()
	u.Draw(c)
	writePNG(t, c, filepath.Join(dir, "mac_settings.png"))
}
