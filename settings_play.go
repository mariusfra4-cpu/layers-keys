package main

// Réglages du jeu au clavier MIDI : courbe de vélocité (une case où l'on tire la courbe) et aftertouch.

import (
	"fmt"
	"math"
)

var atModeNames = []string{"Vibrato + brillance", "Vibrato", "Brillance", "Désactivé"}
var atAmts = []float64{0.25, 0.5, 0.75, 1, 1.5, 2}

func (u *UI) atModeText() string { return T(atModeNames[clamp(u.cfg.ATMode, 0, 3)]) }

// description courte de la courbe actuelle
func (u *UI) velText() string {
	c := u.cfg
	switch {
	case c.VelLo == c.VelHi:
		return Tf("Fixe : %d", c.VelLo)
	case c.VelLo == 1 && c.VelHi == 127 && math.Abs(c.VelGamma-1) < 0.06:
		return T("Normale")
	case c.VelLo == 1 && c.VelHi == 127 && c.VelGamma > 1:
		return T("Douce")
	case c.VelLo == 1 && c.VelHi == 127:
		return T("Dure")
	}
	return T("Personnalisée")
}

func (u *UI) setVel(gamma float64, lo, hi int) {
	c := u.cfg
	c.VelGamma, c.VelLo, c.VelHi = gamma, lo, hi
	c.sanitize()
	u.eng.SetVelCurve(c.VelGamma, c.VelLo, c.VelHi)
	u.dirty = true
	u.host.Invalidate()
}

func (u *UI) cycleATMode() {
	u.cfg.ATMode = (u.cfg.ATMode + 1) % 4
	u.applyPlaying()
	u.dirty = true
	u.OpenSettings()
}

func (u *UI) cycleATAmt() {
	cur := 0
	for i, a := range atAmts {
		if math.Abs(a-u.cfg.ATAmt) < 0.01 {
			cur = i
		}
	}
	u.cfg.ATAmt = atAmts[(cur+1)%len(atAmts)]
	u.applyPlaying()
	u.dirty = true
	u.OpenSettings()
}

// écran de la courbe de vélocité
func (u *UI) OpenVelocity() {
	u.ov = &overlay{title: T("Courbe de vélocité"), hint: T("Tire la courbe avec la souris, ou choisis un réglage · Échap pour fermer"),
		back: u.OpenSettings, sel: -1}
	u.ov.custom = u.drawVelocity
	u.host.Invalidate()
}

