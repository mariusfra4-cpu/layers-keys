package main

// Piano tactile (Android) : grosses touches faites pour les doigts, plusieurs doigts à la fois,
// défilement sur 5 octaves, couches, pédale, volume. Même moteur et mêmes sons que le reste du logiciel.

import (
	"fmt"
)

type pianoState struct {
	first float64 // touche blanche (numéro) qui touche le bord gauche
	vis   int     // nombre de touches blanches visibles
	kb    rect    // zone du clavier
	ww    float64 // largeur d'une touche blanche
}

func (s *pianoState) init() {
	s.vis = 14
	s.first = 7 // Do 3
}

func (d *droidApp) pianoClamp() {
	s := &d.pn
	s.vis = clamp(s.vis, 7, 24)
	max := float64(len(d.ui.whites) - s.vis)
	if s.first > max {
		s.first = max
	}
	if s.first < 0 {
		s.first = 0
	}
}

var frNames = [12]string{"DO", "", "RÉ", "", "MI", "FA", "", "SOL", "", "LA", "", "SI"}

func (d *droidApp) pianoBlack(i int) bool { // une touche noire à droite de la blanche i ?
	if i < 0 || i+1 >= len(d.ui.whites) {
		return false
	}
	switch d.ui.whites[i] % 12 {
	case 0, 2, 5, 7, 9:
		return true
	}
	return false
}

func (d *droidApp) pianoKeyAt(x, y int) int {
	s := &d.pn
	if !s.kb.has(x, y) {
		return -1
	}
	u := d.ui
	bh := s.kb.h * 62 / 100
	fx := float64(x)/s.ww + s.first
	idx := int(fx)
	if y < s.kb.y+bh {
		// touches noires : centrées sur la jonction entre deux blanches
		for _, i := range []int{idx - 1, idx} {
			if d.pianoBlack(i) {
				cx := (float64(i+1) - s.first) * s.ww
				if float64(x) >= cx-s.ww*0.32 && float64(x) < cx+s.ww*0.32 {
					return u.whites[i] + 1
				}
			}
		}
	}
	if idx >= 0 && idx < len(u.whites) {
		return u.whites[idx]
	}
	return -1
}

func pianoVel(y int, kb rect) int {
	v := 50 + 77*(y-kb.y)/maxi(kb.h, 1)
	return clamp(v, 45, 127)
}

func (d *droidApp) drawPiano(c *Canvas) {
	u, s := d.ui, &d.pn
	W, H := c.W, c.H
	d.pianoClamp()
	u.regions = u.regions[:0]
	c.VGrad(0, 0, W, H, 0x0a101b, 0x05080e)
	// ---- rangée du haut : retour, couches, accord, pédale
	u.button(c, "pback", rect{8, 8, 150, 52}, "◀  MIXAGE", false, fontMdB, func() { d.setPiano(false) })
	for i := 0; i < nCh; i++ {
		src, _, _ := u.eng.ChInfo(i)
		ch := u.eng.ch[i]
		on := src != nil && !ch.Mute
		i := i
		u.button(c, fmt.Sprintf("pl%d", i), rect{168 + i*64, 8, 60, 52}, fmt.Sprint(i+1), on, fontLg, func() {
			u.sel = i
			u.with(func() { ch.Mute = !ch.Mute })
			u.markDirty()
		})
	}
	u.drawChordBox(c, rect{690, 8, 250, 52})
	u.buttonC(c, "ppedal", rect{950, 8, 242, 52}, "PÉDALE (sustain)", u.eng.sustain, colGold, colDark, fontMdB, func() {
		u.eng.SetSustain(!u.eng.sustain)
	})
	// ---- deuxième rangée : défilement, taille, volume
	u.button(c, "pleft", rect{8, 68, 64, 52}, "◀", false, fontLg, func() { s.first -= 7; d.pianoClamp() })
	u.button(c, "pright", rect{780, 68, 64, 52}, "▶", false, fontLg, func() { s.first += 7; d.pianoClamp() })
	mm := rect{80, 68, 692, 52}
	d.drawMinimap(c, mm)
	u.button(c, "pzoomm", rect{860, 68, 56, 52}, "–", false, fontLg, func() { s.vis += 2; d.pianoClamp() })
	c.Text(918, 68, 64, 52, "TAILLE", fontXs, colText2, 1)
	u.button(c, "pzoomp", rect{984, 68, 56, 52}, "+", false, fontLg, func() { s.vis -= 2; d.pianoClamp() })
	c.Text(1050, 68, 142, 18, "VOLUME", fontXs, colText2, 1)
	u.slider(c, "master", 1060, 90, 122, 24, u.eng.master, func(v float64) { u.eng.SetMaster(v); u.markDirty() }, 0.8)
	// ---- clavier
	top := 130
	s.kb = rect{0, top, W, H - top - 6}
	s.ww = float64(W) / float64(s.vis)
	kh := s.kb.h
	bh := kh * 62 / 100
	c.Rect(0, top-4, W, 4, 0x000000, 0.5)
	first := int(s.first)
	for i := first; i <= first+s.vis && i < len(u.whites); i++ {
		n := u.whites[i]
		x0 := int((float64(i) - s.first) * s.ww)
		x1 := int((float64(i+1) - s.first) * s.ww)
		on := u.counts[n] > 0
		topc, botc := uint32(0xfdfbf4), uint32(0xe6e0d2)
		if on {
			topc, botc = colGoldHi, colGold
		}
		c.VGrad(x0+1, top, x1-x0-2, kh, topc, botc)
		c.Rect(x0, top, 1, kh, 0x8a8576, 0.6)
		if nm := frNames[n%12]; nm != "" {
			lbl := nm
			col := uint32(0x6b6759)
			if n%12 == 0 {
				lbl = fmt.Sprintf("%s %d", nm, n/12-1)
				col = 0x2a2518
			}
			c.Text(x0, top+kh-34, x1-x0, 26, lbl, fontMdB, col, 1)
		}
	}
	for i := first; i <= first+s.vis && i < len(u.whites); i++ {
		if !d.pianoBlack(i) {
			continue
		}
		n := u.whites[i] + 1
		cx := (float64(i+1) - s.first) * s.ww
		bw := s.ww * 0.62
		on := u.counts[n] > 0
		tc, bc := uint32(0x2a2f38), uint32(0x07090d)
		if on {
			tc, bc = colGoldHi, colGoldDim
		}
		c.RRectGrad(cx-bw/2, float64(top-2), bw, float64(bh+2), 7, tc, bc, 1)
		if !on {
			c.Rect(int(cx-bw/2)+4, top, 3, bh-14, 0xffffff, 0.14)
		}
	}
	u.drawToast(c)
	c.FlushText()
}

