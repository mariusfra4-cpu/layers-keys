package main

// Thèmes de l'interface et couleur des écritures (Personnaliser l'interface).
// Chaque couleur dessinée est rangée dans une famille : accent (l'or), fond (le bleu nuit), écriture (blanc bleuté).
// Le thème change la teinte de chaque famille en gardant la lumière : l'ensemble reste harmonieux et lisible.

import (
	"math"
	"sync"
)

type uiTheme struct {
	name          string
	desc          string  // ambiance, affichée sous le nom
	accH, accS    float64 // accent (l'or d'origine) : teinte et saturation visées (accH < 0 = garder)
	accV          float64 // lumière de l'accent (facteur)
	bgH, bgS, bgV float64 // fonds et cadres : teinte, saturation visée, lumière (facteur)
	txH, txS      float64 // teinte et saturation des écritures avec ce thème (txS < 0 = garder)
}

// harmonies à deux tons, comme le matériel de studio : un fond profond et nuancé, un accent « métal » ou pastel
var uiThemes = []uiTheme{
	{"Or & Bleu nuit", "l'original", -1, 0, 1, -1, 0, 1, -1, -1},
	{"Champagne & Ébène", "luxe, piano de concert", 40, 0.34, 0.97, 30, 0.16, 1.05, 38, 0.06},
	{"Bordeaux & Or rose", "chaleureux, velours", 14, 0.5, 0.98, 344, 0.55, 1.05, 12, 0.07},
	{"Vert anglais & Laiton", "console vintage", 43, 0.62, 0.9, 162, 0.5, 1.0, 50, 0.07},
	{"Bleu pétrole & Cuivre", "studio moderne", 21, 0.66, 0.97, 194, 0.62, 1.0, 190, 0.07},
	{"Prune & Lavande", "doux, élégant", 266, 0.38, 1.0, 286, 0.42, 1.08, 270, 0.08},
	{"Ardoise & Glacier", "frais, épuré", 193, 0.36, 1.0, 212, 0.3, 1.12, 200, 0.06},
	{"Minuit & Corail", "scène, lumière chaude", 9, 0.58, 1.0, 232, 0.55, 1.0, 230, 0.05},
}

type textTheme struct {
	name string
	h, s float64 // teinte et saturation des écritures (s<0 = garder)
	col  uint32  // pastille
}

var textThemes = []textTheme{
	{"Assortie au thème", -1, -1, colText}, // d'origine
	{"Ivoire", 44, 0.1, 0xf6f0e1},
	{"Champagne", 38, 0.24, 0xf1dfbd},
	{"Perle", 220, 0.03, 0xe3e5ea},
	{"Or doux", 44, 0.42, 0xf2d891},
	{"Glacier", 198, 0.2, 0xd6ecf7},
}

var (
	thMu    sync.Mutex
	thCache = map[uint32]uint32{}
	thKey   [2]int
)

func themeIdentity() bool { return look.theme == 0 && look.text == 0 }

func rgbToHSV(c uint32) (h, s, v float64) {
	r, g, b := float64(c>>16&255)/255, float64(c>>8&255)/255, float64(c&255)/255
	mx, mn := math.Max(r, math.Max(g, b)), math.Min(r, math.Min(g, b))
	v = mx
	d := mx - mn
	if mx > 0 {
		s = d / mx
	}
	if d == 0 {
		return 0, s, v
	}
	switch mx {
	case r:
		h = math.Mod((g-b)/d, 6)
	case g:
		h = (b-r)/d + 2
	default:
		h = (r-g)/d + 4
	}
	h *= 60
	if h < 0 {
		h += 360
	}
	return
}

func hsvToRGB(h, s, v float64) uint32 {
	s, v = clamp01(s), clamp01(v)
	h = math.Mod(h, 360)
	if h < 0 {
		h += 360
	}
	c := v * s
	x := c * (1 - math.Abs(math.Mod(h/60, 2)-1))
	m := v - c
	var r, g, b float64
	switch {
	case h < 60:
		r, g, b = c, x, 0
	case h < 120:
		r, g, b = x, c, 0
	case h < 180:
		r, g, b = 0, c, x
	case h < 240:
		r, g, b = 0, x, c
	case h < 300:
		r, g, b = x, 0, c
	default:
		r, g, b = c, 0, x
	}
	to := func(f float64) uint32 { return uint32(math.Round((f + m) * 255)) }
	return to(r)<<16 | to(g)<<8 | to(b)
}

func themeColor(col uint32) uint32 {
	thMu.Lock()
	defer thMu.Unlock()
	if k := [2]int{look.theme, look.text}; k != thKey {
		thKey, thCache = k, map[uint32]uint32{}
	}
	if o, ok := thCache[col]; ok {
		return o
	}
	o := mapThemeColor(col, uiThemes[clamp(look.theme, 0, len(uiThemes)-1)], textThemes[clamp(look.text, 0, len(textThemes)-1)])
	if len(thCache) < 8192 {
		thCache[col] = o
	}
	return o
}

func mapThemeColor(col uint32, th uiTheme, tt textTheme) uint32 {
	h, s, v := rgbToHSV(col)
	switch {
	case s >= 0.45 && h >= 25 && h <= 62: // accent (or, ambre, brun doré)
		if th.accH < 0 {
			return col
		}
		return hsvToRGB(th.accH, th.accS*math.Min(1, s/0.95), math.Min(1, v*th.accV))
	case h >= 190 && h <= 245 && s > 0.06 && v < 0.55 && s < 0.8: // fonds, cadres, lignes (bleu nuit)
		if th.bgH < 0 {
			return col
		}
		return hsvToRGB(th.bgH, th.bgS*math.Min(1.3, s/0.6), v*th.bgV)
	case s < 0.3 && v >= 0.5 && (s < 0.06 || (h >= 185 && h <= 245)): // écritures (blanc bleuté, gris bleu)
		switch {
		case tt.s >= 0:
			return hsvToRGB(tt.h, tt.s*(0.6+0.4*v), v)
		case th.txS >= 0:
			return hsvToRGB(th.txH, th.txS+s*0.5, v)
		}
	}
	return col
}
