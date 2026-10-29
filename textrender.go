//go:build !windows

package main

// Texte dessiné par le programme lui-même (polices intégrées) : utilisé sur Mac (et pour les aperçus de test).
// Sous Windows, c'est GDI qui dessine le texte ; ici on obtient le même rendu sur toutes les machines.

import (
	_ "embed"
	"image"
	"sync"

	"golang.org/x/image/font"
	"golang.org/x/image/font/opentype"
	"golang.org/x/image/font/sfnt"
	"golang.org/x/image/math/fixed"
)

//go:embed fonts/Inter-Regular.otf
var fontRegularData []byte

//go:embed fonts/Inter-Bold.otf
var fontBoldData []byte

//go:embed fonts/DejaVuSans.ttf
var fontSymbolData []byte

type glyphImg struct {
	ok      bool
	dx, dy  int // position du coin haut-gauche par rapport au point d'écriture
	w, h    int
	alpha   []uint8
	advance fixed.Int26_6
}

type textFace struct {
	face  font.Face
	font  *sfnt.Font
	cache map[uint32]*glyphImg // clé : rune<<2 | quart de pixel
	asc   fixed.Int26_6
	desc  fixed.Int26_6
}

type textStyle struct{ main, alt *textFace }

// TextRenderer dessine les TextCmd d'un Canvas dans ses pixels.
type TextRenderer struct {
	styles [nFonts]textStyle
	mu     sync.Mutex
	buf    sfnt.Buffer
}

var (
	trOnce sync.Once
	trInst *TextRenderer
)

// SharedTextRenderer : un seul rendu de texte pour tout le programme (les polices ne sont lues qu'une fois).
func SharedTextRenderer() *TextRenderer {
	trOnce.Do(func() { trInst = newTextRenderer() })
	return trInst
}

func newTextFace(f *sfnt.Font, size float64) *textFace {
	face, err := opentype.NewFace(f, &opentype.FaceOptions{Size: size, DPI: 72, Hinting: font.HintingNone})
	if err != nil {
		return nil
	}
	m := face.Metrics()
	return &textFace{face: face, font: f, cache: map[uint32]*glyphImg{}, asc: m.Ascent, desc: m.Descent}
}

func newTextRenderer() *TextRenderer {
	r := &TextRenderer{}
	reg, e1 := opentype.Parse(fontRegularData)
	bold, e2 := opentype.Parse(fontBoldData)
	sym, e3 := opentype.Parse(fontSymbolData)
	if e1 != nil || e2 != nil || e3 != nil {
		return r // sans polices : aucun texte, mais pas de plantage
	}
	spec := [nFonts]struct {
		px   float64
		bold bool
	}{{10, true}, {11, true}, {13, false}, {13, true}, {16, true}, {24, true}, {34, true}}
	for i, s := range spec {
		f := reg
		if s.bold {
			f = bold
		}
		r.styles[i] = textStyle{main: newTextFace(f, s.px), alt: newTextFace(sym, s.px)}
	}
	return r
}

// glyph : l'image d'une lettre (mise en mémoire), posée à un quart de pixel près
func (r *TextRenderer) glyph(tf *textFace, ru rune, quarter int) *glyphImg {
	key := uint32(ru)<<2 | uint32(quarter)
	if g, ok := tf.cache[key]; ok {
		return g
	}
	g := &glyphImg{}
	dot := fixed.Point26_6{X: fixed.Int26_6(quarter * 16), Y: 0}
	dr, mask, maskp, adv, ok := tf.face.Glyph(dot, ru)
	if ok {
		g.ok = true
		g.advance = adv
		g.dx, g.dy = dr.Min.X, dr.Min.Y
		g.w, g.h = dr.Dx(), dr.Dy()
		if g.w > 0 && g.h > 0 && mask != nil {
			g.alpha = make([]uint8, g.w*g.h)
			for y := 0; y < g.h; y++ {
				for x := 0; x < g.w; x++ {
					_, _, _, a := mask.At(maskp.X+x, maskp.Y+y).RGBA()
					g.alpha[y*g.w+x] = uint8(a >> 8)
				}
			}
		}
	}
	tf.cache[key] = g
	return g
}

