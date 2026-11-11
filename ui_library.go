package main

// Fenêtres de la bibliothèque de sons (« Charger un son ») et des pads continus.

import (
	"fmt"
	"math"
	"path/filepath"
	"strings"
)

// ---------------------------------------------------------------- « Charger un son » : les sons du dossier, rangés en colonnes

func (u *UI) openSoundMenu(ch int) {
	u.libTrack = ch
	u.ov = &overlay{title: Tf("Choisir le son de la piste %d", ch+1), wide: true, sel: -1,
		hint: T("Clique sur un son pour le charger · molette = faire défiler une colonne · Échap pour fermer")}
	u.ov.custom = u.drawLibrary
	u.host.Invalidate()
}

const libRowH = 26

// libPickFile : bouton « Ouvrir un fichier… » (un son hors du dossier de sons)
func (u *UI) libPickFile(ch int) {
	if p, ok := u.host.OpenFile("sound"); ok {
		u.openFileFor(ch, p)
	}
}

func (u *UI) drawLibrary(c *Canvas, r rect) {
	ch := u.libTrack
	lib := u.library()
	u.libs.mu.Lock()
	dir, sounds := lib.Dir, len(lib.Sounds)
	cats := lib.byCat()
	u.libs.mu.Unlock()
	scanning, nfiles := u.libScanning()

	// barre du haut
	bx := r.x
	u.button(c, "libFile", rect{bx, r.y - 6, 190, 28}, "OUVRIR UN FICHIER…", false, fontSm, func() { u.libPickFile(ch) })
	bx += 198
	u.button(c, "libNone", rect{bx, r.y - 6, 150, 28}, "VIDER LA PISTE", false, fontSm, func() {
		u.loadSource(ch, SrcCfg{})
		u.closeOverlay()
	})
	bx += 158
	dirLbl := "CHOISIR LE DOSSIER DE SONS…"
	if dir != "" {
		dirLbl = "CHANGER DE DOSSIER…"
	}
	u.button(c, "libDir", rect{bx, r.y - 6, 230, 28}, dirLbl, dir == "", fontSm, u.libChooseDir)
	bx += 238
	if dir != "" {
		u.button(c, "libRescan", rect{bx, r.y - 6, 170, 28}, "ANALYSER À NOUVEAU", false, fontSm, func() { u.libScan(dir) })
		bx += 178
	}
	info := ""
	switch {
	case scanning:
		info = Tf("Analyse… %d fichiers lus", nfiles)
	case dir != "":
		info = Tf("%d sons · %s", sounds, dir)
	}
	c.Text(bx, r.y-6, r.x+r.w-bx, 28, info, fontXs, colText2, 0)

	top := r.y + 32
	if dir == "" && sounds == 0 {
		// aucun dossier : explication
		c.RRect(float64(r.x), float64(top), float64(r.w), float64(r.h-40), 10, 0x0c121d, 1)
		drawIcon(c, icDownload, float64(r.x+r.w/2), float64(top+90), 64, colGold, 0.8)
		c.Text(r.x, top+140, r.w, 26, "Choisis ton dossier de sons", fontLg, colText, 1)
		c.Text(r.x, top+172, r.w, 20, "Layers Keys l'analyse (sous-dossiers compris) et range tout seul les pianos, orgues, synthés, pads et strings.", fontMd, colText2, 1)
		c.Text(r.x, top+196, r.w, 20, "SoundFont SF2 / SF3, WAV et MP3. La liste est enregistrée : à la prochaine ouverture, tes sons sont déjà là.", fontMd, colText2, 1)
		u.button(c, "libDirBig", rect{r.x + r.w/2 - 150, top + 236, 300, 40}, "CHOISIR LE DOSSIER DE SONS…", true, fontMdB, u.libChooseDir)
		return
	}

	// colonnes : une par famille
	gap := 10
	cw := (r.w - gap*(nCat-1)) / nCat
	listTop := top + 40
	rows := (r.y + r.h - 6 - listTop) / libRowH
	cur := u.srcCfg[ch]
	for k := 0; k < nCat; k++ {
		k := k
		x := r.x + k*(cw+gap)
		list := cats[k]
		// titre de la colonne
		c.RRect(float64(x), float64(top), float64(cw), 32, 8, 0x162238, 1)
		drawIcon(c, catIcons[k], float64(x+20), float64(top+16), 18, colGold, 0.9)
		c.Text(x+34, top, cw-40, 32, fmt.Sprintf("%s  ·  %d", T(catNames[k]), len(list)), fontSm, colGold, 0)
		// liste
		c.RRect(float64(x), float64(listTop-4), float64(cw), float64(rows*libRowH+8), 8, 0x0b111b, 1)
		u.libs.mu.Lock()
		sc := u.libs.scroll[k]
		if max := len(list) - rows; sc > max {
			sc = max
		}
		if sc < 0 {
			sc = 0
		}
		u.libs.scroll[k] = sc
		u.libs.mu.Unlock()
		colR := rect{x, listTop - 4, cw, rows*libRowH + 8}
		u.reg(region{r: colR, id: fmt.Sprintf("libcol%d", k), down: func(int, int) {}, wheel: func(d int) {
			u.libs.mu.Lock()
			u.libs.scroll[k] -= d / 40
			u.libs.mu.Unlock()
			u.host.Invalidate()
		}})
		if len(list) == 0 {
			c.Text(x, listTop+8, cw, 18, "aucun son", fontXs, colText2, 1)
			continue
		}
		for i := 0; i < rows && sc+i < len(list); i++ {
			s := list[sc+i]
			ry := listTop + i*libRowH
			rr := rect{x + 4, ry, cw - 8, libRowH - 2}
			on := cur.File == s.File && cur.Kind == s.Kind && (s.Kind != "sf2" || cur.Bank == s.Bank && cur.Prog == s.Prog)
			if on {
				c.RRect(float64(rr.x), float64(rr.y), float64(rr.w), float64(rr.h), 6, colGold, 1)
			} else if rr.has(u.mx, u.my) {
				c.RRect(float64(rr.x), float64(rr.y), float64(rr.w), float64(rr.h), 6, 0x22314b, 1)
			}
			fg := uint32(colText)
			if on {
				fg = colDark
			}
			c.Text(rr.x+8, rr.y, rr.w-12, rr.h, s.Name, fontSm, fg, 0)
			u.reg(region{r: rr, id: fmt.Sprintf("lib%d_%d", k, i), down: func(int, int) {
				u.loadSource(ch, s.src())
				u.addRecent(s.File)
				u.closeOverlay()
			}})
		}
		// petite barre de défilement
		if len(list) > rows {
			th := math.Max(20, float64(rows*libRowH)*float64(rows)/float64(len(list)))
			ty := float64(listTop) + (float64(rows*libRowH)-th)*float64(sc)/float64(len(list)-rows)
			c.RRect(float64(x+cw-5), ty, 3, th, 1.5, colGoldDim, 0.9)
		}
	}
}

