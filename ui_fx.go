package main

// Fenêtres « EQ » et « EFFETS » (ouvertes depuis le panneau de droite).

import (
	"fmt"
	"math"
)

// ---------------------------------------------------------------- accès aux réglages

// fxAt renvoie les réglages d'effets de la piste i (créés à la demande).
func (u *UI) fxAt(i int) *FXCfg {
	if i < 0 || i >= nCh {
		i = 0
	}
	for len(u.cfg.FXT) < nCh {
		u.cfg.FXT = append(u.cfg.FXT, nil)
	}
	if u.cfg.FXT[i] == nil {
		d := defaultFX()
		u.cfg.FXT[i] = &d
	}
	return u.cfg.FXT[i]
}

// fx : réglages de la piste dont les fenêtres EQ / EFFETS sont ouvertes (ou de la piste choisie)
func (u *UI) fx() *FXCfg { return u.fxAt(u.fxTr) }

// loadFX remplace les effets de toutes les pistes (nil = valeurs d'origine).
func (u *UI) loadFX(t []*FXCfg) {
	u.cfg.FXT = nil
	for i := 0; i < nCh; i++ {
		var cp FXCfg
		if i < len(t) && t[i] != nil {
			cp = *t[i]
		} else {
			cp = defaultFX()
		}
		cp.sanitize()
		u.cfg.FXT = append(u.cfg.FXT, &cp)
	}
	u.applyFX()
}

// applyFX envoie les réglages de chaque piste au moteur audio.
func (u *UI) applyFX() {
	for i := 0; i < nCh; i++ {
		u.eng.SetFX(i, *u.fxAt(i))
	}
}

// fxSet modifie un réglage d'effet de la piste en cours, le contrôle, l'envoie au moteur et marque le projet modifié.
func (u *UI) fxSet(f func(c *FXCfg)) {
	c := u.fx()
	f(c)
	c.sanitize()
	u.eng.SetFX(u.fxTr, *c)
	u.markDirty()
	u.host.Invalidate()
}

func (u *UI) fxReset(part string) {
	d := defaultFX()
	u.fxSet(func(c *FXCfg) {
		switch part {
		case "eq":
			on := c.EQ.On
			c.EQ = d.EQ
			c.EQ.On = on
		case "all":
			*c = d
		}
	})
}

func (u *UI) fxActiveCount() int {
	c := u.fxAt(u.sel)
	n := 0
	for _, b := range []bool{c.Comp.On, c.Chorus.On, c.Delay.On, c.Trem.On} {
		if b {
			n++
		}
	}
	return n
}

// ---------------------------------------------------------------- bouton rotatif à valeur réelle

func fxMap(v, lo, hi float64, log bool) float64 { // 0..1 → valeur
	if log {
		return lo * math.Pow(hi/lo, v)
	}
	return lo + (hi-lo)*v
}

func fxUnmap(x, lo, hi float64, log bool) float64 { // valeur → 0..1
	if log {
		return clamp01(math.Log(x/lo) / math.Log(hi/lo))
	}
	return clamp01((x - lo) / (hi - lo))
}

type fxK struct {
	id, label string
	lo, hi    float64
	log       bool
	def       float64
	bipolar   bool
	ptr       func(c *FXCfg) *float64
	text      func(v float64) string
}

func (u *UI) fxKnob(c *Canvas, cx, cy, r float64, k fxK) {
	cur := *k.ptr(u.fx())
	u.knob(c, k.id, cx, cy, r,
		func() float64 { return fxUnmap(*k.ptr(u.fx()), k.lo, k.hi, k.log) },
		func(v float64) { u.fxSet(func(f *FXCfg) { *k.ptr(f) = fxMap(v, k.lo, k.hi, k.log) }) },
		fxUnmap(k.def, k.lo, k.hi, k.log), k.bipolar)
	c.Text(int(cx)-34, int(cy+r+6), 68, 12, k.label, fontXs, colText2, 1)
	c.Text(int(cx)-34, int(cy+r+19), 68, 14, k.text(cur), fontSm, colGold, 1)
}

