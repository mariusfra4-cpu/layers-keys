package main

// Petit moteur de dessin 2D lissé (anti-aliasing) écrit à la main :
// il dessine dans un tableau de pixels, donc le même code sert à l'écran Windows
// et aux aperçus de test. Le texte est mis en file d'attente puis dessiné par Windows.

import "math"

type TextCmd struct {
	X, Y, W, H int
	S          string
	Font       int
	Col        uint32
	Align      int // 0 gauche, 1 centre, 2 droite
}

const (
	fontXs  = iota // 10 px gras (petites étiquettes)
	fontSm         // 11 px gras
	fontMd         // 13 px
	fontMdB        // 13 px gras
	fontLg         // 16 px gras
	fontXl         // 24 px gras
	fontHg         // 34 px gras (nom de l'accord)
	nFonts
)

type Canvas struct {
	W, H  int
	Pix   []uint32 // 0x00RRGGBB
	Texts []TextCmd
	// tous les textes déjà affichés (utile pour les aperçus de test)
	AllTexts []TextCmd
	// appelé avec les textes en attente quand on demande de les afficher (aperçus de test, Windows)
	FlushHook func(texts []TextCmd)
}

func NewCanvas(w, h int) *Canvas { return &Canvas{W: w, H: h, Pix: make([]uint32, w*h)} }

func (c *Canvas) Text(x, y, w, h int, s string, font int, col uint32, align int) {
	if s == "" {
		return
	}
	c.Texts = append(c.Texts, TextCmd{x, y, w, h, T(s), font, col, align})
}

func (c *Canvas) FlushText() {
	if c.FlushHook != nil {
		c.FlushHook(c.Texts)
	}
	c.AllTexts = append(c.AllTexts, c.Texts...)
	c.Texts = c.Texts[:0]
}

func mixColor(dst, src uint32, a float64) uint32 {
	if a >= 0.999 {
		return src
	}
	if a <= 0 {
		return dst
	}
	ia := int(a*256 + 0.5)
	dr, dg, db := int(dst>>16&255), int(dst>>8&255), int(dst&255)
	sr, sg, sb := int(src>>16&255), int(src>>8&255), int(src&255)
	r := dr + ((sr-dr)*ia)>>8
	g := dg + ((sg-dg)*ia)>>8
	b := db + ((sb-db)*ia)>>8
	return uint32(r)<<16 | uint32(g)<<8 | uint32(b)
}

func (c *Canvas) blend(x, y int, col uint32, a float64) {
	if x < 0 || y < 0 || x >= c.W || y >= c.H {
		return
	}
	i := y*c.W + x
	c.Pix[i] = mixColor(c.Pix[i], col, a)
}

func (c *Canvas) clipBox(x0, y0, x1, y1 *int) bool {
	if *x0 < 0 {
		*x0 = 0
	}
	if *y0 < 0 {
		*y0 = 0
	}
	if *x1 > c.W {
		*x1 = c.W
	}
	if *y1 > c.H {
		*y1 = c.H
	}
	return *x0 < *x1 && *y0 < *y1
}

// rectangle plein (pixels entiers), avec transparence a
func (c *Canvas) Rect(x, y, w, h int, col uint32, a float64) {
	x0, y0, x1, y1 := x, y, x+w, y+h
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	if a >= 0.999 {
		for py := y0; py < y1; py++ {
			row := c.Pix[py*c.W+x0 : py*c.W+x1]
			for i := range row {
				row[i] = col
			}
		}
		return
	}
	if a <= 0 {
		return
	}
	ia := int(a*256 + 0.5)
	sr, sg, sb := int(col>>16&255), int(col>>8&255), int(col&255)
	for py := y0; py < y1; py++ {
		row := c.Pix[py*c.W+x0 : py*c.W+x1]
		for i, d := range row {
			dr, dg, db := int(d>>16&255), int(d>>8&255), int(d&255)
			row[i] = uint32(dr+((sr-dr)*ia)>>8)<<16 | uint32(dg+((sg-dg)*ia)>>8)<<8 | uint32(db+((sb-db)*ia)>>8)
		}
	}
}

// dégradé vertical
func (c *Canvas) VGrad(x, y, w, h int, top, bottom uint32) {
	for i := 0; i < h; i++ {
		c.Rect(x, y+i, w, 1, mixColor(top, bottom, float64(i)/float64(maxi(h-1, 1))), 1)
	}
}