// ---------------------------------------------------------------- pads continus

// bouton « PADS » de la barre du bas : ouvre la fenêtre des pads, et montre ce qui joue
func (u *UI) drawPadButton(c *Canvas, x, y int) {
	cfg := u.padCfg()
	lbl := "PADS…"
	on := false
	if cfg.On {
		on = true
		k := u.padPlaying()
		if k < 0 {
			k = cfg.Key
		}
		lbl = "PAD · " + padKeyName(u.namer, k)
	}
	u.button(c, "padOpen", rect{x, y, 170, 18}, lbl, on, fontXs, u.OpenPads)
}

func (u *UI) OpenPads() {
	u.ov = &overlay{title: T("Pads continus"), sel: -1, wide: true,
		hint: T("Une tonalité par bouton · en mode AUTO, le pad suit l'accord que tu tiens · Échap pour fermer")}
	u.ov.custom = u.drawPads
	u.host.Invalidate()
}

func (u *UI) drawPads(c *Canvas, r rect) {
	cfg := u.padCfg()
	playing, loading := u.padPlaying(), u.padLoading()
	n := cfg.count()

	// ligne du haut : marche / arrêt, mode, dossier, volume
	onLbl := "PADS ÉTEINTS"
	if cfg.On {
		onLbl = "PADS ALLUMÉS"
	}
	u.button(c, "padOn", rect{r.x, r.y - 6, 170, 32}, onLbl, cfg.On, fontMdB, u.padToggle)
	modeLbl := "MODE : SUIT LES ACCORDS"
	if cfg.Manual {
		modeLbl = "MODE : MANUEL"
	}
	u.button(c, "padMode", rect{r.x + 180, r.y - 6, 240, 32}, modeLbl, !cfg.Manual, fontSm, func() {
		u.padSet(func(p *PadCfg) { p.Manual = !p.Manual })
	})
	dirLbl := "CHOISIR LE DOSSIER DES PADS…"
	if cfg.Dir != "" {
		dirLbl = "CHANGER DE DOSSIER…"
	}
	u.button(c, "padDir", rect{r.x + 430, r.y - 6, 250, 32}, dirLbl, cfg.Dir == "", fontSm, u.padChooseDir)
	c.Text(r.x+700, r.y-6, 70, 32, "Volume", fontMd, colText, 0)
	u.slider(c, "padVol", r.x+770, r.y, 220, 20, cfg.Vol, func(v float64) {
		u.padSet(func(p *PadCfg) { p.Vol = v })
	}, 0.6)
	c.Text(r.x+996, r.y-6, 60, 32, fmt.Sprintf("%d%%", int(math.Round(cfg.Vol*100))), fontSm, colGold, 2)

	info := T("Aucun dossier choisi : choisis un dossier qui contient un pad par tonalité (majeur et mineur).")
	if cfg.Dir != "" {
		info = Tf("%d / 24 tonalités trouvées · %s", n, cfg.Dir)
	}
	c.Text(r.x, r.y+34, r.w, 18, info, fontXs, colText2, 0)

	// 24 boutons : majeur en haut, mineur en bas
	bw := (r.w - 11*8) / 12
	for row := 0; row < 2; row++ {
		y := r.y + 70 + row*120
		title := "MAJEUR"
		if row == 1 {
			title = "MINEUR"
		}
		c.Text(r.x, y, 200, 18, title, fontSm, colGold, 0)
		for pc := 0; pc < 12; pc++ {
			key := row*12 + pc
			x := r.x + pc*(bw+8)
			br := rect{x, y + 24, bw, 70}
			has := cfg.Files[key] != ""
			isOn := cfg.On && playing == key
			name := u.namer.nm(pc, sharpFamily(pc, row == 1))
			if row == 1 {
				name = u.namer.join(name, "m")
			}
			if !has {
				c.RRect(float64(br.x), float64(br.y), float64(br.w), float64(br.h), 8, 0x0e1420, 1)
				c.RRectStroke(float64(br.x), float64(br.y), float64(br.w), float64(br.h), 8, 1, 0x26324a, 1)
				c.Text(br.x, br.y+8, br.w, 26, name, fontLg, 0x4a5670, 1)
				c.Text(br.x, br.y+40, br.w, 16, "pas de pad", fontXs, 0x4a5670, 1)
				continue
			}
			u.button(c, fmt.Sprintf("padK%d", key), br, "", isOn, fontLg, func() { u.padChoose(key) })
			fg := uint32(colText)
			if isOn {
				fg = colDark
			}
			c.Text(br.x, br.y+8, br.w, 26, name, fontLg, fg, 1)
			sub := baseNoExt(filepath.Base(cfg.Files[key]))
			if loading == key {
				sub = "chargement…"
			}
			c.Text(br.x+4, br.y+40, br.w-8, 16, sub, fontXs, lerpColor(fg, 0x808080, 0.3), 1)
		}
	}
	// arrêt en fondu
	if cfg.On {
		u.button(c, "padStop", rect{r.x, r.y + 320, 200, 30}, "ARRÊT EN FONDU", false, fontSm, func() {
			u.padSet(func(p *PadCfg) { p.On = false })
			u.padStop()
		})
	}
	c.Text(r.x+210, r.y+320, r.w-210, 30,
		"Noms de fichiers reconnus : « C Major », « C# minor », « Dbm », « F#m », « Bb », « La mineur », « Sib majeur »…",
		fontXs, colText2, 0)
	_ = strings.TrimSpace
}

