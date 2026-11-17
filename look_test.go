package main

import (
	"os"
	"testing"
)

// la personnalisation est enregistrée, relue et bornée ; les captures (LOOKSHOTS=dossier) montrent le rendu
func TestLook(t *testing.T) {
	u, _, _ := newTestUI(t, "")
	u.setLook(func(c *Config) { c.LookOn, c.LookWhite, c.LookBlack, c.LookFont = 1, 4, 4, famSerif })
	if look.on != 1 || look.white != 4 || look.black != 4 || curFam() != famSerif {
		t.Fatalf("réglage non appliqué : %+v", look)
	}
	c := &Config{LookOn: 99, LookWhite: -3, LookFont: 42}
	c.sanitizeLook()
	if c.LookOn != len(lookOnCols)-1 || c.LookWhite != 0 || c.LookFont != nFams-1 {
		t.Fatalf("bornes : %+v", c)
	}
	cv := NewCanvas(WinW, WinH)
	u.OpenLook()
	u.Draw(cv)
	found := false
	for _, tx := range cv.AllTexts {
		if tx.Fam == famSerif {
			found = true
		}
	}
	if !found {
		t.Fatal("le texte n'utilise pas la police choisie")
	}
	if dir := os.Getenv("LOOKSHOTS"); dir != "" {
		for _, n := range []int{53, 57, 60, 61, 64} {
			u.counts[n] = 1
		}
		shotPNG(t, dir, "look_page_serif", u)
		u.closeOverlay()
		shotPNG(t, dir, "look_main_bleu_noir", u)
		u.setLook(func(c *Config) { c.LookOn, c.LookWhite, c.LookBlack, c.LookFont = 6, 2, 2, famMono })
		shotPNG(t, dir, "look_main_violet_creme_mono", u)
		u.OpenSettings()
		shotPNG(t, dir, "look_settings", u)
		u.setLook(func(c *Config) { c.LookOn, c.LookWhite, c.LookBlack, c.LookFont = 0, 0, 0, 0 })
		u.OpenLook()
		shotPNG(t, dir, "look_page_origine", u)
	}
	u.setLook(func(c *Config) { c.LookOn, c.LookWhite, c.LookBlack, c.LookFont = 0, 0, 0, 0 })
}