// la police qui possède cette lettre (Inter, sinon DejaVu pour les symboles ◀ ▶ ■ …)
func (r *TextRenderer) faceFor(st textStyle, ru rune) *textFace {
	if st.main == nil {
		return nil
	}
	if idx, err := st.main.font.GlyphIndex(&r.buf, ru); err == nil && idx != 0 {
		return st.main
	}
	if st.alt != nil {
		if idx, err := st.alt.font.GlyphIndex(&r.buf, ru); err == nil && idx != 0 {
			return st.alt
		}
	}
	return st.main
}

func (r *TextRenderer) advance(st textStyle, s []rune) fixed.Int26_6 {
	var w fixed.Int26_6
	for _, ru := range s {
		tf := r.faceFor(st, ru)
		if tf == nil {
			continue
		}
		w += r.glyph(tf, ru, 0).advance
	}
	return w
}

// Draw dessine un texte : une ligne, centrée en hauteur dans son rectangle, "…" si trop long.
func (r *TextRenderer) Draw(c *Canvas, t TextCmd) {
	if t.Font < 0 || t.Font >= nFonts || t.S == "" {
		return
	}
	st := r.styles[t.Font]
	if st.main == nil {
		return
	}
	rs := []rune(t.S)
	maxW := fixed.I(t.W)
	if t.W > 0 && r.advance(st, rs) > maxW {
		ell := []rune{'…'}
		ew := r.advance(st, ell)
		for len(rs) > 0 && r.advance(st, rs)+ew > maxW {
			rs = rs[:len(rs)-1]
		}
		rs = append(rs, '…')
	}
	width := r.advance(st, rs)
	x := fixed.I(t.X)
	switch t.Align {
	case 1:
		x += (fixed.I(t.W) - width) / 2
	case 2:
		x += fixed.I(t.W) - width
	}
	lineH := st.main.asc + st.main.desc
	base := fixed.I(t.Y) + (fixed.I(t.H)-lineH)/2 + st.main.asc
	by := base.Round()
	cr, cg, cb := int(t.Col>>16&255), int(t.Col>>8&255), int(t.Col&255)
	for _, ru := range rs {
		tf := r.faceFor(st, ru)
		if tf == nil {
			continue
		}
		q := int((x & 63) >> 4) // quart de pixel
		g := r.glyph(tf, ru, q)
		if g.ok && len(g.alpha) > 0 {
			gx := int(x>>6) + g.dx
			gy := by + g.dy
			for yy := 0; yy < g.h; yy++ {
				py := gy + yy
				if py < 0 || py >= c.H {
					continue
				}
				for xx := 0; xx < g.w; xx++ {
					a := int(g.alpha[yy*g.w+xx])
					if a == 0 {
						continue
					}
					px := gx + xx
					if px < 0 || px >= c.W {
						continue
					}
					i := py*c.W + px
					d := c.Pix[i]
					dr, dg, db := int(d>>16&255), int(d>>8&255), int(d&255)
					c.Pix[i] = uint32(dr+((cr-dr)*a+127)/255)<<16 | uint32(dg+((cg-dg)*a+127)/255)<<8 | uint32(db+((cb-db)*a+127)/255)
				}
			}
		}
		x += g.advance
	}
}

// Attach branche ce rendu de texte sur un Canvas (appelé à chaque FlushText).
func (r *TextRenderer) Attach(c *Canvas) {
	c.FlushHook = func(texts []TextCmd) {
		r.mu.Lock()
		for _, t := range texts {
			r.Draw(c, t)
		}
		r.mu.Unlock()
	}
}

var _ = image.Point{}