// petite vue d'ensemble des 5 octaves : on y fait glisser la fenêtre visible
func (d *droidApp) drawMinimap(c *Canvas, r rect) {
	u, s := d.ui, &d.pn
	nw := len(u.whites)
	c.RRectGrad(float64(r.x), float64(r.y), float64(r.w), float64(r.h), 8, 0x070b12, 0x0e1623, 1)
	c.RRectStroke(float64(r.x), float64(r.y), float64(r.w), float64(r.h), 8, 1, 0x26364f, 0.8)
	w := float64(r.w-8) / float64(nw)
	for i, n := range u.whites {
		x := float64(r.x+4) + float64(i)*w
		col := uint32(0x39445a)
		if u.counts[n] > 0 {
			col = colGold
		}
		c.Rect(int(x)+1, r.y+8, maxi(int(w)-1, 1), r.h-16, col, 1)
		if n%12 == 0 {
			c.Text(int(x), r.y+r.h-20, 40, 14, fmt.Sprintf("DO%d", n/12-1), fontXs, colText2, 0)
		}
	}
	vx := float64(r.x+4) + s.first*w
	c.RRectStroke(vx, float64(r.y+3), float64(s.vis)*w, float64(r.h-6), 6, 3, colGold, 1)
	c.Rect(int(vx), r.y+3, int(float64(s.vis)*w), r.h-6, colGold, 0.12)
	set := func(x, _ int) {
		s.first = (float64(x-r.x-4) - float64(s.vis)*w/2) / w
		d.pianoClamp()
	}
	u.reg(region{r: r, id: "pmap", down: set, drag: set})
}

// ---------------------------------------------------------------- doigts

func (d *droidApp) pianoDown(id int, p *dPtr, x, y int) {
	if d.pn.kb.has(x, y) {
		p.kb = true
		if k := d.pianoKeyAt(x, y); k >= 0 {
			p.note = k
			d.ui.press(3000+id, k, pianoVel(y, d.pn.kb))
			d.noteOnCount()
			d.Invalidate()
		}
		return
	}
	d.mainDown(id, p, x, y)
	d.Invalidate()
}

func (d *droidApp) pianoMove(id int, p *dPtr, x, y int) {
	if p.kb {
		k := d.pianoKeyAt(x, y)
		if k != p.note {
			if p.note >= 0 {
				d.ui.release(3000+id, p.note)
			}
			p.note = k
			if k >= 0 {
				d.ui.press(3000+id, k, pianoVel(y, d.pn.kb))
				d.noteOnCount()
			}
			d.Invalidate()
		}
		return
	}
	d.mainMove(id, p, x, y)
	d.Invalidate()
}

func (d *droidApp) pianoUp(id int, p *dPtr, x, y int) {
	if p.kb {
		if p.note >= 0 {
			d.ui.release(3000+id, p.note)
			p.note = -1
		}
		d.Invalidate()
		return
	}
	d.mainUp(id, p, x, y)
	d.Invalidate()
}
