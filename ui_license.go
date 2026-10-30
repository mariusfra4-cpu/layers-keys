package main

// Écran « Autoriser le logiciel » : plein écran tant que la licence n'est pas entrée.

import (
	"strings"
	"time"
)

func (u *UI) licenseOK() bool { return u.licSt.State == LicLicensed }

// refreshLicense relit l'état de la licence et règle le verrou du moteur de son.
func (u *UI) refreshLicense() {
	st := u.lic.Status()
	u.licSt = st
	switch st.State {
	case LicLicensed:
		u.eng.SetLicenseGate(false, time.Time{})
	case LicDemo:
		u.eng.SetLicenseGate(false, u.now().Add(st.DemoLeft))
	default:
		u.eng.SetLicenseGate(true, time.Time{})
	}
}

// tickLicense : appelé régulièrement ; renvoie vrai si l'écran doit être redessiné.
func (u *UI) tickLicense() bool {
	if u.licSt.State == LicLicensed {
		return false
	}
	now := u.now()
	if now.Before(u.licNext) {
		return false
	}
	u.licNext = now.Add(2 * time.Second)
	was := u.licSt.State
	u.refreshLicense()
	if was != LicLocked && u.licSt.State == LicLocked { // la démo vient de se terminer
		u.eng.Panic()
		u.licShow = true
		u.licMsg, u.licMsgOK = "Ta démo de 3 jours est terminée. Entre ta clé de licence pour continuer.", false
		return true
	}
	left := ""
	if u.licSt.State == LicDemo {
		left = FormatDemoLeft(u.licSt.DemoLeft)
	}
	if left != u.licLeft {
		u.licLeft = left
		return true
	}
	return false
}

func (u *UI) licPaste() {
	if cl, ok := u.host.(Clipper); ok {
		u.licKey = u.cleanKeyInput(cl.ClipGet())
		u.licMsg = ""
	}
	u.host.Invalidate()
}

func (u *UI) cleanKeyInput(s string) string {
	var b strings.Builder
	for _, r := range strings.ToUpper(s) {
		if r >= 'A' && r <= 'Z' || r >= '2' && r <= '7' {
			b.WriteRune(r)
		}
	}
	out := b.String()
	if len(out) > 110 {
		out = out[:110]
	}
	return out
}

func (u *UI) licActivate() {
	if err := u.lic.Activate(u.licKey); err != nil {
		u.licMsg, u.licMsgOK = err.Error(), false
		return
	}
	u.refreshLicense()
	u.licMsg, u.licMsgOK = "Logiciel autorisé. Merci !", true
	u.licKey = ""
	u.licShow = false
	u.Toast("Logiciel autorisé. Merci !")
}

func (u *UI) licDemo() {
	if u.licSt.State == LicDemo {
		u.licShow = false
		return
	}
	u.lic.StartDemo()
	u.refreshLicense()
	if u.licSt.State == LicDemo {
		u.licShow = false
		return
	}
	if u.licSt.Tamper {
		u.licMsg = "Les fichiers de la démo ont été modifiés : la démo est bloquée. Entre une clé de licence."
	} else {
		u.licMsg = "La démo de 3 jours a déjà été utilisée sur cet ordinateur. Entre ta clé de licence."
	}
	u.licMsgOK = false
}

func (u *UI) licCopy() {
	if cl, ok := u.host.(Clipper); ok {
		cl.ClipSet(u.lic.Code())
		u.licMsg, u.licMsgOK = "Code copié. Envoie-le à JB Praise.", true
	}
}

func (u *UI) licKeyDown(vk int) {
	ctrl, _ := u.host.Mods()
	switch {
	case ctrl && vk == 'V':
		u.licPaste()
	case ctrl && vk == 'C':
		u.licCopy()
	case vk == vkBack:
		if n := len(u.licKey); n > 0 {
			u.licKey = u.licKey[:n-1]
		}
	case vk == vkReturn:
		u.licActivate()
	}
	u.host.Invalidate()
}

func (u *UI) licChar(r rune) {
	if r < 32 || r == 127 {
		return
	}
	u.licKey = u.cleanKeyInput(u.licKey + string(r))
	u.licMsg = ""
	u.host.Invalidate()
}

func drawLogo(c *Canvas, x, y, s float64) {
	c.GlowCircle(x+22*s, y+17*s, 12*s, 14*s, colGold, 0.35)
	c.RRectGrad(x, y, 44*s, 34*s, 5*s, colGoldHi, colGoldDim, 1)
	for k := 0; k < 4; k++ {
		c.Rect(int(x+(11*float64(k+1)-1)*s), int(y+14*s), int(2*s), int(20*s), 0x2a1f05, 0.8)
	}
	for _, k := range []int{0, 1, 3} {
		c.RRect(x+(11*float64(k+1)-5)*s, y, 10*s, 19*s, 2*s, 0x120d03, 1)
	}
}