func (u *UI) drawVelocity(c *Canvas, r rect) {
	cfg := u.cfg
	bx, by, bs := r.x+10, r.y+34, 300 // case carrée de la courbe
	fx, fy := float64(bx), float64(by)
	fs := float64(bs)
	c.RRect(fx-4, fy-4, fs+8, fs+8, 8, 0x05080e, 1)
	c.RRectStroke(fx-4, fy-4, fs+8, fs+8, 8, 1.5, colGoldDim, 1)
	for k := 1; k < 4; k++ {
		c.Rect(bx+k*bs/4, by, 1, bs, 0x26364f, 0.8)
		c.Rect(bx, by+k*bs/4, bs, 1, 0x26364f, 0.8)
	}
	c.Line(fx, fy+fs, fx+fs, fy, 1.2, 0x4a5a78, 0.8) // la droite « normale » en repère
	out := func(in float64) float64 {
		lo, hi := float64(cfg.VelLo)/127, float64(cfg.VelHi)/127
		return lo + (hi-lo)*math.Pow(in, cfg.VelGamma)
	}
	px, py := fx, fy+fs*(1-out(0))
	for k := 1; k <= 64; k++ {
		in := float64(k) / 64
		nx, ny := fx+fs*in, fy+fs*(1-out(in))
		c.Line(px, py, nx, ny, 2.6, colGoldHi, 1)
		px, py = nx, ny
	}
	// poignée au milieu de la courbe
	hx, hy := fx+fs*0.5, fy+fs*(1-out(0.5))
	c.GlowCircle(hx, hy, 9, 10, colGold, 0.55)
	c.Circle(hx, hy, 8, 0x05080e, 1)
	c.Circle(hx, hy, 6.5, colGoldHi, 1)
	// dernière frappe jouée sur le clavier
	vin, vout := int(u.lastVel[0]), int(u.lastVel[1])
	if vin > 0 {
		dx, dy := fx+fs*float64(vin)/127, fy+fs*(1-float64(vout)/127)
		c.Circle(dx, dy, 5, 0xffffff, 0.95)
		c.Circle(dx, dy, 3, colRed, 1)
	}
	c.Text(bx, by+bs+10, bs, 14, T("Force de ta frappe"), fontXs, colText2, 1)
	c.Text(bx-8, by-24, 200, 14, T("Son obtenu"), fontXs, colText2, 0)
	if vin > 0 {
		c.Text(bx, by+bs+28, bs, 16, Tf("Dernière frappe : %d → %d", vin, vout), fontSm, colGold, 1)
	} else {
		c.Text(bx, by+bs+28, bs, 16, T("Joue une note sur ton clavier : un point rouge apparaît."), fontXs, colText2, 1)
	}
	u.reg(region{r: rect{bx - 6, by - 6, bs + 12, bs + 12}, id: "velbox",
		down: func(x, y int) { u.dragVel(x, y, bx, by, bs) },
		drag: func(x, y int) { u.dragVel(x, y, bx, by, bs) }})

	// colonne de droite : réglages rapides et limites
	rx := r.x + 340
	rw := r.w - 350
	c.Text(rx, r.y+8, rw, 16, T("Réglage rapide"), fontXs, colGold, 0)
	bw := (rw - 8) / 2
	u.button(c, "velSoft", rect{rx, r.y + 30, bw, 30}, T("Douce"), u.velText() == T("Douce"), fontSm, func() { u.setVel(1.7, 1, 127) })
	u.button(c, "velNorm", rect{rx + bw + 8, r.y + 30, bw, 30}, T("Normale"), u.velText() == T("Normale"), fontSm, func() { u.setVel(1, 1, 127) })
	u.button(c, "velHard", rect{rx, r.y + 66, bw, 30}, T("Dure"), u.velText() == T("Dure"), fontSm, func() { u.setVel(0.62, 1, 127) })
	u.button(c, "velFixed", rect{rx + bw + 8, r.y + 66, bw, 30}, T("Fixe"), cfg.VelLo == cfg.VelHi, fontSm, func() { u.setVel(1, 100, 100) })
	c.Text(rx, r.y+112, rw, 14, T("Minimum"), fontXs, colGold, 0)
	c.Text(rx+rw-40, r.y+112, 40, 14, fmt.Sprint(cfg.VelLo), fontSm, colText, 2)
	u.slider(c, "velLo", rx+6, r.y+128, rw-18, 22, float64(cfg.VelLo-1)/126, func(v float64) {
		lo := 1 + int(math.Round(v*126))
		hi := cfg.VelHi
		if hi < lo {
			hi = lo
		}
		u.setVel(cfg.VelGamma, lo, hi)
	}, 0)
	c.Text(rx, r.y+162, rw, 14, T("Maximum"), fontXs, colGold, 0)
	c.Text(rx+rw-40, r.y+162, 40, 14, fmt.Sprint(cfg.VelHi), fontSm, colText, 2)
	u.slider(c, "velHi", rx+6, r.y+178, rw-18, 22, float64(cfg.VelHi-1)/126, func(v float64) {
		hi := 1 + int(math.Round(v*126))
		lo := cfg.VelLo
		if lo > hi {
			lo = hi
		}
		u.setVel(cfg.VelGamma, lo, hi)
	}, 1)
	c.Text(rx, r.y+220, rw, 14, T("Douce : le son reste doux, il faut frapper fort pour du volume."), fontXs, colText2, 0)
	c.Text(rx, r.y+238, rw, 14, T("Dure : une frappe légère sonne déjà fort."), fontXs, colText2, 0)
	c.Text(rx, r.y+256, rw, 14, T("Fixe : toutes les notes sonnent pareil (orgue)."), fontXs, colText2, 0)
	c.Text(rx, r.y+300, rw, 16, Tf("Courbe actuelle : %s", u.velText()), fontSm, colGold, 0)
}

// le point (x, y) de la case est un point par lequel la courbe doit passer
func (u *UI) dragVel(x, y, bx, by, bs int) {
	in := clampf(float64(x-bx)/float64(bs), 0.06, 0.94)
	yn := clampf(1-float64(y-by)/float64(bs), 0, 1)
	cfg := u.cfg
	lo, hi := float64(cfg.VelLo)/127, float64(cfg.VelHi)/127
	if hi-lo < 0.02 {
		return
	}
	t := clampf((yn-lo)/(hi-lo), 0.03, 0.97)
	u.setVel(math.Log(t)/math.Log(in), cfg.VelLo, cfg.VelHi)
}

func (u *UI) playingSettingsItems() []olItem {
	items := []olItem{{text: T("Jeu au clavier MIDI"), header: true}}
	items = append(items, olItem{text: T("Courbe de vélocité…"), sub: u.velText(), action: u.OpenVelocity})
	items = append(items, olItem{text: Tf("Aftertouch : %s", u.atModeText()), sub: T("touche pour changer"), action: u.cycleATMode})
	items = append(items, olItem{text: Tf("Intensité de l'aftertouch : %d %%", int(math.Round(u.cfg.ATAmt*100))), sub: T("touche pour changer"), action: u.cycleATAmt})
	items = append(items, olItem{text: T("MIDI Learn : potards et boutons…"), sub: Tf("%d association(s) · clic droit sur un réglage", len(u.learnMap())), action: u.openLearnList})
	return items
}
