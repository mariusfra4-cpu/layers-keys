package main

// Dessin de l'interface Layers Keys (design original : sombre, accents dorés).

import (
	"fmt"
	"math"
	"path/filepath"
	"strings"
)

const (
	colBg      = 0x070b13
	colHeader  = 0x0e1522
	colPanel   = 0x0f1623
	colRow     = 0x121a29
	colRowSel  = 0x1a2538
	colLine    = 0x23314a
	colText    = 0xeaeef6
	colText2   = 0x8d9ab2
	colGold    = 0xf2b705
	colGoldHi  = 0xffd23a
	colGoldDim = 0x8a6a08
	colRed     = 0xe03a3a
	colBlue    = 0x38a1ff
	colGreen   = 0x3ddc84
	colKeyW    = 0xfbf8ee
	colKeyB    = 0x0b0d11
	colDark    = 0x16110a
)

// couleur d'accent de chaque piste (comme sur la maquette)
var trackCols = [nCh]uint32{0xffc21a, 0x3b82f6, 0x22c7b8, 0x4ade80, 0x38bdf8, 0x5a9bff, 0x8cc8ff, 0x94a3b8}

func clamp(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

func clamp01(v float64) float64 { return math.Max(0, math.Min(1, v)) }

func sgn(d int) float64 {
	if d > 0 {
		return 1
	}
	return -1
}

func (u *UI) reg(r region) { u.regions = append(u.regions, r) }

func (u *UI) hover(r rect) bool { return u.ov == nil && r.has(u.mx, u.my) }

func (u *UI) isActive(id string) bool { return u.active != nil && u.active.id == id }

// ---------------------------------------------------------------- éléments d'interface

func (u *UI) button(c *Canvas, id string, r rect, label string, on bool, font int, click func()) {
	u.buttonC(c, id, r, label, on, colGold, colDark, font, click)
}

// bouton avec ses propres couleurs quand il est allumé
func (u *UI) buttonC(c *Canvas, id string, r rect, label string, on bool, onBg, onFg uint32, font int, click func()) {
	top, bot, fg := uint32(0x2b3850), uint32(0x172134), uint32(colText)
	edge := uint32(0x3a4c6b)
	if on {
		top, bot, fg = lerpColor(onBg, 0xffffff, 0.25), onBg, onFg
		edge = lerpColor(onBg, 0xffffff, 0.45)
	} else if u.hover(r) {
		top, bot = 0x384a68, 0x1f2c44
	}
	if u.isActive(id) {
		top, bot = lerpColor(top, 0x000000, 0.18), lerpColor(bot, 0x000000, 0.1)
	}
	x, y, w, h := float64(r.x), float64(r.y), float64(r.w), float64(r.h)
	c.Glow(x, y+1, w, h, 7, 5, 0x000000, 0.35)
	if on {
		c.Glow(x, y, w, h, 7, 7, onBg, 0.22)
	}
	c.RRectGrad(x, y, w, h, 7, top, bot, 1)
	c.RRectStroke(x, y, w, h, 7, 1, edge, 0.55)
	c.Rect(r.x+5, r.y+1, r.w-10, 1, 0xffffff, 0.13)
	c.Text(r.x, r.y, r.w, r.h, label, font, fg, 1)
	if click != nil {
		u.reg(region{r: r, id: id, down: func(x, y int) { click() }})
	}
}

func (u *UI) stepper(c *Canvas, id string, x, y, w, h int, text string, dec, inc func()) {
	c.RRectGrad(float64(x), float64(y), float64(w), float64(h), 7, 0x070b12, 0x0e1623, 1)
	c.RRectStroke(float64(x), float64(y), float64(w), float64(h), 7, 1, 0x26364f, 0.8)
	bw := 22
	l, rr := rect{x, y, bw, h}, rect{x + w - bw, y, bw, h}
	for k, b := range []rect{l, rr} {
		if u.hover(b) {
			c.RRect(float64(b.x), float64(b.y), float64(b.w), float64(b.h), 6, 0x39414d, 1)
		}
		s := "–"
		if k == 1 {
			s = "+"
		}
		c.Text(b.x, b.y, b.w, b.h, s, fontMdB, colText, 1)
	}
	c.Text(x+bw, y, w-2*bw, h, text, fontMdB, colGold, 1)
	u.reg(region{r: l, id: id + "-", down: func(int, int) { dec() }})
	u.reg(region{r: rr, id: id + "+", down: func(int, int) { inc() }})
}

// bouton rotatif : v de 0 à 1 ; bipolar = l'arc part du milieu
func (u *UI) knob(c *Canvas, id string, cx, cy, r float64, get func() float64, set func(float64), def float64, bipolar bool) {
	v := clamp01(get())
	a0, a1 := 0.75*math.Pi, 2.25*math.Pi
	hot := u.isActive(id) || u.hover(rect{int(cx - r - 3), int(cy - r - 3), int(2*r + 6), int(2*r + 6)})
	av := a0 + v*(a1-a0)
	lo, hi := a0, av
	if bipolar {
		m := a0 + 0.5*(a1-a0)
		lo, hi = math.Min(m, av), math.Max(m, av)
	}
	lit := hi-lo > 0.02
	c.GlowCircle(cx, cy+1, r+3, 6, 0x000000, 0.5)
	c.Circle(cx, cy, r+4, 0x05080d, 1)
	c.Arc(cx, cy, r+2, a0, a1, 3, 0x1e2a3f, 1)
	if lit {
		c.Arc(cx, cy, r+2, lo, hi, 7, colGold, 0.16) // lueur
		c.Arc(cx, cy, r+2, lo, hi, 3, colGoldHi, 1)
	}
	// corps métal : anneau clair, puis face sombre avec reflet en haut à gauche
	rim := uint32(0x6b7a93)
	if hot {
		rim = 0x8a9ab6
	}
	c.Circle(cx, cy, r-1.5, rim, 1)
	c.Circle(cx, cy, r-2.5, 0x1a2233, 1)
	c.Circle(cx-r*0.12, cy-r*0.14, r-4.5, 0x2c3850, 1)
	c.Circle(cx+r*0.06, cy+r*0.08, r-6.5, 0x1b2436, 1)
	c.Circle(cx-r*0.28, cy-r*0.32, r*0.32, 0xffffff, 0.07)
	c.Line(cx+(r-13)*math.Cos(av), cy+(r-13)*math.Sin(av), cx+(r-5)*math.Cos(av), cy+(r-5)*math.Sin(av), 2.6, colGoldHi, 1)
	ir := rect{int(cx - r - 4), int(cy - r - 4), int(2*r + 8), int(2*r + 8)}
	u.reg(region{r: ir, id: id,
		down:  func(x, y int) { u.dragY, u.dragV = y, clamp01(get()) },
		drag:  func(x, y int) { set(clamp01(u.dragV + float64(u.dragY-y)/140)) },
		wheel: func(d int) { set(clamp01(get() + 0.03*sgn(d))) },
		dbl:   func() { set(def) }})
}

// curseur horizontal
func (u *UI) slider(c *Canvas, id string, x, y, w, h int, v float64, set func(float64), def float64) {
	v = clamp01(v)
	ty := float64(y) + float64(h)/2
	hot := u.isActive(id) || u.hover(rect{x, y, w, h})
	c.RRectGrad(float64(x), ty-3.5, float64(w), 7, 3.5, 0x04070c, 0x0d1522, 1)
	c.RRectStroke(float64(x), ty-3.5, float64(w), 7, 3.5, 1, 0x26364f, 0.7)
	if v > 0.004 {
		fw := float64(w) * v
		c.Glow(float64(x), ty-3.5, fw, 7, 3.5, 6, colGold, 0.2)
		c.RRectHGrad(float64(x), ty-3, fw, 6, 3, colGoldDim, colGoldHi, 1)
		c.Rect(x+3, int(ty)-2, maxi(int(fw)-6, 0), 1, 0xffffff, 0.3)
	}
	tx := float64(x) + float64(w)*v
	rad := 7.5
	if hot {
		rad = 8.5
	}
	c.GlowCircle(tx, ty+1, rad, 5, 0x000000, 0.55)
	c.Circle(tx, ty, rad+1.2, 0x05080d, 1)
	c.Circle(tx, ty, rad, 0xdfe6f3, 1)
	c.Circle(tx, ty+0.8, rad-1.6, 0x9aa7bd, 1)
	c.Circle(tx, ty, rad-3, colGoldHi, 1)
	c.Circle(tx-1, ty-1.2, rad-5, 0xfff2b0, 0.8)
	pos := func(px int) float64 { return clamp01(float64(px-x) / float64(w)) }
	u.reg(region{r: rect{x - 6, y, w + 12, h}, id: id,
		down:  func(px, py int) { set(pos(px)) },
		drag:  func(px, py int) { set(pos(px)) },
		wheel: func(d int) { set(clamp01(v + 0.03*sgn(d))) },
		dbl:   func() { set(def) }})
}

// ---------------------------------------------------------------- dessin général

func (u *UI) Draw(c *Canvas) {
	u.regions = u.regions[:0]
	if u.licShow {
		u.drawLicense(c)
		c.FlushText()
		return
	}
	c.VGrad(0, 0, c.W, c.H, 0x0a101b, 0x05080e)
	u.drawHeader(c)
	u.drawPresets(c)
	for i := 0; i < nCh; i++ {
		u.drawRow(c, i)
	}
	u.drawPanel(c)
	u.drawKeyboard(c)
	u.drawFooter(c)
	u.drawToast(c)
	u.drawLearnMarks(c)
	c.FlushText() // première passe : le texte du fond
	if u.ov != nil {
		u.drawOverlay(c)
		c.FlushText() // seconde passe : le texte de la fenêtre
	}
}

func (u *UI) drawHeader(c *Canvas) {
	c.VGrad(0, 0, c.W, 58, 0x131d2f, 0x0a101b)
	c.Rect(0, 57, c.W, 1, colGoldDim, 0.55)
	c.Rect(0, 58, c.W, 1, 0x000000, 0.5)
	// reflets en diagonale, discrets
	for k := 0; k < 3; k++ {
		x := float64(240 + k*330)
		c.Tri(x, 0, x+120, 0, x+40, 57, 0xffffff, 0.018)
	}
	// logo : touches de piano dorées
	c.GlowCircle(36, 29, 12, 14, colGold, 0.35)
	c.RRectGrad(14, 12, 44, 34, 5, colGoldHi, colGoldDim, 1)
	for k := 0; k < 4; k++ {
		c.Rect(14+11*(k+1)-1, 26, 2, 20, 0x2a1f05, 0.8)
	}
	for _, k := range []int{0, 1, 3} {
		c.RRect(float64(14+11*(k+1)-5), 12, 10, 19, 2, 0x120d03, 1)
	}
	c.Text(70, 8, 160, 30, "Layers Keys", fontXl, colGold, 0)
	pn := u.projectName()
	if u.changed {
		pn += " *"
	}
	if u.Plugin {
		c.Text(72, 36, 150, 14, "Plugin", fontXs, colText2, 0)
	} else {
		c.Text(72, 36, 150, 14, Tf("Projet : %s", pn), fontXs, colText2, 0)
	}
	if u.Plugin && u.PluginInfo != nil {
		notes, sound, refused := u.PluginInfo()
		nc := uint32(colRed)
		if notes > 0 {
			nc = colGreen
		}
		c.Circle(244, 22, 5, nc, 1)
		c.Text(254, 14, 200, 16, Tf("Notes reçues : %d", notes), fontXs, colText, 0)
		sc, st := uint32(colRed), T("Son : aucun")
		if sound {
			sc, st = colGreen, T("Son : OK")
		}
		if refused > 0 {
			sc, st = colRed, Tf("Son refusé par l'hôte (%d)", refused)
		}
		c.Circle(244, 42, 5, sc, 1)
		c.Text(254, 34, 200, 16, st, fontXs, colText, 0)
		if u.OnShowLog != nil {
			u.button(c, "showlog", rect{460, 14, 124, 30}, "Journal", false, fontSm, func() { u.OnShowLog() })
		}
	}
	if !u.Plugin {
		u.button(c, "pnew", rect{236, 14, 76, 30}, "Nouveau", false, fontSm, func() { u.NewProject() })
		u.button(c, "popen", rect{318, 14, 76, 30}, "Ouvrir", false, fontSm, func() { u.OpenProjectMenu() })
		u.button(c, "psave", rect{400, 14, 106, 30}, "Enregistrer", u.changed, fontSm, func() { u.SaveProject() })
		u.button(c, "psaveas", rect{512, 14, 72, 30}, "Sous…", false, fontSm, func() { u.SaveProjectAs() })
	}

	if u.OnPiano != nil { // Android : bouton du piano tactile
		u.drawChordBox(c, rect{596, 6, 262, 46})
		u.buttonC(c, "touchpiano", rect{866, 14, 140, 30}, "PIANO TACTILE", false, colGold, colDark, fontSm, func() { u.OnPiano() })
	} else {
		u.drawChordBox(c, rect{596, 6, 400, 46})
	}

	col := uint32(colRed)
	if u.midiOK {
		col = colGreen
	}
	if !u.Plugin {
		c.GlowCircle(1018, 29, 5, 9, col, 0.6)
		c.Circle(1018, 29, 5, col, 1)
	}
	u.button(c, "settings", rect{1036, 14, 148, 30}, "Paramètres", false, fontMdB, func() { u.OpenSettings() })
}

// calcule l'accord des touches tenues (seulement quand les touches changent)
func (u *UI) updateChord() {
	var key [2]uint64
	var notes []int
	for n := 0; n < 128; n++ {
		if u.counts[n] > 0 {
			key[n/64] |= 1 << uint(n%64)
			notes = append(notes, n)
		}
	}
	u.chordLive = len(notes) > 0
	if key == u.chordKey {
		return
	}
	u.chordKey = key
	if len(notes) > 0 {
		u.chord = u.namer.Detect(notes)
	}
}

func (u *UI) drawChordBox(c *Canvas, r rect) {
	u.updateChord()
	x, y, w, h := float64(r.x), float64(r.y), float64(r.w), float64(r.h)
	if u.chordLive {
		c.Glow(x, y, w, h, 12, 12, colGold, 0.28)
	}
	c.RRectGrad(x, y, w, h, 12, 0x03060a, 0x0b121d, 1)
	edge := uint32(0x2b3b57)
	if u.chordLive {
		edge = colGold
	}
	c.RRectStroke(x, y, w, h, 12, 1.5, edge, 1)
	c.Rect(r.x+14, r.y+2, r.w-28, 1, 0xffffff, 0.08)
	txt := u.chord.Name
	if txt == "" {
		txt = strings.Join(u.chord.Notes, "  ")
	}
	if txt == "" {
		c.Text(r.x, r.y, r.w, r.h, "JOUE UN ACCORD", fontSm, colText2, 1)
		return
	}
	col := uint32(colGoldHi)
	if !u.chordLive {
		col = 0x8c7a3c // le dernier accord reste affiché, plus discret
	}
	f := fontHg
	switch n := len([]rune(txt)); {
	case n > 26:
		f = fontMdB
	case n > 20:
		f = fontLg
	case n > 14:
		f = fontXl
	}
	c.Text(r.x+8, r.y, r.w-16, r.h, txt, f, col, 1)
}

func (u *UI) drawPresets(c *Canvas) {
	for i := 0; i < nPresets; i++ {
		i := i
		r := rect{16 + i*131, 68, 123, 44}
		on := u.cfg.Cur == i
		x, y, w, h := float64(r.x), float64(r.y), float64(r.w), float64(r.h)
		top, bot := uint32(0x1a2538), uint32(0x0f1625)
		if u.hover(r) {
			top, bot = 0x24324b, 0x15203a
		}
		if on {
			c.Glow(x, y, w, h, 9, 10, colGold, 0.3)
			top, bot = 0x2a2a1a, 0x16140c
		} else {
			c.Glow(x, y+1, w, h, 9, 5, 0x000000, 0.4)
		}
		c.RRectGrad(x, y, w, h, 9, top, bot, 1)
		if on {
			c.RRectStroke(x, y, w, h, 9, 2, colGoldHi, 1)
		} else {
			c.RRectStroke(x, y, w, h, 9, 1, 0x2b3b57, 1)
		}
		ic, numCol := uint32(0x9fb2d3), uint32(colText2)
		if on {
			ic, numCol = colGoldHi, colGold
		}
		// coin replié : une « fiche »
		fold := uint32(0x2b3b57)
		if on {
			fold = colGoldHi
		}
		c.Tri(x+w-16, y+1.5, x+w-1.5, y+1.5, x+w-1.5, y+16, fold, 0.9)
		c.Tri(x+w-16, y+1.5, x+w-16, y+16, x+w-1.5, y+16, 0x05080e, 0.55)
		drawIcon(c, icDownload, x+22, y+h/2, 20, ic, 1)
		c.Text(r.x+40, r.y+6, 80, 12, fmt.Sprintf("PRESET %d", i+1), fontXs, numCol, 0)
		c.Text(r.x+40, r.y+20, r.w-46, 20, u.cfg.Presets[i].Name, fontMdB, colText, 0)
		u.reg(region{r: r, id: fmt.Sprintf("preset%d", i),
			down: func(x, y int) { u.applyPreset(i) },
			dbl:  func() { u.openRename(i) }})
	}
	sr := rect{1064, 68, 120, 44}
	u.buttonC(c, "savepreset", sr, "Enregistrer", false, colGold, colDark, fontMdB, func() { u.savePreset() })
	c.GlowCircle(1078, 90, 5, 8, colRed, 0.5)
	c.Circle(1078, 90, 5.5, colRed, 1)
	c.Circle(1076.8, 88.6, 2, 0xffffff, 0.35)
}

func (u *UI) applyPreset(i int) {
	u.cfg.Cur = i
	u.applySetup(u.cfg.Presets[i].Chans)
	u.markDirty()
}

func (u *UI) savePreset() {
	i := u.cfg.Cur
	u.cfg.Presets[i].Chans = u.snapshot()
	u.markDirty()
	u.Toast(Tf("Preset %d « %s » enregistré", i+1, u.cfg.Presets[i].Name))
}

// niveau d'un vumètre, de 0 à 1
func meterLevel(p float64) float64 {
	if p < 0.0005 {
		return 0
	}
	return clamp01((20*math.Log10(p) + 54) / 54)
}

func dbText(v float64) string {
	if v <= -59.9 {
		return "-∞ dB"
	}
	return fmt.Sprintf("%+.1f dB", v)
}

func (u *UI) drawRow(c *Canvas, i int) {
	ch := u.eng.ch[i]
	x, y := rowX, rowY0+i*(rowH+rowGap)
	r := rect{x, y, rowW, rowH}
	sel := u.sel == i
	fx, fy, fw, fh := float64(x), float64(y), float64(rowW), float64(rowH)
	top, bot := uint32(0x16203a), uint32(0x0d1424)
	if sel {
		top, bot = 0x1e2c4a, 0x111b30
		c.Glow(fx, fy, fw, fh, 9, 11, colGold, 0.22)
	} else {
		c.Glow(fx, fy+1, fw, fh, 9, 6, 0x000000, 0.45)
	}
	c.RRectGrad(fx, fy, fw, fh, 9, top, bot, 1)
	if sel {
		c.RRectStroke(fx, fy, fw, fh, 9, 1.8, colGoldHi, 0.95)
	} else {
		c.RRectStroke(fx, fy, fw, fh, 9, 1, 0x26364f, 0.9)
	}
	c.Rect(x+10, y+1, rowW-20, 1, 0xffffff, 0.06)
	chSrc, _, _ := u.eng.ChInfo(i)
	empty := chSrc == nil && u.srcCfg[i].Kind == ""
	acc := trackCols[i]
	switch {
	case empty:
		acc = lerpColor(acc, 0x2a3446, 0.6)
	case ch.Mute:
		acc = lerpColor(acc, 0x2a3446, 0.55)
	}
	c.RRectGrad(float64(x+5), float64(y+6), 5, float64(rowH-12), 2.5, lerpColor(acc, 0xffffff, 0.3), acc, 1)
	if !empty && !ch.Mute {
		c.Glow(float64(x+5), float64(y+6), 5, float64(rowH-12), 2.5, 7, acc, 0.35)
	}
	c.Text(x+16, y, 24, rowH, fmt.Sprint(i+1), fontXl, acc, 1)

	_, name, cat := u.eng.ChInfo(i)
	if empty {
		name, cat = "Piste vide", "Double-clique pour charger"
	}
	if u.labels[i] != "" {
		if empty {
			name, cat = u.labels[i], "Vide · double-clique pour charger"
		} else {
			cat = name + " · " + cat
			name = u.labels[i]
		}
	}
	if u.isLoading(i) {
		cat = "CHARGEMENT…"
	}
	tc, ic := uint32(colText), uint32(0xdbe6fb)
	if ch.Mute || empty {
		tc, ic = colText2, 0x5d6b85
	}
	drawIcon(c, u.rowIcon(i, empty), float64(x+62), float64(y+rowH/2), 26, ic, 1)
	c.Text(x+86, y+7, 160, 20, name, fontMdB, tc, 0)
	c.Text(x+86, y+27, 160, 14, cat, fontXs, colText2, 0)

	u.reg(region{r: r, id: fmt.Sprintf("row%d", i),
		down: func(px, py int) { u.sel = i },
		dbl:  func() { u.sel = i; u.openSoundMenu(i) }})
	// la barre de couleur à gauche de la piste : un clic active / désactive la piste
	u.reg(region{r: rect{x, y, 16, rowH}, id: fmt.Sprintf("act%d", i),
		down: func(px, py int) {
			u.sel = i
			u.with(func() { ch.Mute = !ch.Mute })
			u.markDirty()
		}})
	u.reg(region{r: rect{x + 86, y + 4, 156, 20}, id: fmt.Sprintf("name%d", i),
		down: func(px, py int) { u.sel = i },
		dbl:  func() { u.sel = i; u.openRenameTrack(i) }})

	// panoramique
	c.Text(x+246, y+3, 44, 12, "PAN", fontXs, colText2, 1)
	u.knob(c, fmt.Sprintf("pan%d", i), float64(x+268), float64(y+29), 10,
		func() float64 { return (ch.Pan + 1) / 2 },
		func(v float64) {
			u.with(func() { ch.Pan = math.Round((v*2-1)*20) / 20 })
			u.markDirty()
		}, 0.5, true)

	// octave et transposition
	c.Text(x+300, y+3, 76, 12, "OCTAVE", fontXs, colText2, 1)
	u.stepper(c, fmt.Sprintf("oct%d", i), x+300, y+17, 76, 24, fmt.Sprintf("%+d", ch.Oct),
		func() { u.with(func() { ch.Oct = clamp(ch.Oct-1, -3, 3) }); u.markDirty() },
		func() { u.with(func() { ch.Oct = clamp(ch.Oct+1, -3, 3) }); u.markDirty() })
	c.Text(x+386, y+3, 76, 12, "TRANSPO", fontXs, colText2, 1)
	u.stepper(c, fmt.Sprintf("tr%d", i), x+386, y+17, 76, 24, fmt.Sprintf("%+d", ch.Trans),
		func() { u.with(func() { ch.Trans = clamp(ch.Trans-1, -12, 12) }); u.markDirty() },
		func() { u.with(func() { ch.Trans = clamp(ch.Trans+1, -12, 12) }); u.markDirty() })

	// muet, solo, maintien
	u.buttonC(c, fmt.Sprintf("mute%d", i), rect{x + 472, y + 17, 28, 24}, "M", ch.Mute, colRed, 0xffffff, fontMdB, func() {
		u.with(func() { ch.Mute = !ch.Mute })
		u.markDirty()
	})
	u.button(c, fmt.Sprintf("solo%d", i), rect{x + 508, y + 17, 28, 24}, "S", ch.Solo, fontMdB, func() {
		u.with(func() { ch.Solo = !ch.Solo })
		u.markDirty()
	})
	u.buttonC(c, fmt.Sprintf("hold%d", i), rect{x + 544, y + 17, 28, 24}, "H", ch.Hold, colBlue, colDark, fontMdB, func() {
		u.eng.SetHold(i, !ch.Hold)
		u.markDirty()
	})
	c.Text(x+470, y+3, 32, 12, "MUET", fontXs, colText2, 1)
	c.Text(x+506, y+3, 32, 12, "SOLO", fontXs, colText2, 1)
	c.Text(x+542, y+3, 32, 12, "HOLD", fontXs, colText2, 1)

	// volume et vumètre
	c.Text(x+582, y+3, 154, 12, "VOLUME", fontXs, colText2, 0)
	u.slider(c, fmt.Sprintf("vol%d", i), x+582, y+14, 154, 18, (ch.Vol+60)/66,
		func(v float64) {
			u.with(func() { ch.Vol = math.Round((v*66-60)*2) / 2 })
			u.markDirty()
		}, 60.0/66)
	c.RRectGrad(float64(x+582), float64(y+36), 154, 5, 2.5, 0x03060a, 0x0c1320, 1)
	if lv := meterLevel(ch.peak); lv > 0 {
		mc := uint32(colGreen)
		if lv > 0.93 {
			mc = colRed
		} else if lv > 0.8 {
			mc = colGoldHi
		}
		c.RRectHGrad(float64(x+582), float64(y+36), math.Max(5, 154*lv), 5, 2.5, lerpColor(mc, 0x000000, 0.45), mc, 1)
	}
	c.Text(x+742, y, 54, rowH, dbText(ch.Vol), fontSm, colText2, 2)
}

func (u *UI) drawPanel(c *Canvas) {
	ch := u.eng.ch[u.sel]
	px, py := panX, panY
	c.Glow(float64(px), float64(py+1), panW, panH, 12, 8, 0x000000, 0.45)
	c.RRectGrad(float64(px), float64(py), panW, panH, 12, 0x131d31, 0x0b121f, 1)
	c.RRectStroke(float64(px), float64(py), panW, panH, 12, 1, 0x2b3b57, 1)
	// grande icône du son, en filigrane doré (à la place de la photo)
	{
		chSrc0, _, _ := u.eng.ChInfo(u.sel)
		emp := chSrc0 == nil && u.srcCfg[u.sel].Kind == ""
		gx, gy := float64(px+panW-62), float64(py+40)
		drawIcon(c, u.rowIcon(u.sel, emp), gx, gy, 50, colGold, 0.55)
	}
	c.Text(px+16, py+12, 200, 12, Tf("RÉGLAGES · SON %d", u.sel+1), fontXs, colGold, 0)
	chSrc, name, cat := u.eng.ChInfo(u.sel)
	if chSrc == nil && u.srcCfg[u.sel].Kind == "" {
		name, cat = "Vide", "AUCUN SON"
	}
	c.Text(px+16, py+26, panW-100, 22, u.trackTitle(u.sel), fontLg, colText, 0)
	if chSrc == nil && u.srcCfg[u.sel].Kind == "" {
		cat = "AUCUN SON · CLIQUE SUR « CHARGER UN SON »"
	} else if u.labels[u.sel] != "" {
		cat = name + " · " + cat
	}
	c.Text(px+16, py+50, panW-100, 14, cat, fontXs, colText2, 0)
	sel := u.sel
	bw := (panW - 32 - 8) / 2
	u.button(c, "changeSound", rect{px + 16, py + 70, bw, 32}, "CHARGER UN SON", false, fontSm, func() {
		u.openSoundMenu(sel)
	})
	u.button(c, "renameTrack", rect{px + 24 + bw, py + 70, bw, 32}, "RENOMMER", false, fontSm, func() {
		u.openRenameTrack(sel)
	})
	for k := 0; k < nParam; k++ {
		k := k
		col, row := k%4, k/4
		cx := float64(px + 16 + col*80 + 40)
		cy := float64(py + 142 + row*82)
		u.knob(c, fmt.Sprintf("p%d", k), cx, cy, 20,
			func() float64 { return ch.P[k] },
			func(v float64) {
				u.with(func() { ch.P[k] = v })
				u.markDirty()
			}, paramDefault[k], false)
		c.Text(int(cx)-40, int(cy)+27, 80, 12, paramNames[k], fontXs, colText2, 1)
		c.Text(int(cx)-40, int(cy)+40, 80, 14, paramText(k, ch.P[k]), fontSm, colGold, 1)
	}
	tw := (panW - 32 - 24) / 4
	ty := py + 290
	u.button(c, "tgPedal", rect{px + 16, ty, tw, 28}, "PÉDALE", ch.Pedal, fontSm, func() {
		u.eng.SetPedalAware(sel, !ch.Pedal)
		u.markDirty()
	})
	u.button(c, "tgFilter", rect{px + 24 + tw, ty, tw, 28}, "FILTRE", ch.Filter, fontSm, func() {
		u.with(func() { ch.Filter = !ch.Filter })
		u.markDirty()
	})
	u.button(c, "tgModFX", rect{px + 32 + 2*tw, ty, tw, 28}, "MOD FX", ch.ModFX, fontSm, func() {
		u.with(func() { ch.ModFX = !ch.ModFX })
		u.markDirty()
		if ch.ModFX {
			u.Toast("Effet planant activé : la molette de droite (MOD) ouvre le son de cette piste.")
		}
	})
	u.button(c, "tgAT", rect{px + 40 + 3*tw, ty, tw, 28}, "AFTER", ch.AT && u.eng.atMode != 3, fontSm, func() {
		u.with(func() { ch.AT = !ch.AT })
		u.markDirty()
		if ch.AT && u.eng.atMode == 3 {
			u.Toast("L'aftertouch est désactivé dans les Paramètres.")
		}
	})
	fx := u.fxAt(u.sel)
	u.button(c, "btnEQ", rect{px + 16, py + 324, bw, 30}, "EQ", fx.EQ.On, fontSm, u.OpenEQ)
	fxLbl := "EFFETS"
	if n := u.fxActiveCount(); n > 0 {
		fxLbl = fmt.Sprintf("EFFETS · %d", n)
	}
	u.button(c, "btnFX", rect{px + 24 + bw, py + 324, bw, 30}, fxLbl, u.fxActiveCount() > 0, fontSm, u.OpenFXWin)
	c.Rect(px+16, py+364, panW-32, 1, 0x2b3b57, 1)
	c.Text(px+16, py+372, 110, 20, "Volume général", fontMd, colText, 0)
	u.slider(c, "master", px+130, py+372, 160, 20, u.eng.master,
		func(v float64) { u.eng.SetMaster(v); u.markDirty() }, 0.8)
	c.Text(px+296, py+372, 40, 20, fmt.Sprintf("%d%%", int(math.Round(u.eng.master*100))), fontSm, colGold, 2)
	c.Text(px+16, py+394, 110, 20, "Réverbération", fontMd, colText, 0)
	u.slider(c, "reverb", px+130, py+394, 160, 20, u.eng.revAmt,
		func(v float64) { u.eng.SetReverb(v); u.markDirty() }, 0.15)
	c.Text(px+296, py+394, 40, 20, fmt.Sprintf("%d%%", int(math.Round(u.eng.revAmt*100))), fontSm, colGold, 2)
}

func (u *UI) drawKeyboard(c *Canvas) {
	n := len(u.whites)
	fx, fy, fw, fh := float64(kbX-8), float64(kbY-8), float64(n*whiteW+16), float64(whiteH+14)
	c.Glow(fx, fy+2, fw, fh, 10, 10, 0x000000, 0.5)
	c.RRectGrad(fx, fy, fw, fh, 10, colGoldHi, colGoldDim, 1) // liseré doré
	c.RRectGrad(fx+1.5, fy+1.5, fw-3, fh-3, 9, 0x131a27, 0x05070b, 1)
	for _, w := range u.whites {
		x := u.whiteX[w]
		top, bot := uint32(colKeyW), uint32(0xcfc9b6)
		on := u.counts[w] > 0
		if on {
			top, bot = 0xffe27a, colGold
			c.Glow(float64(x), float64(kbY), whiteW-1, whiteH-4, 3, 14, colGold, 0.35)
		}
		c.VGrad(x, kbY, whiteW-1, whiteH-4, top, bot)
		c.RRect(float64(x), float64(kbY+whiteH-10), whiteW-1, 10, 4, bot, 1)
		if !on {
			c.Rect(x, kbY, 1, whiteH-4, 0xffffff, 0.6) // reflet à gauche
			c.Rect(x+whiteW-2, kbY, 1, whiteH-4, 0x000000, 0.18)
		}
		if w%12 == 0 {
			c.Text(x, kbY+whiteH-18, whiteW-1, 12, fmt.Sprintf("C%d", w/12-1), fontXs, 0x7a7566, 1)
		}
	}
	for _, b := range u.blacks {
		top, bot := uint32(0x2c323d), uint32(colKeyB)
		on := u.counts[b.note] > 0
		if on {
			top, bot = 0xffd23a, 0xb98200
			c.Glow(float64(b.r.x), float64(b.r.y), float64(b.r.w), float64(b.r.h), 3, 12, colGold, 0.4)
		} else {
			c.Glow(float64(b.r.x), float64(b.r.y+3), float64(b.r.w), float64(b.r.h), 3, 6, 0x000000, 0.45)
		}
		c.VGrad(b.r.x, b.r.y, b.r.w, b.r.h-3, top, bot)
		c.RRect(float64(b.r.x), float64(b.r.y+b.r.h-9), float64(b.r.w), 9, 3, bot, 1)
		if !on {
			c.Rect(b.r.x+2, b.r.y, 2, b.r.h-14, 0xffffff, 0.16) // reflet laqué
		}
	}
	u.reg(region{r: rect{kbX, kbY, n * whiteW, whiteH}, id: "keys",
		down: func(x, y int) {
			if k := u.keyAt(x, y); k >= 0 {
				u.mouseNote = k
				u.press(1, k, mouseVel(y))
			}
		},
		drag: func(x, y int) {
			k := u.keyAt(x, y)
			if k != u.mouseNote {
				if u.mouseNote >= 0 {
					u.release(1, u.mouseNote)
				}
				u.mouseNote = k
				if k >= 0 {
					u.press(1, k, mouseVel(y))
				}
			}
		},
		up: func(x, y int) {
			if u.mouseNote >= 0 {
				u.release(1, u.mouseNote)
				u.mouseNote = -1
			}
		}})
	u.drawWheels(c)
	hint := "Espace = sustain  ·  Échap = tout arrêter"
	c.Text(kbX-8, kbY+whiteH+10, 250, 14, hint, fontXs, colText2, 0)
	u.drawPlayer(c, 410, kbY+whiteH+9)
	pc, pt := uint32(0x2a3039), uint32(colText2)
	if u.eng.sustain {
		pc, pt = colGold, colDark
	}
	c.RRect(1090, float64(kbY+whiteH+8), 94, 18, 9, pc, 1)
	c.Text(1090, kbY+whiteH+8, 94, 18, "PÉDALE", fontXs, pt, 1)
}

// ---------------------------------------------------------------- lecteur audio

func fmtTime(t float64) string {
	n := int(t)
	return fmt.Sprintf("%d:%02d", n/60, n%60)
}

func (u *UI) drawPlayer(c *Canvas, x, y int) {
	pl := u.eng.Player
	name := "Lecteur audio…"
	if pl.Loaded() {
		name = pl.Name
	}
	u.button(c, "plOpen", rect{x, y, 90, 18}, name, false, fontXs, func() {
		p, ok := u.host.OpenFile("audio")
		if !ok {
			return
		}
		go func() {
			if err := pl.Load(p); err != nil {
				u.Toast(filepath.Base(p) + " : " + err.Error())
			} else {
				u.markDirty()
			}
		}()
	})
	if !pl.Loaded() {
		return
	}
	on := pl.Playing()
	lbl := "▶"
	if on {
		lbl = "II"
	}
	u.button(c, "plPlay", rect{x + 94, y, 28, 18}, lbl, on, fontSm, func() {
		if pl.Playing() {
			pl.Pause()
		} else {
			pl.Play()
		}
		u.markDirty()
	})
	u.button(c, "plStop", rect{x + 126, y, 28, 18}, "■", false, fontSm, func() { pl.Stop(); u.markDirty() })
	cur, tot := pl.Times()
	frac := 0.0
	if tot > 0 {
		frac = cur / tot
	}
	u.slider(c, "plPos", x+168, y, 80, 18, frac, func(v float64) { pl.Seek(v) }, 0)
	c.Text(x+262, y+2, 76, 14, fmtTime(cur)+" / "+fmtTime(tot), fontXs, colText2, 0)
	u.slider(c, "plVol", x+346, y, 44, 18, pl.Vol(), func(v float64) { pl.SetVol(v) }, 0.8)
}

// ---------------------------------------------------------------- molettes (pitch bend et modulation)

func (u *UI) wheel(c *Canvas, id string, r rect, v float64, lit bool, setFromY func(y int), release func()) {
	x, y, w, h := float64(r.x), float64(r.y), float64(r.w), float64(r.h)
	c.Glow(x, y+2, w, h, 8, 8, 0x000000, 0.5)
	c.RRectGrad(x-3, y-3, w+6, h+6, 9, 0x2b3850, 0x0a0f18, 1) // logement
	c.RRectGrad(x, y, w, h, 7, 0x070b12, 0x0e1523, 1)
	// la molette : des stries qui défilent avec la valeur
	off := math.Mod(v*h*1.7, 9)
	for yy := -9 + off; yy < h; yy += 9 {
		ly := y + yy
		if ly < y+3 || ly > y+h-4 {
			continue
		}
		shade := 0.5 + 0.5*math.Cos((ly-y)/h*math.Pi) // plus clair au centre : effet de rondeur
		c.Rect(r.x+3, int(ly), r.w-6, 2, lerpColor(0x3a465c, 0xc2cce0, shade*0.75), 1)
		c.Rect(r.x+3, int(ly)+2, r.w-6, 1, 0x000000, 0.45)
	}
	c.RRectGrad(x, y, w, h*0.22, 7, 0x000000, 0x000000, 0) // (réservé)
	c.Rect(r.x+1, r.y, r.w-2, int(h*0.18), 0x000000, 0.35)
	c.Rect(r.x+1, r.y+int(h*0.82), r.w-2, int(h*0.18), 0x000000, 0.35)
	// repère doré
	my := y + h*(1-v)
	col := uint32(colGoldHi)
	if lit {
		c.Glow(x, my-2, w, 4, 2, 9, colGold, 0.5)
	}
	c.RRect(x-2, my-2, w+4, 4, 2, col, 1)
	u.reg(region{r: rect{r.x - 4, r.y - 4, r.w + 8, r.h + 8}, id: id,
		down: func(px, py int) { setFromY(py) },
		drag: func(px, py int) { setFromY(py) },
		up: func(px, py int) {
			if release != nil {
				release()
			}
		},
		wheel: func(d int) { setFromY(int(my) - int(6*sgn(d))) }})
}

func (u *UI) drawWheels(c *Canvas) {
	h := float64(whiteH)
	fromY := func(r rect, y int) float64 { return clamp01(1 - (float64(y)-float64(r.y))/h) }
	pr := rect{50, kbY, 30, whiteH}
	mr := rect{94, kbY, 30, whiteH}
	bend := float64(u.eng.Bend())
	pv := 0.5 + bend/16384
	u.wheel(c, "wheelPitch", pr, pv, math.Abs(bend) > 200, func(y int) {
		v := fromY(pr, y)
		if math.Abs(v-0.5) < 0.04 { // zone morte au centre
			v = 0.5
		}
		u.eng.SetBend(int(math.Round((v - 0.5) * 2 * 8191)))
	}, func() { u.eng.SetBend(0) })
	mv := u.eng.ModWheel()
	u.wheel(c, "wheelMod", mr, mv, mv > 0.02, func(y int) { u.eng.SetMod(fromY(mr, y)) }, nil)
	c.Text(pr.x-8, kbY+whiteH+8, pr.w+16, 12, "PITCH", fontXs, colText2, 1)
	c.Text(mr.x-8, kbY+whiteH+8, mr.w+16, 12, "MOD", fontXs, colText2, 1)
}

func (u *UI) drawFooter(c *Canvas) {
	c.Rect(0, 706, c.W, 1, 0x1c2840, 1)
	c.VGrad(0, 707, c.W, 37, 0x0a101b, 0x05080e)
	mx := c.W / 2
	c.RRectHGrad(float64(mx-250), 718, 120, 2, 1, 0x05080e, colGold, 1)
	c.RRectHGrad(float64(mx+130), 718, 120, 2, 1, colGold, 0x05080e, 1)
	c.Text(0, 708, c.W, 20, "By JB Praise", fontLg, colGold, 1)
	c.Text(0, 727, c.W, 14, "for church musician", fontXs, colText2, 1)
	if u.licSt.State == LicDemo {
		r := rect{16, 712, 230, 26}
		u.buttonC(c, "licpill", r, "Démo : "+FormatDemoLeft(u.licSt.DemoLeft)+" — Activer", false, colGold, colDark, fontSm, func() { u.licShow = true })
	}
}

func (u *UI) drawToast(c *Canvas) {
	u.mu.Lock()
	msg := u.toast
	u.mu.Unlock()
	if msg == "" {
		return
	}
	w := len([]rune(msg))*7 + 48
	if w > 1000 {
		w = 1000
	}
	if w < 200 {
		w = 200
	}
	x := (c.W - w) / 2
	c.RRect(float64(x), 654, float64(w), 34, 8, 0x2a1a18, 0.97)
	c.RRectStroke(float64(x), 654, float64(w), 34, 8, 1.5, colRed, 1)
	c.Text(x+12, 654, w-24, 34, msg, fontMd, colText, 1)
}

// ---------------------------------------------------------------- fenêtres de choix

func (u *UI) drawOverlay(c *Canvas) {
	o := u.ov
	c.Rect(0, 0, c.W, c.H, 0x000000, 0.62)
	w, h := 640, 540
	if o.input {
		h = 170
	}
	x, y := (c.W-w)/2, 80
	if o.input {
		y = 200
	}
	u.reg(region{r: rect{0, 0, c.W, c.H}, id: "ovbg", down: func(int, int) { u.closeOverlay() }})
	u.reg(region{r: rect{x, y, w, h}, id: "ovpanel", down: func(int, int) {}})
	c.RRect(float64(x), float64(y), float64(w), float64(h), 12, colPanel, 1)
	c.RRectStroke(float64(x), float64(y), float64(w), float64(h), 12, 1.5, colGoldDim, 1)
	c.Text(x+20, y+14, w-40, 24, o.title, fontLg, colGold, 0)
	if o.back != nil && !o.input {
		back := o.back
		u.buttonC(c, "ovback", rect{x + w - 112, y + 10, 96, 30}, T("◀ Retour"), false, colGold, colDark, fontSm, back)
	}
	if o.custom != nil {
		o.custom(c, rect{x + 20, y + 52, w - 40, h - 90})
		c.Text(x+20, y+h-30, w-40, 16, o.hint, fontXs, colText2, 0)
		return
	}
	if o.input {
		c.RRect(float64(x+20), float64(y+56), float64(w-40), 36, 6, 0x0c0e11, 1)
		c.RRectStroke(float64(x+20), float64(y+56), float64(w-40), 36, 6, 1.5, colGold, 1)
		c.Text(x+30, y+56, w-60, 36, o.text+"|", fontMdB, colText, 0)
		c.Text(x+20, y+h-34, w-40, 16, o.hint, fontXs, colText2, 0)
		return
	}
	top := y + 48
	if o.filterable {
		c.RRect(float64(x+20), float64(y+46), float64(w-40), 30, 6, 0x0c0e11, 1)
		ft, fc := "Filtre : tape le nom d'un son…", uint32(colText2)
		if o.filter != "" {
			ft, fc = o.filter+"|", colText
		}
		c.Text(x+30, y+46, w-60, 30, ft, fontMd, fc, 0)
		top = y + 86
	}
	vis := o.visible()
	rowh := 28
	if o.sel >= 0 {
		o.clampScroll(vis)
	}
	if len(vis) == 0 {
		c.Text(x+20, top+10, w-40, 20, "Aucun son ne correspond.", fontMd, colText2, 0)
	}
	for k := 0; k < ovRows; k++ {
		idx := o.scroll + k
		if idx >= len(vis) {
			break
		}
		ii := vis[idx]
		it := o.items[ii]
		r := rect{x + 14, top + k*rowh, w - 40, rowh - 2}
		if it.header {
			c.Text(r.x+6, r.y+6, r.w, 16, strings.ToUpper(it.text), fontXs, colGold, 0)
			continue
		}
		if ii == o.sel {
			c.RRect(float64(r.x), float64(r.y), float64(r.w), float64(r.h), 6, 0x39414d, 1)
			c.RRectStroke(float64(r.x), float64(r.y), float64(r.w), float64(r.h), 6, 1.5, colGoldDim, 1)
		} else if r.has(u.mx, u.my) {
			c.RRect(float64(r.x), float64(r.y), float64(r.w), float64(r.h), 6, 0x2a3039, 1)
		}
		subW := 190
		if it.sub == "" {
			subW = 0
		}
		c.Text(r.x+10, r.y, r.w-subW-24, r.h, it.text, fontMd, colText, 0)
		if it.sub != "" {
			c.Text(r.x+r.w-subW-8, r.y, subW, r.h, it.sub, fontXs, colText2, 2)
		}
		ii2 := ii
		u.reg(region{r: r, id: fmt.Sprintf("ov%d", ii2), down: func(int, int) { u.overlayChoose(ii2) }})
	}
	if len(vis) > ovRows {
		th := ovRows * rowh
		c.RRect(float64(x+w-20), float64(top), 6, float64(th), 3, 0x0c0e11, 1)
		bh := math.Max(24, float64(th)*float64(ovRows)/float64(len(vis)))
		by := float64(top) + (float64(th)-bh)*float64(o.scroll)/float64(len(vis)-ovRows)
		c.RRect(float64(x+w-20), by, 6, bh, 3, colGoldDim, 1)
	}
	u.reg(region{r: rect{x, top, w, ovRows * rowh}, id: "ovwheel", wheel: func(d int) {
		o.scroll -= 3 * int(sgn(d))
		maxS := len(vis) - ovRows
		if maxS < 0 {
			maxS = 0
		}
		o.scroll = clamp(o.scroll, 0, maxS)
	}})
	c.Text(x+20, y+h-30, w-40, 16, o.hint, fontXs, colText2, 0)
}

// icône d'une piste : vide = note, sinon celle reconnue d'après le son
func (u *UI) rowIcon(i int, empty bool) int {
	if empty {
		return icNote
	}
	return u.icon(i)
}
