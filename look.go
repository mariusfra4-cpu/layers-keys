package main

// Personnalisation (Paramètres > Personnalisation) : couleur des notes jouées, des touches blanches et noires,
// et police des lettres. Réglages du logiciel (pas du projet), communs à toutes les fenêtres.

import "fmt"

// traductions des textes de la Personnalisation
func init() {
	for k, v := range map[string][2]string{
		"Personnalisation": {"Customization", "Personalización"},
		"Couleurs du clavier et police des lettres…":                                            {"Keyboard colors and font…", "Colores del teclado y tipo de letra…"},
		"Clique sur une couleur ou une police : le changement est immédiat · Échap pour fermer": {"Click a color or a font: the change is immediate · Esc to close", "Haz clic en un color o un tipo de letra: el cambio es inmediato · Esc para cerrar"},
		"Couleur des notes jouées":                                                              {"Color of played notes", "Color de las notas tocadas"},
		"Couleur des touches blanches":                                                          {"White keys color", "Color de las teclas blancas"},
		"Couleur des touches noires":                                                            {"Black keys color", "Color de las teclas negras"},
		"Police des lettres":                                                                    {"Font", "Tipo de letra"},
		"Aperçu":                                                                                {"Preview", "Vista previa"},
		"Couleurs et police d'origine":                                                          {"Original colors and font", "Colores y letra originales"},
		"Or":                                                                                    {"Gold", "Oro"},
		"Bleu":                                                                                  {"Blue", "Azul"},
		"Turquoise":                                                                             {"Turquoise", "Turquesa"},
		"Vert":                                                                                  {"Green", "Verde"},
		"Rouge":                                                                                 {"Red", "Rojo"},
		"Rose":                                                                                  {"Pink", "Rosa"},
		"Violet":                                                                                {"Purple", "Violeta"},
		"Orange":                                                                                {"Orange", "Naranja"},
		"Argent":                                                                                {"Silver", "Plata"},
		"Ivoire":                                                                                {"Ivory", "Marfil"},
		"Blanc pur":                                                                             {"Pure white", "Blanco puro"},
		"Crème":                                                                                 {"Cream", "Crema"},
		"Gris perle":                                                                            {"Pearl grey", "Gris perla"},
		"Noir":                                                                                  {"Black", "Negro"},
		"Graphite":                                                                              {"Graphite", "Grafito"},
		"Bois":                                                                                  {"Wood", "Madera"},
		"Bleu nuit":                                                                             {"Midnight blue", "Azul noche"},
		"Moderne":                                                                               {"Modern", "Moderna"},
		"Classique":                                                                             {"Classic", "Clásica"},
		"Lisible":                                                                               {"Easy to read", "Legible"},
		"Machine à écrire":                                                                      {"Typewriter", "Máquina de escribir"},
	} {
		if _, ok := trTable[k]; !ok {
			trTable[k] = v
		}
	}
}

// couleur des notes jouées
type lookOnCol struct {
	name                  string
	base, light, bHi, bLo uint32 // touche blanche : light → base ; touche noire : bHi → bLo
}

// couleur d'un jeu de touches (dégradé du haut vers le bas)
type lookKeyCol struct {
	name     string
	top, bot uint32
	label    uint32 // texte « C4 » sur les touches blanches
	dark     bool   // touches foncées : reflet plus discret
}

func mkOnCol(name string, base uint32) lookOnCol {
	return lookOnCol{name, base, mixColor(base, 0xffffff, 0.45), mixColor(base, 0xffffff, 0.2), mixColor(base, 0x000000, 0.25)}
}

var lookOnCols = []lookOnCol{
	{"Or", colGold, 0xffe27a, 0xffd23a, 0xb98200}, // couleur d'origine
	mkOnCol("Bleu", 0x38a1ff),
	mkOnCol("Turquoise", 0x22c7b8),
	mkOnCol("Vert", 0x3ddc84),
	mkOnCol("Rouge", 0xe03a3a),
	mkOnCol("Rose", 0xff5fa2),
	mkOnCol("Violet", 0xa46bff),
	mkOnCol("Orange", 0xff8a1f),
	mkOnCol("Argent", 0xc9d4e6),
}

var lookWhiteCols = []lookKeyCol{
	{"Ivoire", colKeyW, 0xcfc9b6, 0x7a7566, false}, // couleur d'origine
	{"Blanc pur", 0xffffff, 0xdfe3ea, 0x7d8592, false},
	{"Crème", 0xf6ead2, 0xd2bd92, 0x85704a, false},
	{"Gris perle", 0xe4e8ef, 0xb4bcc9, 0x6a7384, false},
	{"Noir", 0x2c323d, 0x0b0d11, 0x8d9ab2, true},
}