func rrectDist(px, py, cx, cy, hw, hh, r float64) float64 {
	qx := px - cx
	if qx < 0 {
		qx = -qx
	}
	qx -= hw
	qy := py - cy
	if qy < 0 {
		qy = -qy
	}
	qy -= hh
	if qx <= 0 && qy <= 0 {
		if qx > qy {
			return qx - r
		}
		return qy - r
	}
	ox, oy := qx, qy
	if ox < 0 {
		ox = 0
	}
	if oy < 0 {
		oy = 0
	}
	var d float64
	switch {
	case ox == 0:
		d = oy
	case oy == 0:
		d = ox
	default:
		d = math.Sqrt(ox*ox + oy*oy)
	}
	m := qx
	if qy > m {
		m = qy
	}
	if m > 0 {
		m = 0
	}
	return d + m - r
}

// intervalle de pixels entièrement couverts (couverture 1) sur une ligne « droite » d'un rectangle arrondi
func rrectInner(py int, x, y, w, h, r float64) (ia, ib int) {
	fy := float64(py) + 0.5
	if fy < y+r || fy > y+h-r {
		return 0, 0 // ligne dans la zone des coins : calcul complet
	}
	return int(math.Ceil(x + 0.5)), int(math.Floor(x + w - 0.5)) // pixels dont le centre est à plus d'un demi-pixel du bord
}

func cover(d float64) float64 {
	v := 0.5 - d
	if v <= 0 {
		return 0
	}
	if v >= 1 {
		return 1
	}
	return v
}

// colonnes [a, b) dont le centre est dans la partie « droite » du rectangle arrondi (loin des coins)
func midSpan(cx, hw float64, x0, x1 int) (int, int) {
	if hw <= 0 {
		return 0, 0
	}
	a := int(math.Ceil(cx - hw - 0.5))
	b := int(math.Floor(cx+hw-0.5)) + 1
	if a < x0 {
		a = x0
	}
	if b > x1 {
		b = x1
	}
	if b <= a {
		return 0, 0
	}
	return a, b
}

// fond une couleur sur les pixels [a, b) d'une ligne
func (c *Canvas) span(py, a, b int, col uint32, al float64) {
	if al <= 0 || py < 0 || py >= c.H {
		return
	}
	if a < 0 {
		a = 0
	}
	if b > c.W {
		b = c.W
	}
	if a >= b {
		return
	}
	row := c.Pix[py*c.W+a : py*c.W+b]
	if al >= 0.999 {
		for i := range row {
			row[i] = col
		}
		return
	}
	ia := int(al*256 + 0.5)
	sr, sg, sb := int(col>>16&255), int(col>>8&255), int(col&255)
	for i, d := range row {
		dr, dg, db := int(d>>16&255), int(d>>8&255), int(d&255)
		row[i] = uint32(dr+((sr-dr)*ia)>>8)<<16 | uint32(dg+((sg-dg)*ia)>>8)<<8 | uint32(db+((sb-db)*ia)>>8)
	}
}

// tronçon de ligne dont la couverture est constante : (début, fin, couverture) ; fin <= début = aucun
func rowSpan(py int, x0, x1 int, x, y, w, h, r, cx, cy, hw, hh float64) (int, int, float64) {
	ia, ib := rrectInner(py, x, y, w, h, r)
	if ib > ia {
		if ia < x0 {
			ia = x0
		}
		if ib > x1 {
			ib = x1
		}
		return ia, ib, 1
	}
	a, b := midSpan(cx, hw, x0, x1)
	if b <= a {
		return 0, 0, 0
	}
	return a, b, cover(rrectDist(float64(a)+0.5, float64(py)+0.5, cx, cy, hw, hh, r))
}

func (c *Canvas) RRect(x, y, w, h, r float64, col uint32, a float64) {
	if r > w/2 {
		r = w / 2
	}
	if r > h/2 {
		r = h / 2
	}
	cx, cy := x+w/2, y+h/2
	hw, hh := w/2-r, h/2-r
	x0, y0 := int(math.Floor(x))-1, int(math.Floor(y))-1
	x1, y1 := int(math.Ceil(x+w))+1, int(math.Ceil(y+h))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	for py := y0; py < y1; py++ {
		sa, sb, scv := rowSpan(py, x0, x1, x, y, w, h, r, cx, cy, hw, hh)
		for px := x0; px < x1; px++ {
			if px == sa && sb > sa {
				c.span(py, sa, sb, col, a*scv)
				px = sb - 1
				continue
			}
			cv := cover(rrectDist(float64(px)+0.5, float64(py)+0.5, cx, cy, hw, hh, r))
			if cv > 0 {
				c.blend(px, py, col, a*cv)
			}
		}
	}
}