// ---------------------------------------------------------------- dans les Paramètres

func (u *UI) libSettingsItems() []olItem {
	lib := u.library()
	u.libs.mu.Lock()
	dir, n := lib.Dir, len(lib.Sounds)
	u.libs.mu.Unlock()
	pc := u.padCfg()
	items := []olItem{{text: T("Sons"), header: true}}
	dirTxt := T("Dossier de sons : aucun")
	sub := T("Layers Keys analyse le dossier et range les sons : piano, orgue, synthé, pad, strings")
	if dir != "" {
		dirTxt = Tf("Dossier de sons : %s", filepath.Base(dir))
		sub = Tf("%d sons rangés", n)
	}
	items = append(items, olItem{text: dirTxt, sub: sub, action: func() { u.libChooseDir(); u.OpenSettings() }})
	if dir != "" {
		items = append(items, olItem{text: T("Analyser à nouveau le dossier de sons"), sub: T("après avoir ajouté ou retiré des sons"), action: func() {
			u.libScan(dir)
			u.OpenSettings()
		}})
	}
	items = append(items, olItem{text: T("Pads continus"), header: true})
	padTxt := T("Dossier des pads : aucun")
	padSub := T("un pad par tonalité, majeur et mineur")
	if pc.Dir != "" {
		padTxt = Tf("Dossier des pads : %s", filepath.Base(pc.Dir))
		padSub = Tf("%d / 24 tonalités trouvées", pc.count())
	}
	items = append(items, olItem{text: padTxt, sub: padSub, action: func() { u.padChooseDir(); u.OpenSettings() }})
	items = append(items, olItem{text: T("Ouvrir la fenêtre des pads"), sub: T("tonalités, mode automatique, volume"), action: u.OpenPads})
	return items
}