var lookBlackCols = []lookKeyCol{
	{"Noir", 0x2c323d, colKeyB, 0, true}, // couleur d'origine
	{"Graphite", 0x5a606b, 0x33373e, 0, true},
	{"Bois", 0x7a4a2a, 0x3e2312, 0, true},
	{"Bleu nuit", 0x2a3b66, 0x0e1730, 0, true},
	{"Ivoire", colKeyW, 0xcfc9b6, 0, false},
}

// polices : nom affiché, police intégrée (Mac, Android) et police de Windows équivalente
const (
	famModern = iota // Inter
	famSerif         // DejaVu Serif / Georgia
	famSans          // DejaVu Sans / Verdana
	famMono          // DejaVu Sans Mono / Consolas
	nFams
)

var lookFamNames = [nFams]string{"Moderne", "Classique", "Lisible", "Machine à écrire"}
var lookFamWin = [nFams]string{"Segoe UI", "Georgia", "Verdana", "Consolas"}

// réglage en cours (le même pour toutes les fenêtres du programme)
var look struct{ on, white, black, font int }

func (c *Config) sanitizeLook() {
	c.LookOn = clamp(c.LookOn, 0, len(lookOnCols)-1)
	c.LookWhite = clamp(c.LookWhite, 0, len(lookWhiteCols)-1)
	c.LookBlack = clamp(c.LookBlack, 0, len(lookBlackCols)-1)
	c.LookFont = clamp(c.LookFont, 0, nFams-1)
}

func (u *UI) applyLook() {
	u.cfg.sanitizeLook()
	look.on, look.white, look.black, look.font = u.cfg.LookOn, u.cfg.LookWhite, u.cfg.LookBlack, u.cfg.LookFont
	u.host.Invalidate()
}

func curOnCol() lookOnCol  { return lookOnCols[clamp(look.on, 0, len(lookOnCols)-1)] }
func curWhite() lookKeyCol { return lookWhiteCols[clamp(look.white, 0, len(lookWhiteCols)-1)] }
func curBlack() lookKeyCol { return lookBlackCols[clamp(look.black, 0, len(lookBlackCols)-1)] }
func curFam() int          { return clamp(look.font, 0, nFams-1) }
func (u *UI) lookText() string {
	return Tf("%s · %s / %s · %s", T(curOnCol().name), T(curWhite().name), T(curBlack().name), T(lookFamNames[curFam()]))
}

func (u *UI) setLook(f func(c *Config)) {
	f(u.cfg)
	u.applyLook()
	u.dirty = true
}

// écran « Personnalisation »
func (u *UI) OpenLook() {
	u.ov = &overlay{title: T("Personnalisation"), hint: T("Clique sur une couleur ou une police : le changement est immédiat · Échap pour fermer"),
		back: u.OpenSettings, sel: -1}
	u.ov.custom = u.drawLook
	u.host.Invalidate()
}