// contour d'épaisseur t, dessiné à l'intérieur du rectangle
func (c *Canvas) RRectStroke(x, y, w, h, r, t float64, col uint32, a float64) {
	ix, iy, iw, ih := x+t/2, y+t/2, w-t, h-t
	ir := r - t/2
	if ir < 0 {
		ir = 0
	}
	if ir > iw/2 {
		ir = iw / 2
	}
	if ir > ih/2 {
		ir = ih / 2
	}
	cx, cy := ix+iw/2, iy+ih/2
	hw, hh := iw/2-ir, ih/2-ir
	x0, y0 := int(math.Floor(x))-1, int(math.Floor(y))-1
	x1, y1 := int(math.Ceil(x+w))+1, int(math.Ceil(y+h))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	band := int(math.Ceil(t)) + 2
	for py := y0; py < y1; py++ {
		fy := float64(py) + 0.5
		straight := fy > y+r && fy < y+h-r && fy > y+t+1 && fy < y+h-t-1
		ma, mb := 0, 0
		mcv := 0.0
		if !straight && math.Abs(fy-cy) > hh { // bord haut ou bas : la partie centrale a une couverture constante
			if ma, mb = midSpan(cx, hw, x0, x1); mb > ma {
				mcv = cover(math.Abs(rrectDist(float64(ma)+0.5, fy, cx, cy, hw, hh, ir)) - t/2)
			}
		}
		for px := x0; px < x1; px++ {
			if straight && px == x0+band {
				px = maxi(x1-band, px) - 1 // on saute l'intérieur : rien à dessiner
				continue
			}
			if px == ma && mb > ma {
				c.span(py, ma, mb, col, a*mcv)
				px = mb - 1
				continue
			}
			d := math.Abs(rrectDist(float64(px)+0.5, float64(py)+0.5, cx, cy, hw, hh, ir)) - t/2
			if cv := cover(d); cv > 0 {
				c.blend(px, py, col, a*cv)
			}
		}
	}
}

func (c *Canvas) Circle(cx, cy, r float64, col uint32, a float64) {
	x0, y0 := int(math.Floor(cx-r))-1, int(math.Floor(cy-r))-1
	x1, y1 := int(math.Ceil(cx+r))+1, int(math.Ceil(cy+r))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	for py := y0; py < y1; py++ {
		for px := x0; px < x1; px++ {
			dx, dy := float64(px)+0.5-cx, float64(py)+0.5-cy
			if cv := cover(math.Sqrt(dx*dx+dy*dy) - r); cv > 0 {
				c.blend(px, py, col, a*cv)
			}
		}
	}
}

func (c *Canvas) Ring(cx, cy, r, t float64, col uint32, a float64) {
	x0, y0 := int(math.Floor(cx-r-t))-1, int(math.Floor(cy-r-t))-1
	x1, y1 := int(math.Ceil(cx+r+t))+1, int(math.Ceil(cy+r+t))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	for py := y0; py < y1; py++ {
		for px := x0; px < x1; px++ {
			dx, dy := float64(px)+0.5-cx, float64(py)+0.5-cy
			if cv := cover(math.Abs(math.Sqrt(dx*dx+dy*dy)-r) - t/2); cv > 0 {
				c.blend(px, py, col, a*cv)
			}
		}
	}
}

