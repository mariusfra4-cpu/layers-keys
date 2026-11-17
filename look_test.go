package main

import (
	"fmt"
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
		u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
		u.loadSource(1, SrcCfg{Kind: "builtin", Builtin: iPad})
		u.loadSource(2, SrcCfg{Kind: "builtin", Builtin: iPad})
		u.labels[0], u.labels[1], u.labels[2] = "Grand Piano", "Nappe Gospel", "Strings"
		u.setLook(func(c *Config) { c.LookOn, c.LookWhite, c.LookBlack, c.LookFont, c.LookTheme, c.LookText = 0, 0, 0, 0, 0, 0 })
		for _, n := range []int{53, 57, 60, 64} {
			u.counts[n] = 1
		}
		u.OpenSettings()
		shotPNG(t, dir, "01_parametres", u)
		for tab := 0; tab < 3; tab++ {
			look.tab = tab
			u.OpenLook()
			shotPNG(t, dir, fmt.Sprintf("0%d_personnaliser_onglet%d", tab+2, tab+1), u)
		}
		u.closeOverlay()
		for i := range uiThemes {
			u.setLook(func(c *Config) { c.LookTheme = i })
			shotPNG(t, dir, fmt.Sprintf("1%d_theme_%d", i, i), u)
		}
		u.setLook(func(c *Config) { c.LookTheme, c.LookText, c.LookFont = 1, 4, famSerif })
		shotPNG(t, dir, "20_saphir_glacier_classique", u)
		u.setLook(func(c *Config) { c.LookTheme, c.LookText, c.LookOn = 2, 1, 3 })
		look.tab = 1
		u.OpenLook()
		shotPNG(t, dir, "21_emeraude_creme_page", u)
		u.closeOverlay()
	}
	u.setLook(func(c *Config) { c.LookOn, c.LookWhite, c.LookBlack, c.LookFont, c.LookTheme, c.LookText = 0, 0, 0, 0, 0, 0 })
}
