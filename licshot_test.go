package main

import (
	"image"
	"image/color"
	"image/png"
	"os"
	"testing"
)

func TestLicShot(t *testing.T) {
	out := os.Getenv("LICSHOT")
	if out == "" {
		t.Skip()
	}
	u, _, _ := newTestUI(t, "")
	l, _, _ := testLicense(t, "pc-shot")
	u.lic = l
	u.refreshLicense()
	u.licShow = true
	u.licKey = "ABCDEFGH23456723ABCDEFGH23456723ABCDEFGH23456723ABCDEFGH234567"
	u.licMsg = "Cette clé n'est pas valable pour cet ordinateur."
	c := NewCanvas(WinW, WinH)
	SharedTextRenderer().Attach(c)
	u.Draw(c)
	img := image.NewRGBA(image.Rect(0, 0, c.W, c.H))
	for i, p := range c.Pix {
		img.Set(i%c.W, i/c.W, color.RGBA{uint8(p >> 16), uint8(p >> 8), uint8(p), 255})
	}
	f, _ := os.Create(out)
	png.Encode(f, img)
	f.Close()
}