func (u *UI) drawLook(c *Canvas, r rect) {
	x, y := r.x+6, r.y+4
	sw := 40 // pas entre deux pastilles
	row := func(title, val string) {
		c.Text(x, y, 300, 16, title, fontXs, colGold, 0)
		c.Text(x+300, y, r.w-312, 16, val, fontSm, colText, 2)
		y += 22
	}

	// notes jouées
	row(T("Couleur des notes jouées"), T(curOnCol().name))
	for i, oc := range lookOnCols {
		i, oc := i, oc
		cx := float64(x + 14 + i*sw)
		cy := float64(y + 14)
		if i == look.on {
			c.Circle(cx, cy, 16, colText, 1)
			c.Circle(cx, cy, 14, colPanel, 1)
		}
		c.GlowCircle(cx, cy, 11, 6, oc.base, 0.35)
		c.Circle(cx, cy, 11, oc.base, 1)
		c.Circle(cx-3, cy-3, 4, oc.light, 0.8)
		u.reg(region{r: rect{int(cx) - 16, int(cy) - 16, 32, 32}, id: fmt.Sprintf("lookOn%d", i),
			down: func(int, int) { u.setLook(func(c *Config) { c.LookOn = i }) }})
	}
	y += 40

	// touches blanches et noires
	keyRow := func(title string, cols []lookKeyCol, cur int, id string, set func(i int)) {
		row(title, T(cols[clamp(cur, 0, len(cols)-1)].name))
		for i, kc := range cols {
			i := i
			bx := x + i*62
			if i == cur {
				c.RRectStroke(float64(bx-3), float64(y-3), 56, 38, 6, 2, colGold, 1)
			}
			c.VGrad(bx, y, 50, 32, kc.top, kc.bot)
			c.RRectStroke(float64(bx), float64(y), 50, 32, 4, 1, 0x4a5a78, 1)
			u.reg(region{r: rect{bx - 3, y - 3, 56, 38}, id: fmt.Sprintf("%s%d", id, i), down: func(int, int) { set(i) }})
		}
		y += 46
	}
	keyRow(T("Couleur des touches blanches"), lookWhiteCols, look.white, "lookW", func(i int) { u.setLook(func(c *Config) { c.LookWhite = i }) })
	keyRow(T("Couleur des touches noires"), lookBlackCols, look.black, "lookB", func(i int) { u.setLook(func(c *Config) { c.LookBlack = i }) })

	// police
	row(T("Police des lettres"), T(lookFamNames[curFam()]))
	bw := (r.w - 12 - 3*8) / 4
	for f := 0; f < nFams; f++ {
		f := f
		br := rect{x + f*(bw+8), y, bw, 34}
		on := f == curFam()
		bg, fg := uint32(colRow), uint32(colText)
		if on {
			bg, fg = colGold, colDark
		}
		c.RRect(float64(br.x), float64(br.y), float64(br.w), float64(br.h), 6, bg, 1)
		c.RRectStroke(float64(br.x), float64(br.y), float64(br.w), float64(br.h), 6, 1, colLine, 1)
		c.TextFam(br.x+4, br.y, br.w-8, br.h, T(lookFamNames[f]), fontMdB, fg, 1, f)
		u.reg(region{r: br, id: fmt.Sprintf("lookF%d", f), down: func(int, int) { u.setLook(func(c *Config) { c.LookFont = f }) }})
	}
	y += 50

	// aperçu : deux octaves, quelques notes jouées
	c.Text(x, y, 300, 16, T("Aperçu"), fontXs, colGold, 0)
	y += 20
	u.drawKeysPreview(c, x, y, 14*28, 92, map[int]bool{0: true, 4: true, 7: true, 13: true})
	px := x + 14*28 + 16
	c.Text(px, y, r.w-(px-r.x)-6, 30, "Do  Ré  Mi  Fa  Sol", fontLg, colText, 0)
	c.Text(px, y+32, r.w-(px-r.x)-6, 20, "Layers Keys · By JB Praise", fontMd, colText2, 0)
	c.Text(px, y+56, r.w-(px-r.x)-6, 20, "C  Dm  Em  F  G  Am", fontMdB, colGold, 0)

	// retour aux réglages d'origine
	u.button(c, "lookReset", rect{r.x + r.w - 220, r.y + r.h - 34, 214, 30}, T("Couleurs et police d'origine"), false, fontSm, func() {
		u.setLook(func(c *Config) { c.LookOn, c.LookWhite, c.LookBlack, c.LookFont = 0, 0, 0, 0 })
	})
}

// petit clavier d'aperçu (notes = demi-tons depuis Do qui sont « jouées »)
func (u *UI) drawKeysPreview(c *Canvas, x, y, w, h int, on map[int]bool) {
	wk := []int{0, 2, 4, 5, 7, 9, 11}
	n := w / 28
	kw := w / n
	oc, wc, bc := curOnCol(), curWhite(), curBlack()
	c.RRect(float64(x-4), float64(y-4), float64(n*kw+8), float64(h+8), 6, 0x05070b, 1)
	for i := 0; i < n; i++ {
		note := i/7*12 + wk[i%7]
		top, bot := wc.top, wc.bot
		if on[note] {
			top, bot = oc.light, oc.base
		}
		c.VGrad(x+i*kw, y, kw-1, h, top, bot)
	}
	for i := 0; i < n-1; i++ {
		s := wk[i%7]
		if s == 4 || s == 11 {
			continue
		}
		note := i/7*12 + s + 1
		top, bot := bc.top, bc.bot
		if on[note] {
			top, bot = oc.bHi, oc.bLo
		}
		bw := kw * 6 / 10
		c.VGrad(x+(i+1)*kw-bw/2, y, bw, h*62/100, top, bot)
	}
}