func hzText(v float64) string {
	if v >= 1000 {
		return fmt.Sprintf("%.1f kHz", v/1000)
	}
	return fmt.Sprintf("%d Hz", int(math.Round(v)))
}
func dbT(v float64) string     { return fmt.Sprintf("%+.1f dB", v) }
func msT(v float64) string     { return fmt.Sprintf("%d ms", int(math.Round(v))) }
func pctT(v float64) string    { return fmt.Sprintf("%d %%", int(math.Round(v*100))) }
func hzSlowT(v float64) string { return fmt.Sprintf("%.1f Hz", v) }
func ratioT(v float64) string  { return fmt.Sprintf("%.1f:1", v) }

// ---------------------------------------------------------------- fenêtre EQ

var eqNames = [4]string{"BASSES", "MÉDIUM 1", "MÉDIUM 2", "AIGUS"}

func eqBandPtr(c *FXCfg, i int) *EQBand {
	switch i {
	case 0:
		return &c.EQ.LS
	case 1:
		return &c.EQ.P1
	case 2:
		return &c.EQ.P2
	}
	return &c.EQ.HS
}

func (u *UI) OpenEQ() {
	u.fxTr = u.sel
	u.ov = &overlay{title: Tf("Égaliseur · %s", u.trackTitle(u.fxTr)), hint: T("Glisse les points · molette sur un point = largeur · double-clic = valeur d'origine · Échap"), sel: -1}
	u.ov.custom = u.drawEQ
	u.host.Invalidate()
}

func (u *UI) OpenFXWin() {
	u.fxTr = u.sel
	u.ov = &overlay{title: Tf("Effets · %s", u.trackTitle(u.fxTr)), hint: T("Allume un effet, puis tourne les potards · double-clic = valeur d'origine · Échap pour fermer"), sel: -1}
	u.ov.custom = u.drawFXWin
	u.host.Invalidate()
}

const (
	eqMinF, eqMaxF, eqRange = 20.0, 20000.0, 18.0
)

