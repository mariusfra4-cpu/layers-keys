package main

// Paramètres > Molette de modulation : sur quelles pistes elle agit, et le Leslie ; affichage en temps réel.

import (
	"fmt"
	"math"
)

var wheelModeNames = []string{"Piste sélectionnée seulement", "Toutes les pistes", "Désactivée"}
var wheelModeSubs = []string{
	"conseillé : le lead bouge, les strings et le piano ne sont pas dérangés",
	"comme un synthé classique : toutes les pistes qui jouent",
	"la molette ne change rien au son",
}

func (u *UI) wheelText() string { return T(wheelModeNames[clamp(u.cfg.WheelMode, 0, 2)]) }

func (u *UI) setWheel(f func(c *Config)) {
	f(u.cfg)
	u.cfg.sanitize()
	u.applyPlaying()
	u.dirty = true
	u.host.Invalidate()
}

// nom de la piste sur laquelle la molette agit en ce moment
func (u *UI) wheelTargetText() string {
	switch u.cfg.WheelMode {
	case 1:
		return T("toutes les pistes")
	case 2:
		return T("aucune piste (molette désactivée)")
	}
	i := u.eng.WheelTrack()
	name := u.labels[i]
	if name == "" {
		if _, n, _ := u.eng.ChInfo(i); n != "" {
			name = n
		} else {
			name = T("Piste vide")
		}
	}
	s := Tf("piste %d · %s", i+1, name)
	if !u.cfg.WheelNoLes && u.eng.hasLeslie(i) {
		s += "  —  " + T("Leslie : la molette règle sa vitesse")
	}
	return s
}

func (u *UI) OpenWheel() {
	u.ov = &overlay{title: T("Molette de modulation"), hint: T("Tourne la molette de ton clavier : l'écran montre en direct ce qu'elle fait · Échap pour fermer"),
		back: u.OpenSettings, sel: -1, live: true}
	u.ov.custom = u.drawWheelPage
	u.host.Invalidate()
}

func (u *UI) drawWheelPage(c *Canvas, r rect) {
	x, y, w := r.x+6, r.y+4, r.w-12
	// en direct : position de la molette et piste concernée
	c.Text(x, y, w, 16, T("En direct"), fontXs, colGold, 0)
	y += 22
	c.RRect(float64(x), float64(y), float64(w), 74, 8, colRow, 1)
	c.RRectStroke(float64(x), float64(y), float64(w), 74, 8, 1, colLine, 1)
	m := u.eng.ModWheel()
	bx, bw := x+16, w-110
	c.RRect(float64(bx), float64(y+16), float64(bw), 14, 7, 0x05080e, 1)
	if m > 0.004 {
		c.RRectGrad(float64(bx), float64(y+16), float64(bw)*m, 14, 7, colGoldHi, colGold, 1)
	}
	c.Text(bx+bw+10, y+12, 70, 22, fmt.Sprintf("%d %%", int(math.Round(m*100))), fontLg, colGold, 2)
	c.Text(bx, y+40, w-32, 22, T("Agit sur : ")+u.wheelTargetText(), fontMdB, colText, 0)
	y += 92

	// sur quelles pistes
	c.Text(x, y, w, 16, T("La molette agit sur"), fontXs, colGold, 0)
	y += 22
	for i := range wheelModeNames {
		i := i
		br := rect{x, y, w, 40}
		on := u.cfg.WheelMode == i
		bg := uint32(colRow)
		if on {
			bg = colRowSel
		}
		c.RRect(float64(br.x), float64(br.y), float64(br.w), float64(br.h), 6, bg, 1)
		stroke := uint32(colLine)
		if on {
			stroke = colGold
		}
		c.RRectStroke(float64(br.x), float64(br.y), float64(br.w), float64(br.h), 6, 1.5, stroke, 1)
		c.Circle(float64(br.x+20), float64(br.y+20), 7, colGoldDim, 1)
		if on {
			c.Circle(float64(br.x+20), float64(br.y+20), 4.5, colGoldHi, 1)
		} else {
			c.Circle(float64(br.x+20), float64(br.y+20), 5.5, colRow, 1)
		}
		c.Text(br.x+38, br.y+3, 250, 18, T(wheelModeNames[i]), fontMdB, colText, 0)
		c.Text(br.x+38, br.y+21, br.w-48, 15, T(wheelModeSubs[i]), fontXs, colText2, 0)
		u.reg(region{r: br, id: fmt.Sprintf("wheelMode%d", i), down: func(int, int) { u.setWheel(func(c *Config) { c.WheelMode = i }) }})
		y += 46
	}
	y += 8

	// orgue
	c.Text(x, y, w, 16, T("Orgue (pistes avec LESLIE)"), fontXs, colGold, 0)
	y += 22
	bw2 := (w - 8) / 2
	u.button(c, "wheelLesOn", rect{x, y, bw2, 32}, T("La molette règle la vitesse"), !u.cfg.WheelNoLes, fontSm, func() {
		u.setWheel(func(c *Config) { c.WheelNoLes = false })
	})
	u.button(c, "wheelLesOff", rect{x + bw2 + 8, y, bw2, 32}, T("Vitesse seulement à la télécommande"), u.cfg.WheelNoLes, fontSm, func() {
		u.setWheel(func(c *Config) { c.WheelNoLes = true })
	})
	y += 40
	c.Text(x, y, w, 15, T("En haut = rapide, en bas = lent. Sur ces pistes, la molette ne fait pas de vibrato."), fontXs, colText2, 0)
}

