package main

// Projets Layers Keys (.mpiano) : nouveau, ouvrir, enregistrer, enregistrer sous.

import (
	"path/filepath"
	"strings"
)

func (u *UI) projectName() string {
	if u.projPath == "" {
		return T("Sans titre")
	}
	return baseNoExt(u.projPath)
}

func (u *UI) windowTitle() string {
	mark := ""
	if u.changed {
		mark = " *"
	}
	return u.projectName() + mark + "  —  Layers Keys  ·  By JB Praise"
}

func (u *UI) addRecentProject(path string) {
	out := []string{path}
	for _, r := range u.cfg.RecentProj {
		if r != path && len(out) < 8 {
			out = append(out, r)
		}
	}
	u.cfg.RecentProj = out
}

// écrit le projet dans path ; renvoie vrai si c'est réussi
func (u *UI) writeProject(path string) bool {
	if strings.ToLower(filepath.Ext(path)) != projectExt {
		path += projectExt
	}
	c := *u.cfg
	c.Setup = u.snapshot()
	c.Master, c.Reverb = u.eng.master, u.eng.revAmt
	if err := saveProject(path, &c); err != nil {
		u.Toast(Tf("Impossible d'enregistrer le projet : %s", err.Error()))
		return false
	}
	u.projPath, u.changed = path, false
	u.addRecentProject(path)
	u.dirty = true
	u.Toast(Tf("Projet « %s » enregistré", baseNoExt(path)))
	return true
}

// Enregistrer (Ctrl+S) : vrai si le projet est bien enregistré
func (u *UI) SaveProject() bool {
	if u.projPath == "" {
		return u.SaveProjectAs()
	}
	return u.writeProject(u.projPath)
}

// Enregistrer sous (Ctrl+Maj+S)
func (u *UI) SaveProjectAs() bool {
	name := u.projectName()
	if u.projPath == "" {
		name = T("Mon projet")
	}
	p, ok := u.host.SaveFile(name)
	if !ok {
		return false
	}
	return u.writeProject(p)
}

// demande quoi faire des modifications non enregistrées, puis exécute then()
func (u *UI) confirmDiscard(then func()) {
	if !u.changed {
		then()
		return
	}
	items := []olItem{
		{text: "Enregistrer", sub: "puis continuer", action: func() {
			if u.SaveProject() {
				then()
			}
		}},
		{text: "Ne pas enregistrer", sub: "perdre les modifications", action: func() { u.changed = false; then() }},
		{text: "Annuler", action: func() {}},
	}
	u.ov = &overlay{title: Tf("Enregistrer les modifications de « %s » ?", u.projectName()), items: items,
		hint: "Clique sur un choix · Échap pour annuler"}
	u.host.Invalidate()
}

func (u *UI) resetProject() {
	u.eng.Panic()
	for i := range u.eng.ch {
		u.eng.ch[i].Solo = false
	}
	d := defaultConfig()
	u.cfg.Presets, u.cfg.Cur = d.Presets, 0
	u.eng.SetMaster(d.Master)
	u.eng.SetReverb(d.Reverb)
	u.loadFX(nil)
	u.applySetup(d.Setup)
	u.projPath, u.changed = "", false
	u.dirty = true
}

func (u *UI) NewProject() {
	u.confirmDiscard(func() {
		u.resetProject()
		u.Toast(T("Nouveau projet"))
	})
}

func (u *UI) openProjectFile(path string) {
	u.confirmDiscard(func() {
		c, err := loadProject(path)
		if err != nil {
			u.Toast(filepath.Base(path) + " : " + T(err.Error()))
			return
		}
		u.eng.Panic()
		u.cfg.Presets, u.cfg.Cur = c.Presets, c.Cur
		u.eng.SetMaster(c.Master)
		u.eng.SetReverb(c.Reverb)
		u.loadFX(c.FX)
		for i := range u.eng.ch {
			u.eng.ch[i].Solo = false
		}
		u.applySetup(c.Setup)
		u.projPath, u.changed = path, false
		u.addRecentProject(path)
		u.dirty = true
		u.Toast(Tf("Projet « %s » ouvert", baseNoExt(path)))
	})
}

// Ouvrir (Ctrl+O) : liste des projets récents + parcourir
func (u *UI) OpenProjectMenu() {
	items := []olItem{{text: "Parcourir l'ordinateur…", sub: "fichier .lkeys", action: func() {
		if p, ok := u.host.OpenFile("project"); ok {
			u.openProjectFile(p)
		}
	}}}
	var recent []olItem
	for _, p := range u.cfg.RecentProj {
		p := p
		if !fileExists(p) {
			continue
		}
		recent = append(recent, olItem{text: baseNoExt(p), sub: filepath.Dir(p), action: func() { u.openProjectFile(p) }})
	}
	if len(recent) > 0 {
		items = append(items, olItem{text: "Projets récents", header: true})
		items = append(items, recent...)
	}
	u.ov = &overlay{title: "Ouvrir un projet", items: items, hint: "Clique sur un projet · Échap pour fermer"}
	u.ov.sel = u.ov.firstSelectable()
	u.host.Invalidate()
}

// appelé quand l'utilisateur ferme la fenêtre : vrai = on peut fermer tout de suite
func (u *UI) RequestClose() bool {
	if !u.changed {
		return true
	}
	u.confirmDiscard(func() { u.host.Quit() })
	return false
}