func (u *UI) drawEQ(c *Canvas, r rect) {
	fx := u.fx()
	eq := fx.EQ
	// boutons du haut
	u.button(c, "eqOn", rect{r.x, r.y - 6, 120, 28}, map[bool]string{true: "EQ ACTIVÉ", false: "EQ ÉTEINT"}[eq.On], eq.On, fontSm, func() {
		u.fxSet(func(f *FXCfg) { f.EQ.On = !f.EQ.On })
	})
	u.button(c, "eqReset", rect{r.x + 128, r.y - 6, 150, 28}, "TOUT REMETTRE À 0", false, fontSm, func() { u.fxReset("eq") })
	// graphique
	gx, gy, gw, gh := r.x, r.y+30, r.w, 150
	fgx, fgy, fgw, fgh := float64(gx), float64(gy), float64(gw), float64(gh)
	c.RRect(fgx-3, fgy-3, fgw+6, fgh+6, 8, 0x05080e, 1)
	c.RRectStroke(fgx-3, fgy-3, fgw+6, fgh+6, 8, 1.2, colGoldDim, 1)
	xOf := func(f float64) float64 { return fgx + fgw*math.Log(f/eqMinF)/math.Log(eqMaxF/eqMinF) }
	yOf := func(d float64) float64 { return fgy + fgh*(0.5-d/(2*eqRange)) }
	for _, d := range []float64{-12, -6, 0, 6, 12} {
		a := 0.35
		if d == 0 {
			a = 0.9
		}
		c.Rect(gx, int(yOf(d)), gw, 1, 0x26364f, a)
		c.Text(gx+3, int(yOf(d))-12, 40, 12, fmt.Sprintf("%+d", int(d)), fontXs, colText2, 0)
	}
	for _, f := range []float64{50, 100, 200, 500, 1000, 2000, 5000, 10000} {
		c.Rect(int(xOf(f)), gy, 1, gh, 0x26364f, 0.4)
		c.Text(int(xOf(f))-22, gy+gh-14, 44, 12, hzText(f), fontXs, colText2, 1)
	}
	// courbe de réponse
	secs := eq.sections()
	resp := func(f float64) float64 {
		m := 1.0
		for _, s := range secs {
			m *= s.mag(f)
		}
		return clampf(20*math.Log10(m+1e-12), -eqRange, eqRange)
	}
	var px, py float64
	for i := 0; i <= 240; i++ {
		f := eqMinF * math.Pow(eqMaxF/eqMinF, float64(i)/240)
		x, y := xOf(f), yOf(resp(f))
		if i > 0 {
			al := 1.0
			if !eq.On {
				al = 0.35
			}
			c.Line(px, py, x, y, 2.6, colGoldHi, al)
			c.Line(x, y, x, yOf(0), 1, colGold, 0.07*al)
		}
		px, py = x, y
	}
	// points
	for i := 0; i < 4; i++ {
		i := i
		b := *eqBandPtr(fx, i)
		hx, hy := xOf(b.F), yOf(b.G)
		sel := u.fxBand == i
		c.GlowCircle(hx, hy, 9, 9, colGold, 0.45)
		c.Circle(hx, hy, 9, 0x05080e, 1)
		col := uint32(0xdfe6f3)
		if sel {
			col = colGoldHi
		}
		c.Circle(hx, hy, 7.5, col, 1)
		c.Text(int(hx)-5, int(hy)-7, 10, 14, fmt.Sprint(i+1), fontXs, colDark, 1)
		set := func(x, y int) {
			u.fxBand = i
			f := eqMinF * math.Pow(eqMaxF/eqMinF, clamp01(float64(x-gx)/fgw))
			g := (0.5 - (float64(y)-fgy)/fgh) * 2 * eqRange
			u.fxSet(func(cc *FXCfg) { bb := eqBandPtr(cc, i); bb.F, bb.G = f, g })
		}
		u.reg(region{r: rect{int(hx) - 12, int(hy) - 12, 24, 24}, id: fmt.Sprintf("eqpt%d", i),
			down: set, drag: set,
			wheel: func(d int) {
				u.fxBand = i
				u.fxSet(func(cc *FXCfg) { bb := eqBandPtr(cc, i); bb.Q *= math.Pow(1.15, sgn(d)) })
			},
			dbl: func() { u.fxSet(func(cc *FXCfg) { eqBandPtr(cc, i).G = 0 }) }})
	}
	// tableau de potards : coupe-bas | 4 bandes | coupe-haut
	ky := r.y + 224
	colW := r.w / 6
	cx := func(k int) float64 { return float64(r.x + k*colW + colW/2) }
	c.Rect(r.x, ky-38, r.w, 1, 0x2b3b57, 1)
	c.Text(r.x, ky-32, colW, 12, "COUPE-BAS", fontXs, colGold, 1)
	u.fxKnob(c, cx(0), float64(ky), 15, fxK{id: "eqhp", label: "Fréquence", lo: 20, hi: 500, log: true, def: 20,
		ptr: func(f *FXCfg) *float64 { return &f.EQ.HP }, text: func(v float64) string {
			if v <= 25 {
				return "aucun"
			}
			return hzText(v)
		}})
	c.Text(r.x+5*colW, ky-32, colW, 12, "COUPE-HAUT", fontXs, colGold, 1)
	u.fxKnob(c, cx(5), float64(ky), 15, fxK{id: "eqlp", label: "Fréquence", lo: 2000, hi: 20000, log: true, def: 20000,
		ptr: func(f *FXCfg) *float64 { return &f.EQ.LP }, text: func(v float64) string {
			if v >= 19500 {
				return "aucun"
			}
			return hzText(v)
		}})
	flo := [4]float64{30, 80, 300, 2000}
	fhi := [4]float64{500, 4000, 12000, 16000}
	for i := 0; i < 4; i++ {
		i := i
		x0 := r.x + (i+1)*colW
		col := uint32(colGold)
		if u.fxBand == i {
			col = colGoldHi
		}
		c.Text(x0, ky-32, colW, 12, fmt.Sprintf("%d · %s", i+1, eqNames[i]), fontXs, col, 1)
		d := defaultFX()
		bd := *eqBandPtr(&d, i)
		u.fxKnob(c, cx(i+1), float64(ky), 15, fxK{id: fmt.Sprintf("eqg%d", i), label: "Gain", lo: -15, hi: 15, def: 0, bipolar: true,
			ptr: func(f *FXCfg) *float64 { return &eqBandPtr(f, i).G }, text: dbT})
		u.fxKnob(c, cx(i+1), float64(ky+66), 15, fxK{id: fmt.Sprintf("eqf%d", i), label: "Fréquence", lo: flo[i], hi: fhi[i], log: true, def: bd.F,
			ptr: func(f *FXCfg) *float64 { return &eqBandPtr(f, i).F }, text: hzText})
		u.fxKnob(c, cx(i+1), float64(ky+132), 15, fxK{id: fmt.Sprintf("eqq%d", i), label: "Largeur (Q)", lo: 0.3, hi: 8, log: true, def: bd.Q,
			ptr: func(f *FXCfg) *float64 { return &eqBandPtr(f, i).Q }, text: func(v float64) string { return fmt.Sprintf("%.1f", v) }})
	}
}