func init() {
	for k, v := range map[string][2]string{
		"Molette de modulation":        {"Modulation wheel", "Rueda de modulación"},
		"Molette de modulation…":       {"Modulation wheel…", "Rueda de modulación…"},
		"Piste sélectionnée seulement": {"Selected track only", "Solo la pista seleccionada"},
		"Toutes les pistes":            {"All tracks", "Todas las pistas"},
		"Désactivée":                   {"Off", "Desactivada"},
		"conseillé : le lead bouge, les strings et le piano ne sont pas dérangés": {"recommended: the lead moves, strings and piano are not disturbed", "recomendado: el lead se mueve, las cuerdas y el piano no se alteran"},
		"comme un synthé classique : toutes les pistes qui jouent":                {"like a classic synth: every track that plays", "como un sinte clásico: todas las pistas que suenan"},
		"la molette ne change rien au son":                                        {"the wheel does not change the sound", "la rueda no cambia el sonido"},
		"toutes les pistes":                                                       {"all tracks", "todas las pistas"},
		"aucune piste (molette désactivée)":                                       {"no track (wheel off)", "ninguna pista (rueda desactivada)"},
		"piste %d · %s":                                                           {"track %d · %s", "pista %d · %s"},
		"Leslie : la molette règle sa vitesse":                                    {"Leslie: the wheel sets its speed", "Leslie: la rueda ajusta su velocidad"},
		"Tourne la molette de ton clavier : l'écran montre en direct ce qu'elle fait · Échap pour fermer": {"Turn your keyboard's wheel: the screen shows live what it does · Esc to close", "Gira la rueda de tu teclado: la pantalla muestra en directo lo que hace · Esc para cerrar"},
		"En direct":                           {"Live", "En directo"},
		"Agit sur : ":                         {"Acts on: ", "Actúa en: "},
		"La molette agit sur":                 {"The wheel acts on", "La rueda actúa en"},
		"Orgue (pistes avec LESLIE)":          {"Organ (tracks with LESLIE)", "Órgano (pistas con LESLIE)"},
		"La molette règle la vitesse":         {"The wheel sets the speed", "La rueda ajusta la velocidad"},
		"Vitesse seulement à la télécommande": {"Speed from the remote only", "Velocidad solo con el mando"},
		"En haut = rapide, en bas = lent. Sur ces pistes, la molette ne fait pas de vibrato.": {"Up = fast, down = slow. On these tracks the wheel adds no vibrato.", "Arriba = rápido, abajo = lento. En estas pistas la rueda no hace vibrato."},
	} {
		if _, ok := trTable[k]; !ok {
			trTable[k] = v
		}
	}
}
