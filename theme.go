package main

// Thèmes de l'interface et couleur des écritures (Personnaliser l'interface).
// Chaque couleur dessinée est rangée dans une famille : accent (l'or), fond (le bleu nuit), écriture (blanc bleuté).
// Le thème change la teinte de chaque famille en gardant la lumière : l'ensemble reste harmonieux et lisible.

import (
	"math"
	"sync"
)

type uiTheme struct {
	name             string
	accH, accS       float64 // accent : nouvelle teinte (degrés, <0 = garder) et facteur de saturation
	accV             float64 // facteur de lumière de l'accent
	bgH, bgS, bgV    float64 // fonds et cadres
	light            bool    // thème clair : fonds clairs, écritures foncées
	swatchA, swatchB uint32  // pastille dans l'écran de choix (fond, accent)
}

var uiThemes = []uiTheme{
	{"Or & Bleu nuit", -1, 1, 1, -1, 1, 1, false, colPanel, colGold}, // d'origine
	{"Saphir", 208, 0.85, 1, 214, 1.1, 1, false, 0x0d1830, 0x38a1ff},
	{"Émeraude", 148, 0.8, 0.92, 168, 0.8, 0.95, false, 0x0a1a17, 0x2fd583},
	{"Rubis", 354, 0.85, 0.95, 350, 0.7, 1, false, 0x1a0d12, 0xe8414f},
	{"Violet royal", 272, 0.7, 1, 262, 0.85, 1.05, false, 0x140f24, 0xa77bff},
	{"Graphite & Argent", 215, 0.12, 1.05, 220, 0.12, 1.08, false, 0x16181c, 0xd3d9e3},
	{"Orange & Charbon", 26, 1, 1, 25, 0.1, 1.05, false, 0x171513, 0xff8a1f},
	{"Clair", -1, 1, 0.78, 215, 0.3, 1, true, 0xeef1f6, 0xc99600},
}

type textTheme struct {
	name string
	h, s float64 // teinte et saturation des écritures (s<0 = garder)
	col  uint32  // pastille
}

var textThemes = []textTheme{
	{"Blanc", -1, -1, colText}, // d'origine
	{"Crème", 44, 0.16, 0xf3ead2},
	{"Gris clair", 0, 0, 0xd5d8dd},
	{"Or", 45, 0.55, 0xf4d77a},
	{"Bleu glacier", 198, 0.25, 0xcfe9f7},
	{"Vert menthe", 150, 0.22, 0xcdf3df},
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
	case s >= 0.45 && h >= 28 && h <= 62 && v >= 0.25: // accent (or)
		if th.accH >= 0 {
			h = th.accH
		}
		return hsvToRGB(h, s*th.accS, v*th.accV)
	case h >= 190 && h <= 245 && s > 0.06 && v < 0.55 && s < 0.8: // fonds, cadres, lignes (bleu nuit)
		if th.light {
			return hsvToRGB(th.bgH, s*th.bgS, 0.985-v*0.55)
		}
		if th.bgH >= 0 {
			h = th.bgH
		}
		return hsvToRGB(h, s*th.bgS, v*th.bgV)
	case s < 0.3 && v >= 0.5 && (s < 0.06 || (h >= 185 && h <= 245)): // écritures (blanc bleuté, gris bleu)
		if th.light {
			v = 1.02 - v
		}
		if tt.s >= 0 {
			h, s = tt.h, tt.s*(0.6+0.4*v)
			if th.light {
				s = tt.s * 1.6
			}
		} else if th.light {
			h, s = 218, s+0.1
		}
		return hsvToRGB(h, s, v)
	case th.light && s < 0.06 && v < 0.12 && col != 0: // presque noir (texte sur bouton doré) : inchangé
		return col
	}
	return col
}