func (u *UI) drawLicense(c *Canvas) {
	c.VGrad(0, 0, c.W, c.H, 0x0e1626, 0x04070c)
	for k := 0; k < 4; k++ {
		x := float64(120 + k*300)
		c.Tri(x, 0, x+160, 0, x+50, 330, 0xffffff, 0.015)
	}
	// icône du logiciel et nom
	drawLogo(c, float64(c.W/2)-66, 40, 3)
	c.Text(0, 150, c.W, 44, "Layers Keys", fontHg, colGold, 1)
	c.Text(0, 194, c.W, 22, "By JB Praise — for church musician", fontLg, colText2, 1)

	// grand cadre « Autoriser le logiciel »
	x, y, w, h := 110, 252, c.W-220, 466
	c.Glow(float64(x), float64(y), float64(w), float64(h), 16, 14, 0x000000, 0.5)
	c.RRectGrad(float64(x), float64(y), float64(w), float64(h), 16, 0x172237, colPanel, 1)
	c.RRectStroke(float64(x), float64(y), float64(w), float64(h), 16, 2, colGoldDim, 1)
	c.Text(x, y+14, w, 34, "Autoriser le logiciel", fontXl, colGold, 1)
	c.Text(x+40, y+52, w-80, 20, "Un ordinateur = une licence. Envoie le code ci-dessous à JB Praise ; tu recevras ta clé de licence en retour.", fontMd, colText2, 1)

	// 1. code de l'ordinateur
	c.Text(x+40, y+88, 300, 18, "1.  Code de cet ordinateur", fontMdB, colText, 0)
	cr := rect{x + 40, y + 112, w - 80 - 150, 54}
	c.RRectGrad(float64(cr.x), float64(cr.y), float64(cr.w), float64(cr.h), 9, 0x070b12, 0x0e1623, 1)
	c.RRectStroke(float64(cr.x), float64(cr.y), float64(cr.w), float64(cr.h), 9, 1, 0x26364f, 1)
	c.Text(cr.x, cr.y, cr.w, cr.h, u.lic.Code(), fontHg, colGoldHi, 1)
	u.button(c, "liccopy", rect{cr.x + cr.w + 14, cr.y, 136, cr.h}, "Copier le code", false, fontMdB, func() { u.licCopy() })

	// 2. clé de licence
	c.Text(x+40, y+184, 300, 18, "2.  Clé de licence reçue", fontMdB, colText, 0)
	kr := rect{x + 40, y + 208, w - 80 - 150, 70}
	c.RRectGrad(float64(kr.x), float64(kr.y), float64(kr.w), float64(kr.h), 9, 0x070b12, 0x0e1623, 1)
	edge := uint32(0x26364f)
	if u.licKey != "" {
		edge = colGoldDim
	}
	c.RRectStroke(float64(kr.x), float64(kr.y), float64(kr.w), float64(kr.h), 9, 1.5, edge, 1)
	if u.licKey == "" {
		c.Text(kr.x+14, kr.y, kr.w-28, kr.h, "Colle ici la clé (bouton Coller ou Ctrl+V)", fontMd, colText2, 0)
	} else {
		k := u.licKey
		if len(k) > 0 && int(time.Now().UnixMilli()/500)%2 == 0 {
			k += "|"
		}
		var lines []string
		for len(k) > 0 {
			n := 56
			if len(k) < n {
				n = len(k)
			}
			lines = append(lines, k[:n])
			k = k[n:]
		}
		for i, l := range lines {
			if i > 2 {
				break
			}
			c.Text(kr.x+14, kr.y+8+i*20, kr.w-28, 20, l, fontLg, colText, 0)
		}
	}
	u.button(c, "licpaste", rect{kr.x + kr.w + 14, kr.y, 136, kr.h}, "Coller", false, fontMdB, func() { u.licPaste() })

	// message
	if u.licMsg != "" {
		col := uint32(colRed)
		if u.licMsgOK {
			col = colGreen
		}
		c.Text(x+40, y+288, w-80, 24, u.licMsg, fontMdB, col, 1)
	}

	// boutons
	bw := (w - 80 - 24) / 2
	u.buttonC(c, "licgo", rect{x + 40, y + 326, bw, 62}, "Activer", true, colGold, colDark, fontLg, func() { u.licActivate() })
	demo := "Mode démo — 3 jours"
	switch {
	case u.licSt.State == LicDemo:
		demo = "Continuer la démo (" + FormatDemoLeft(u.licSt.DemoLeft) + ")"
	case u.licSt.DemoUsed:
		demo = "Démo terminée"
	}
	if u.licSt.State == LicLocked && u.licSt.DemoUsed {
		c.RRectGrad(float64(x+40+bw+24), float64(y+326), float64(bw), 62, 7, 0x1c2230, 0x121823, 1)
		c.RRectStroke(float64(x+40+bw+24), float64(y+326), float64(bw), 62, 7, 1, 0x2a3446, 1)
		c.Text(x+40+bw+24, y+326, bw, 62, demo, fontLg, 0x5c6a82, 1)
	} else {
		u.button(c, "licdemo", rect{x + 40 + bw + 24, y + 326, bw, 62}, demo, false, fontLg, func() { u.licDemo() })
	}
	c.Text(x+40, y+412, w-80, 36, "Hors connexion : aucune connexion Internet n'est nécessaire. La licence est valable sur cet ordinateur uniquement (application, VST3 et AU).", fontMd, colText2, 1)
}
