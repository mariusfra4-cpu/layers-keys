package main

// Petites icônes dessinées en vectoriel (aucune image à installer).

import "math"

const (
	icKeys = iota
	icUpright
	icCloud
	icWave
	icSquare
	icGuitar
	icPad
	icLayers
	icNote
	icPlus
	icBars
	icOrgan
	icStrings
	icBrass
	icVoice
	icBass
	icDrum
	icBell
	icWind
	icDownload
)

// icône centrée en (cx, cy), taille s (largeur), couleur col
func drawIcon(c *Canvas, kind int, cx, cy, s float64, col uint32, a float64) {
	t := math.Max(1.6, s/11)
	switch kind {
	case icKeys: // touches de piano
		w, h := s, s*0.78
		x0, y0 := cx-w/2, cy-h/2
		c.RRect(x0, y0, w, h, 2, col, a)
		kw := w / 5
		for i := 0; i < 4; i++ {
			c.Rect(int(x0+kw*float64(i+1)-0.5), int(y0+h*0.18), 1, int(h*0.82), 0x0b0f17, 0.55*a)
		}
		for _, i := range []int{0, 1, 3} {
			c.RRect(x0+kw*float64(i+1)-kw*0.28, y0, kw*0.56, h*0.58, 1, 0x0b0f17, 0.95*a)
		}
	case icUpright: // piano droit
		w, h := s*0.86, s*0.78
		x0, y0 := cx-w/2, cy-h/2
		c.RRect(x0, y0, w, h*0.55, 2, col, a)
		c.RRect(x0+w*0.1, y0+h*0.12, w*0.8, h*0.22, 1, 0x0b0f17, 0.55*a)
		c.Rect(int(x0+w*0.1), int(y0+h*0.55), int(w*0.8), int(h*0.14), col, a)
		c.Line(x0+w*0.15, y0+h*0.69, x0+w*0.15, y0+h, t, col, a)
		c.Line(x0+w*0.85, y0+h*0.69, x0+w*0.85, y0+h, t, col, a)
	case icCloud:
		c.Circle(cx-s*0.2, cy+s*0.02, s*0.2, col, a)
		c.Circle(cx+s*0.04, cy-s*0.12, s*0.26, col, a)
		c.Circle(cx+s*0.26, cy+s*0.04, s*0.19, col, a)
		c.RRect(cx-s*0.4, cy+s*0.02, s*0.8, s*0.2, s*0.1, col, a)
	case icWave: // sinusoïde en dents de scie douces
		pts := []float64{-0.5, 0, -0.32, -0.34, -0.14, 0.3, 0.04, -0.4, 0.2, 0.38, 0.36, -0.12, 0.5, 0}
		for i := 0; i+3 < len(pts); i += 2 {
			c.Line(cx+pts[i]*s, cy+pts[i+1]*s, cx+pts[i+2]*s, cy+pts[i+3]*s, t, col, a)
		}
	case icSquare:
		x := []float64{-0.5, -0.5, -0.5, -0.2, -0.2, 0.15, 0.15, 0.15, 0.15, 0.5}
		y := []float64{0.28, -0.28, -0.28, -0.28, 0.28, 0.28, 0.28, -0.28, -0.28, -0.28}
		_ = x
		_ = y
		c.Line(cx-0.5*s, cy+0.3*s, cx-0.5*s, cy-0.3*s, t, col, a)
		c.Line(cx-0.5*s, cy-0.3*s, cx-0.15*s, cy-0.3*s, t, col, a)
		c.Line(cx-0.15*s, cy-0.3*s, cx-0.15*s, cy+0.3*s, t, col, a)
		c.Line(cx-0.15*s, cy+0.3*s, cx+0.2*s, cy+0.3*s, t, col, a)
		c.Line(cx+0.2*s, cy+0.3*s, cx+0.2*s, cy-0.3*s, t, col, a)
		c.Line(cx+0.2*s, cy-0.3*s, cx+0.5*s, cy-0.3*s, t, col, a)
	case icGuitar:
		c.Line(cx+s*0.42, cy-s*0.42, cx+s*0.02, cy-s*0.02, t*1.1, col, a) // manche
		c.RRect(cx+s*0.34, cy-s*0.5, s*0.2, s*0.14, 2, col, a)            // tête
		c.Circle(cx-s*0.14, cy+s*0.12, s*0.27, col, a)                    // corps
		c.Circle(cx-s*0.02, cy-s*0.0, s*0.16, col, a)
		c.Circle(cx-s*0.14, cy+s*0.12, s*0.09, 0x0b0f17, 0.9*a) // rosace
	case icPad:
		w, h := s*0.9, s*0.7
		x0, y0 := cx-w/2, cy-h/2
		c.RRect(x0, y0, w, h, 3, col, a)
		for i := 0; i < 4; i++ {
			c.RRect(x0+w*0.08+float64(i)*w*0.22, y0+h*0.14, w*0.15, h*0.3, 1, 0x0b0f17, 0.8*a)
			c.RRect(x0+w*0.08+float64(i)*w*0.22, y0+h*0.56, w*0.15, h*0.3, 1, 0x0b0f17, 0.8*a)
		}
	case icLayers:
		for i := 0; i < 3; i++ {
			y := cy + (float64(i)-1)*s*0.2
			c.Tri(cx-s*0.5, y, cx, y-s*0.22, cx+s*0.5, y, col, a*(0.55+0.2*float64(i)))
			c.Tri(cx-s*0.5, y, cx, y+s*0.22, cx+s*0.5, y, col, a*(0.55+0.2*float64(i)))
		}
	case icNote:
		c.Circle(cx-s*0.17, cy+s*0.27, s*0.2, col, a)
		c.Line(cx+s*0.01, cy+s*0.22, cx+s*0.01, cy-s*0.4, t, col, a)
		c.Line(cx+s*0.01, cy-s*0.4, cx+s*0.36, cy-s*0.22, t*1.3, col, a)
		c.Line(cx+s*0.36, cy-s*0.22, cx+s*0.36, cy-s*0.04, t, col, a)
	case icPlus:
		c.Line(cx-s*0.35, cy, cx+s*0.35, cy, t, col, a)
		c.Line(cx, cy-s*0.35, cx, cy+s*0.35, t, col, a)
	case icOrgan: // tuyaux d'orgue
		hs := []float64{0.9, 0.68, 0.5, 0.68, 0.9}
		for i, hh := range hs {
			h := hh * s * 0.85
			x := cx - s*0.46 + float64(i)*s*0.2
			c.RRect(x, cy+s*0.42-h, s*0.15, h, 2, col, a)
			c.RRect(x+s*0.03, cy+s*0.42-h*0.34, s*0.09, h*0.1, 1, 0x0b0f17, 0.8*a)
		}
	case icStrings: // violon et archet
		c.Line(cx-s*0.12, cy-s*0.05, cx-s*0.12, cy-s*0.5, t, col, a)
		c.Circle(cx-s*0.12, cy-s*0.5, s*0.06, col, a)
		c.Circle(cx-s*0.12, cy-s*0.02, s*0.17, col, a)
		c.Circle(cx-s*0.12, cy+s*0.25, s*0.25, col, a)
		c.Line(cx-s*0.22, cy+s*0.2, cx-s*0.22, cy+s*0.34, t*0.7, 0x0b0f17, 0.8*a)
		c.Line(cx-s*0.02, cy+s*0.2, cx-s*0.02, cy+s*0.34, t*0.7, 0x0b0f17, 0.8*a)
		c.Line(cx+s*0.22, cy+s*0.46, cx+s*0.46, cy-s*0.42, t*0.8, col, a)
	case icBrass: // trompette
		c.Line(cx-s*0.48, cy+s*0.02, cx+s*0.22, cy+s*0.02, t*1.3, col, a)
		c.Tri(cx+s*0.18, cy+s*0.02, cx+s*0.5, cy-s*0.3, cx+s*0.5, cy+s*0.34, col, a)
		c.RRectStroke(cx-s*0.34, cy-s*0.32, s*0.52, s*0.34, s*0.16, t*0.9, col, a)
		for k := 0; k < 3; k++ {
			x := cx - s*0.12 + float64(k)*s*0.12
			c.Line(x, cy-s*0.28, x, cy-s*0.44, t, col, a)
		}
	case icVoice: // chœur
		c.Circle(cx-s*0.32, cy-s*0.1, s*0.11, col, 0.7*a)
		c.Circle(cx+s*0.32, cy-s*0.1, s*0.11, col, 0.7*a)
		c.RRect(cx-s*0.48, cy+s*0.02, s*0.26, s*0.36, s*0.1, col, 0.7*a)
		c.RRect(cx+s*0.22, cy+s*0.02, s*0.26, s*0.36, s*0.1, col, 0.7*a)
		c.Circle(cx, cy-s*0.2, s*0.16, col, a)
		c.RRect(cx-s*0.22, cy-s*0.0, s*0.44, s*0.42, s*0.16, col, a)
	case icBass: // guitare basse : long manche, deux micros
		c.Line(cx-s*0.02, cy+s*0.04, cx+s*0.46, cy-s*0.44, t*1.1, col, a)
		c.RRect(cx+s*0.36, cy-s*0.58, s*0.22, s*0.15, 2, col, a)
		c.Circle(cx-s*0.22, cy+s*0.2, s*0.24, col, a)
		c.Circle(cx-s*0.02, cy+s*0.02, s*0.13, col, a)
		c.Line(cx-s*0.3, cy+s*0.22, cx-s*0.14, cy+s*0.38, t*1.1, 0x0b0f17, 0.9*a)
		c.Line(cx-s*0.2, cy+s*0.1, cx-s*0.04, cy+s*0.26, t*1.1, 0x0b0f17, 0.9*a)
	case icDrum: // caisse claire et baguettes
		c.RRect(cx-s*0.44, cy-s*0.02, s*0.88, s*0.44, s*0.1, col, a)
		c.RRect(cx-s*0.44, cy-s*0.16, s*0.88, s*0.22, s*0.11, col, a)
		c.Line(cx-s*0.3, cy+s*0.02, cx-s*0.1, cy+s*0.4, t*0.7, 0x0b0f17, 0.6*a)
		c.Line(cx+s*0.1, cy+s*0.02, cx+s*0.3, cy+s*0.4, t*0.7, 0x0b0f17, 0.6*a)
		c.Line(cx-s*0.34, cy-s*0.52, cx+s*0.06, cy-s*0.22, t*0.9, col, a)
		c.Line(cx+s*0.34, cy-s*0.52, cx-s*0.06, cy-s*0.22, t*0.9, col, a)
	case icBell: // cloche
		c.Circle(cx, cy-s*0.38, s*0.07, col, a)
		c.Circle(cx, cy-s*0.06, s*0.3, col, a)
		c.RRect(cx-s*0.42, cy-s*0.06, s*0.84, s*0.32, s*0.1, col, a)
		c.Circle(cx, cy+s*0.36, s*0.09, col, a)
	case icWind: // flûte
		c.Line(cx-s*0.48, cy+s*0.3, cx+s*0.48, cy-s*0.3, t*2.2, col, a)
		for k := 0; k < 5; k++ {
			f := -0.1 + 0.2*float64(k)
			c.Circle(cx+f*s*0.95, cy-f*s*0.6, s*0.045, 0x0b0f17, 0.85*a)
		}
	case icDownload: // flèche vers un plateau : « enregistrer »
		c.Line(cx, cy-s*0.44, cx, cy+s*0.06, t*1.3, col, a)
		c.Tri(cx-s*0.27, cy-s*0.04, cx+s*0.27, cy-s*0.04, cx, cy+s*0.3, col, a)
		c.Line(cx-s*0.44, cy+s*0.2, cx-s*0.44, cy+s*0.46, t, col, a)
		c.Line(cx+s*0.44, cy+s*0.2, cx+s*0.44, cy+s*0.46, t, col, a)
		c.Line(cx-s*0.44, cy+s*0.46, cx+s*0.44, cy+s*0.46, t, col, a)
	case icBars:
		for i := 0; i < 5; i++ {
			h := []float64{0.3, 0.7, 1, 0.55, 0.35}[i] * s * 0.8
			c.RRect(cx-s*0.42+float64(i)*s*0.19, cy+s*0.4-h, s*0.12, h, 1.5, col, a)
		}
	}
}

// icône correspondant au type de son (d'après sa catégorie affichée)
func iconForCat(cat string, empty bool) int {
	if empty {
		return icNote
	}
	switch {
	case hasPrefixFold(cat, "INTÉGRÉ · PIANO"):
		return icKeys
	case hasPrefixFold(cat, "INTÉGRÉ · PAD"):
		return icPad
	case hasPrefixFold(cat, "INTÉGRÉ · LEAD"):
		return icWave
	case hasPrefixFold(cat, "INTÉGRÉ · GUITAR"), hasPrefixFold(cat, "INTÉGRÉ · BASS"):
		return icGuitar
	case hasPrefixFold(cat, "AUDIO"):
		return icBars
	case hasPrefixFold(cat, "SF2"):
		return icKeys
	}
	return icNote
}

func hasPrefixFold(s, p string) bool {
	if len(s) < len(p) {
		return false
	}
	return s[:len(p)] == p
}