// arc de cercle (angles en radians, sens horaire à l'écran, a1 > a0), extrémités arrondies
func (c *Canvas) Arc(cx, cy, r, a0, a1, t float64, col uint32, a float64) {
	if a1 < a0 {
		a0, a1 = a1, a0
	}
	span := a1 - a0
	if span <= 0 {
		return
	}
	x0, y0 := int(math.Floor(cx-r-t))-1, int(math.Floor(cy-r-t))-1
	x1, y1 := int(math.Ceil(cx+r+t))+1, int(math.Ceil(cy+r+t))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	e0x, e0y := cx+r*math.Cos(a0), cy+r*math.Sin(a0)
	e1x, e1y := cx+r*math.Cos(a1), cy+r*math.Sin(a1)
	for py := y0; py < y1; py++ {
		for px := x0; px < x1; px++ {
			fx, fy := float64(px)+0.5, float64(py)+0.5
			dx, dy := fx-cx, fy-cy
			rel := math.Atan2(dy, dx) - a0
			rel = math.Mod(rel, twoPi)
			if rel < 0 {
				rel += twoPi
			}
			var d float64
			if rel <= span || span >= twoPi {
				d = math.Abs(math.Sqrt(dx*dx+dy*dy) - r)
			} else {
				d = math.Min(math.Hypot(fx-e0x, fy-e0y), math.Hypot(fx-e1x, fy-e1y))
			}
			if cv := cover(d - t/2); cv > 0 {
				c.blend(px, py, col, a*cv)
			}
		}
	}
}

// segment d'épaisseur t, extrémités arrondies
func (c *Canvas) Line(ax, ay, bx, by, t float64, col uint32, a float64) {
	x0, y0 := int(math.Floor(math.Min(ax, bx)-t))-1, int(math.Floor(math.Min(ay, by)-t))-1
	x1, y1 := int(math.Ceil(math.Max(ax, bx)+t))+1, int(math.Ceil(math.Max(ay, by)+t))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	vx, vy := bx-ax, by-ay
	l2 := vx*vx + vy*vy
	for py := y0; py < y1; py++ {
		for px := x0; px < x1; px++ {
			fx, fy := float64(px)+0.5-ax, float64(py)+0.5-ay
			h := 0.0
			if l2 > 0 {
				h = math.Max(0, math.Min(1, (fx*vx+fy*vy)/l2))
			}
			d := math.Hypot(fx-vx*h, fy-vy*h)
			if cv := cover(d - t/2); cv > 0 {
				c.blend(px, py, col, a*cv)
			}
		}
	}
}

// ---------------------------------------------------------------- effets « réalistes »

func lerpColor(a, b uint32, t float64) uint32 { return mixColor(a, b, clamp01(t)) }

// rectangle arrondi rempli d'un dégradé vertical (top en haut, bottom en bas)
func (c *Canvas) RRectGrad(x, y, w, h, r float64, top, bottom uint32, a float64) {
	if r > w/2 {
		r = w / 2
	}
	if r > h/2 {
		r = h / 2
	}
	cx, cy := x+w/2, y+h/2
	hw, hh := w/2-r, h/2-r
	x0, y0 := int(math.Floor(x))-1, int(math.Floor(y))-1
	x1, y1 := int(math.Ceil(x+w))+1, int(math.Ceil(y+h))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	for py := y0; py < y1; py++ {
		col := lerpColor(top, bottom, (float64(py)+0.5-y)/h)
		sa, sb, scv := rowSpan(py, x0, x1, x, y, w, h, r, cx, cy, hw, hh)
		for px := x0; px < x1; px++ {
			if px == sa && sb > sa {
				c.span(py, sa, sb, col, a*scv)
				px = sb - 1
				continue
			}
			if cv := cover(rrectDist(float64(px)+0.5, float64(py)+0.5, cx, cy, hw, hh, r)); cv > 0 {
				c.blend(px, py, col, a*cv)
			}
		}
	}
}

// rectangle arrondi rempli d'un dégradé horizontal
func (c *Canvas) RRectHGrad(x, y, w, h, r float64, left, right uint32, a float64) {
	if r > w/2 {
		r = w / 2
	}
	if r > h/2 {
		r = h / 2
	}
	cx, cy := x+w/2, y+h/2
	hw, hh := w/2-r, h/2-r
	x0, y0 := int(math.Floor(x))-1, int(math.Floor(y))-1
	x1, y1 := int(math.Ceil(x+w))+1, int(math.Ceil(y+h))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	for py := y0; py < y1; py++ {
		sa, sb, scv := rowSpan(py, x0, x1, x, y, w, h, r, cx, cy, hw, hh)
		for px := x0; px < x1; px++ {
			if px == sa && sb > sa {
				if scv > 0 {
					for k := sa; k < sb; k++ {
						c.blend(k, py, lerpColor(left, right, (float64(k)+0.5-x)/w), a*scv)
					}
				}
				px = sb - 1
				continue
			}
			if cv := cover(rrectDist(float64(px)+0.5, float64(py)+0.5, cx, cy, hw, hh, r)); cv > 0 {
				c.blend(px, py, lerpColor(left, right, (float64(px)+0.5-x)/w), a*cv)
			}
		}
	}
}