// ---------------------------------------------------------------- fenêtre EFFETS

func (u *UI) fxCard(c *Canvas, x, y, w, h int, title string, on bool, toggle func(), extra func(x, y int)) {
	c.RRectGrad(float64(x), float64(y), float64(w), float64(h), 8, 0x16213a, 0x0d1424, 1)
	c.RRectStroke(float64(x), float64(y), float64(w), float64(h), 8, 1, 0x2b3b57, 1)
	c.Text(x+12, y+8, w-110, 18, title, fontMdB, colText, 0)
	lbl := "ÉTEINT"
	if on {
		lbl = "ACTIF"
	}
	u.button(c, "fxon"+title, rect{x + w - 82, y + 6, 72, 24}, lbl, on, fontXs, toggle)
	if extra != nil {
		extra(x, y)
	}
}

func (u *UI) fxRow(c *Canvas, x, y, w int, ks []fxK) {
	n := len(ks)
	for i, k := range ks {
		u.fxKnob(c, float64(x+(2*i+1)*w/(2*n)), float64(y+72), 16, k)
	}
}

func (u *UI) drawFXWin(c *Canvas, r rect) {
	fx := u.fx()
	cw, ch := (r.w-10)/2, 138
	x1, x2 := r.x, r.x+cw+10
	y1 := r.y - 6
	y2, y3 := y1+ch+6, y1+2*(ch+6)

	u.fxCard(c, x1, y1, cw, ch, "Compresseur", fx.Comp.On, func() { u.fxSet(func(f *FXCfg) { f.Comp.On = !f.Comp.On }) }, nil)
	u.fxRow(c, x1, y1, cw, []fxK{
		{id: "cpT", label: "Seuil", lo: -50, hi: 0, def: -18, ptr: func(f *FXCfg) *float64 { return &f.Comp.Thresh }, text: func(v float64) string { return fmt.Sprintf("%d dB", int(math.Round(v))) }},
		{id: "cpR", label: "Taux", lo: 1, hi: 20, log: true, def: 3, ptr: func(f *FXCfg) *float64 { return &f.Comp.Ratio }, text: ratioT},
		{id: "cpA", label: "Attaque", lo: 0.5, hi: 100, log: true, def: 10, ptr: func(f *FXCfg) *float64 { return &f.Comp.Attack }, text: msT},
		{id: "cpL", label: "Relâche", lo: 20, hi: 1000, log: true, def: 160, ptr: func(f *FXCfg) *float64 { return &f.Comp.Release }, text: msT},
		{id: "cpG", label: "Gain", lo: 0, hi: 24, def: 3, ptr: func(f *FXCfg) *float64 { return &f.Comp.MakeupGain }, text: func(v float64) string { return fmt.Sprintf("+%.1f dB", v) }},
	})

	u.fxCard(c, x2, y1, cw, ch, "Chorus", fx.Chorus.On, func() { u.fxSet(func(f *FXCfg) { f.Chorus.On = !f.Chorus.On }) }, nil)
	u.fxRow(c, x2, y1, cw, []fxK{
		{id: "chR", label: "Vitesse", lo: 0.1, hi: 6, log: true, def: 0.8, ptr: func(f *FXCfg) *float64 { return &f.Chorus.Rate }, text: hzSlowT},
		{id: "chD", label: "Profondeur", lo: 0, hi: 1, def: 0.5, ptr: func(f *FXCfg) *float64 { return &f.Chorus.Depth }, text: pctT},
		{id: "chM", label: "Mélange", lo: 0, hi: 1, def: 0.4, ptr: func(f *FXCfg) *float64 { return &f.Chorus.Mix }, text: pctT},
	})

	u.fxCard(c, x1, y2, cw, ch, "Delay (écho)", fx.Delay.On, func() { u.fxSet(func(f *FXCfg) { f.Delay.On = !f.Delay.On }) }, nil)
	u.fxRow(c, x1, y2, cw, []fxK{
		{id: "dlT", label: "Durée", lo: 20, hi: 1500, log: true, def: 350, ptr: func(f *FXCfg) *float64 { return &f.Delay.Time }, text: msT},
		{id: "dlF", label: "Répétitions", lo: 0, hi: 0.9, def: 0.35, ptr: func(f *FXCfg) *float64 { return &f.Delay.Feedback }, text: pctT},
		{id: "dlM", label: "Mélange", lo: 0, hi: 1, def: 0.25, ptr: func(f *FXCfg) *float64 { return &f.Delay.Mix }, text: pctT},
		{id: "dlN", label: "Ton", lo: 500, hi: 16000, log: true, def: 5000, ptr: func(f *FXCfg) *float64 { return &f.Delay.Tone }, text: hzText},
	})

	pan := "AUTO-PAN : NON"
	if fx.Trem.Pan {
		pan = "AUTO-PAN : OUI"
	}
	u.fxCard(c, x2, y2, cw, ch, "Trémolo", fx.Trem.On, func() { u.fxSet(func(f *FXCfg) { f.Trem.On = !f.Trem.On }) }, func(x, y int) {
		u.button(c, "fxpan", rect{x + 88, y + 8, 112, 20}, pan, fx.Trem.Pan, fontXs, func() { u.fxSet(func(f *FXCfg) { f.Trem.Pan = !f.Trem.Pan }) })
	})
	u.fxRow(c, x2, y2, cw, []fxK{
		{id: "trR", label: "Vitesse", lo: 0.2, hi: 14, log: true, def: 5, ptr: func(f *FXCfg) *float64 { return &f.Trem.Rate }, text: hzSlowT},
		{id: "trD", label: "Profondeur", lo: 0, hi: 1, def: 0.5, ptr: func(f *FXCfg) *float64 { return &f.Trem.Depth }, text: pctT},
	})

	// réverbération : le réglage existant du moteur
	u.fxCard(c, x1, y3, cw, ch, "Réverbération", u.eng.revAmt > 0.02, func() {
		if u.eng.revAmt > 0.02 {
			u.eng.SetReverb(0)
		} else {
			u.eng.SetReverb(0.15)
		}
		u.markDirty()
	}, nil)
	u.knob(c, "fxrev", float64(x1+cw/2), float64(y3+72), 16,
		func() float64 { return u.eng.revAmt },
		func(v float64) { u.eng.SetReverb(v); u.markDirty() }, 0.15, false)
	c.Text(x1+cw/2-34, y3+72+22, 68, 12, "Quantité", fontXs, colText2, 1)
	c.Text(x1+cw/2-34, y3+72+35, 68, 14, pctT(u.eng.revAmt), fontSm, colGold, 1)
	c.Text(x1+8, y3+ch-18, cw-16, 12, "commune à toutes les pistes", fontXs, colText2, 1)

	// bas à droite : ordre des effets + remise à zéro
	c.RRect(float64(x2), float64(y3), float64(cw), float64(ch), 8, 0x0d1424, 1)
	c.Text(x2+12, y3+10, cw-24, 16, "Ordre du signal", fontXs, colGold, 0)
	c.Text(x2+12, y3+28, cw-24, 14, "Compresseur → Égaliseur → Chorus", fontXs, colText2, 0)
	c.Text(x2+12, y3+43, cw-24, 14, "→ Trémolo → Delay → Réverbération", fontXs, colText2, 0)
	u.button(c, "fxEQ", rect{x2 + 12, y3 + 72, cw - 24, 26}, "OUVRIR L'ÉGALISEUR", fx.EQ.On, fontSm, u.OpenEQ)
	u.button(c, "fxAll", rect{x2 + 12, y3 + 104, cw - 24, 26}, "CETTE PISTE : TOUT À ZÉRO", false, fontSm, func() {
		u.fxReset("all")
	})
}