// halo lumineux (ou ombre si la couleur est sombre) autour d'un rectangle arrondi
func (c *Canvas) Glow(x, y, w, h, r, spread float64, col uint32, a float64) {
	if r > w/2 {
		r = w / 2
	}
	if r > h/2 {
		r = h / 2
	}
	cx, cy := x+w/2, y+h/2
	hw, hh := w/2-r, h/2-r
	x0, y0 := int(math.Floor(x-spread))-1, int(math.Floor(y-spread))-1
	x1, y1 := int(math.Ceil(x+w+spread))+1, int(math.Ceil(y+h+spread))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	for py := y0; py < y1; py++ {
		ia, ib := rrectInner(py, x, y, w, h, r)
		ma, mb := 0, 0
		var md float64
		if ib <= ia { // ligne dans la zone des coins : la partie centrale a une distance constante
			if ma, mb = midSpan(cx, hw, x0, x1); mb > ma {
				md = rrectDist(float64(ma)+0.5, float64(py)+0.5, cx, cy, hw, hh, r)
			}
		}
		for px := x0; px < x1; px++ {
			if px == ia && ib > ia { // l'intérieur de la forme n'a pas de halo
				px = ib - 1
				continue
			}
			if px == ma && mb > ma {
				if md > 0 && md < spread {
					f := 1 - md/spread
					c.span(py, ma, mb, col, a*f*f)
				}
				px = mb - 1
				continue
			}
			d := rrectDist(float64(px)+0.5, float64(py)+0.5, cx, cy, hw, hh, r)
			if d <= 0 || d >= spread {
				continue
			}
			f := 1 - d/spread
			c.blend(px, py, col, a*f*f)
		}
	}
}

// halo rond
func (c *Canvas) GlowCircle(cx, cy, r, spread float64, col uint32, a float64) {
	x0, y0 := int(math.Floor(cx-r-spread))-1, int(math.Floor(cy-r-spread))-1
	x1, y1 := int(math.Ceil(cx+r+spread))+1, int(math.Ceil(cy+r+spread))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	for py := y0; py < y1; py++ {
		for px := x0; px < x1; px++ {
			dx, dy := float64(px)+0.5-cx, float64(py)+0.5-cy
			d := math.Sqrt(dx*dx+dy*dy) - r
			if d <= 0 || d >= spread {
				continue
			}
			f := 1 - d/spread
			c.blend(px, py, col, a*f*f)
		}
	}
}

// triangle plein (pour les icônes)
func (c *Canvas) Tri(ax, ay, bx, by, cx, cy float64, col uint32, a float64) {
	x0, y0 := int(math.Floor(math.Min(ax, math.Min(bx, cx))))-1, int(math.Floor(math.Min(ay, math.Min(by, cy))))-1
	x1, y1 := int(math.Ceil(math.Max(ax, math.Max(bx, cx))))+1, int(math.Ceil(math.Max(ay, math.Max(by, cy))))+1
	if !c.clipBox(&x0, &y0, &x1, &y1) {
		return
	}
	edge := func(px, py, x0, y0, x1, y1 float64) float64 {
		l := math.Hypot(x1-x0, y1-y0)
		if l == 0 {
			return 0
		}
		return ((x1-x0)*(py-y0) - (y1-y0)*(px-x0)) / l
	}
	sign := 1.0
	if edge(cx, cy, ax, ay, bx, by) < 0 {
		sign = -1
	}
	for py := y0; py < y1; py++ {
		for px := x0; px < x1; px++ {
			fx, fy := float64(px)+0.5, float64(py)+0.5
			d := math.Min(math.Min(sign*edge(fx, fy, ax, ay, bx, by), sign*edge(fx, fy, bx, by, cx, cy)), sign*edge(fx, fy, cx, cy, ax, ay))
			if cv := clamp01(d + 0.5); cv > 0 {
				c.blend(px, py, col, a*cv)
			}
		}
	}
}
