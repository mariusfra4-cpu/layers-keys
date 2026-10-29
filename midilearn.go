package main

// MIDI Learn : on associe un potard, un fader ou un bouton du clavier MIDI (un « CC ») à un réglage du logiciel.
// Clic droit sur un réglage → « Apprendre », puis on bouge le potard : c'est associé.
// Les associations sont des réglages du logiciel (comme la courbe de vélocité) : elles ne sont pas dans les fichiers projet,
// mais elles sont gardées dans le projet du DAW quand Layers Keys tourne en plugin.

import (
	"math"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

// LearnCfg : une association « CC → réglage ».
type LearnCfg struct {
	CC int    `json:"cc"`
	ID string `json:"id"`
}

type learnState struct {
	mu     sync.Mutex
	target string    // réglage en attente d'un CC (mode apprentissage)
	until  time.Time // fin du mode apprentissage
	warned int       // dernier CC réservé signalé
	m      atomic.Pointer[map[int]string]
	prev   [128]int32
}

// CC que le logiciel utilise déjà : on ne les détourne pas.
func learnProtected(cc int) bool {
	switch cc {
	case 0, 1, 7, 11, 32, 64, 66, 67:
		return true
	}
	return cc >= 120
}

func idIndex(id, prefix string, max int) (int, bool) {
	if !strings.HasPrefix(id, prefix) {
		return 0, false
	}
	s := id[len(prefix):]
	if s == "" || len(s) > 2 {
		return 0, false
	}
	n := 0
	for _, r := range s {
		if r < '0' || r > '9' {
			return 0, false
		}
		n = n*10 + int(r-'0')
	}
	return n, n < max
}

// learnKind : de quel genre de réglage s'agit-il ? (kind, numéro de piste ou de paramètre)
func learnKind(id string) (string, int, bool) {
	switch id {
	case "master", "reverb", "plVol", "plPlay", "plStop", "tgPedal", "tgFilter", "tgModFX", "tgAT":
		return id, 0, true
	}
	for _, p := range []string{"vol", "pan", "mute", "solo", "hold"} {
		if n, ok := idIndex(id, p, nCh); ok {
			return p, n, true
		}
	}
	if n, ok := idIndex(id, "p", nParam); ok {
		return "p", n, true
	}
	return "", 0, false
}

func learnable(id string) bool { _, _, ok := learnKind(id); return ok }

// learnName : le nom d'un réglage tel qu'on l'affiche.
func learnName(id string) string {
	k, n, ok := learnKind(id)
	if !ok {
		return id
	}
	switch k {
	case "vol":
		return Tf("Volume de la piste %d", n+1)
	case "pan":
		return Tf("Panoramique de la piste %d", n+1)
	case "mute":
		return Tf("Muet de la piste %d", n+1)
	case "solo":
		return Tf("Solo de la piste %d", n+1)
	case "hold":
		return Tf("Maintien de la piste %d", n+1)
	case "p":
		return Tf("%s (piste sélectionnée)", paramNames[n])
	case "tgPedal":
		return T("Pédale (piste sélectionnée)")
	case "tgFilter":
		return T("Filtre (piste sélectionnée)")
	case "tgModFX":
		return T("MOD FX (piste sélectionnée)")
	case "tgAT":
		return T("Aftertouch (piste sélectionnée)")
	case "master":
		return T("Volume général")
	case "reverb":
		return T("Réverbération")
	case "plVol":
		return T("Volume du lecteur")
	case "plPlay":
		return T("Lecteur : lecture / pause")
	case "plStop":
		return T("Lecteur : stop")
	}
	return id
}

func (u *UI) learnMap() map[int]string {
	if p := u.learn.m.Load(); p != nil {
		return *p
	}
	return nil
}

func (u *UI) learnStore(m map[int]string) { u.learn.m.Store(&m) }

// charge la liste enregistrée (en vérifiant chaque ligne)
func (u *UI) learnLoad(list []LearnCfg) {
	m := map[int]string{}
	for _, l := range list {
		if l.CC < 0 || l.CC > 127 || learnProtected(l.CC) || !learnable(l.ID) {
			continue
		}
		for cc, id := range m {
			if id == l.ID {
				delete(m, cc)
			}
		}
		m[l.CC] = l.ID
	}
	u.learnStore(m)
}

// la liste à enregistrer, triée par CC
func (u *UI) learnList() []LearnCfg {
	m := u.learnMap()
	out := make([]LearnCfg, 0, len(m))
	for cc, id := range m {
		out = append(out, LearnCfg{cc, id})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].CC < out[j].CC })
	return out
}

func (u *UI) learnCCOf(id string) int {
	for cc, i := range u.learnMap() {
		if i == id {
			return cc
		}
	}
	return -1
}

func (u *UI) learnForget(id string) {
	m := map[int]string{}
	for cc, i := range u.learnMap() {
		if i != id {
			m[cc] = i
		}
	}
	u.learnStore(m)
	u.dirty = true
}

func (u *UI) learnClear() {
	u.learnStore(map[int]string{})
	u.dirty = true
}

func (u *UI) learnStart(id string) {
	u.learn.mu.Lock()
	u.learn.target = id
	u.learn.until = u.now().Add(30 * time.Second)
	u.learn.warned = -1
	u.learn.mu.Unlock()
	u.Toast(Tf("MIDI Learn : bouge le potard ou appuie sur le bouton à associer à « %s » (Échap pour annuler)", learnName(id)))
}

func (u *UI) learnTarget() string {
	u.learn.mu.Lock()
	defer u.learn.mu.Unlock()
	if u.learn.target != "" && u.now().After(u.learn.until) {
		u.learn.target = ""
	}
	return u.learn.target
}

// annule l'apprentissage en cours ; vrai s'il y en avait un
func (u *UI) learnCancel() bool {
	u.learn.mu.Lock()
	was := u.learn.target != ""
	u.learn.target = ""
	u.learn.mu.Unlock()
	if was {
		u.Toast("MIDI Learn annulé.")
	}
	return was
}

// un CC arrive : s'il y a un apprentissage en cours, il est associé ; sinon il commande son réglage.
// Appelé depuis le thread MIDI (ou le thread audio du plugin) : il ne touche qu'au moteur.
// Renvoie vrai si le message est consommé par l'apprentissage.
func (u *UI) learnMidi(cc, val int) bool {
	if cc < 0 || cc > 127 {
		return false
	}
	if tgt := u.learnTarget(); tgt != "" {
		if learnProtected(cc) {
			u.learn.mu.Lock()
			w := u.learn.warned
			u.learn.warned = cc
			u.learn.mu.Unlock()
			if w != cc {
				u.Toast(Tf("Le CC %d est déjà utilisé par le logiciel : choisis un autre potard.", cc))
			}
			return false
		}
		m := map[int]string{}
		for c, i := range u.learnMap() {
			if i != tgt && c != cc {
				m[c] = i
			}
		}
		m[cc] = tgt
		u.learnStore(m)
		atomic.StoreInt32(&u.learn.prev[cc], int32(val))
		u.learn.mu.Lock()
		u.learn.target = ""
		u.learn.mu.Unlock()
		u.dirty = true
		u.Toast(Tf("CC %d associé à : %s", cc, learnName(tgt)))
		return true
	}
	m := u.learnMap()
	if len(m) == 0 {
		return false
	}
	id, ok := m[cc]
	if !ok {
		return false
	}
	prev := atomic.SwapInt32(&u.learn.prev[cc], int32(val))
	u.learnApply(id, val, val >= 64 && prev < 64)
	return false
}

// applique la valeur reçue (0..127) à un réglage ; edge = le bouton vient d'être appuyé
func (u *UI) learnApply(id string, val int, edge bool) {
	f := float64(val) / 127
	k, n, ok := learnKind(id)
	if !ok {
		return
	}
	sel := u.sel
	if sel < 0 || sel >= nCh {
		sel = 0
	}
	switch k {
	case "vol":
		ch := u.eng.ch[n]
		u.with(func() { ch.Vol = math.Round((f*66-60)*2) / 2 })
	case "pan":
		ch := u.eng.ch[n]
		u.with(func() { ch.Pan = math.Round((f*2-1)*20) / 20 })
	case "p":
		ch := u.eng.ch[sel]
		u.with(func() { ch.P[n] = f })
	case "master":
		u.eng.SetMaster(f)
	case "reverb":
		u.eng.SetReverb(f)
	case "plVol":
		u.eng.Player.SetVol(f)
	default: // boutons : un appui = un basculement
		if !edge {
			return
		}
		switch k {
		case "mute":
			ch := u.eng.ch[n]
			u.with(func() { ch.Mute = !ch.Mute })
		case "solo":
			ch := u.eng.ch[n]
			u.with(func() { ch.Solo = !ch.Solo })
		case "hold":
			u.eng.SetHold(n, !u.eng.ch[n].Hold)
		case "tgPedal":
			u.eng.SetPedalAware(sel, !u.eng.ch[sel].Pedal)
		case "tgFilter":
			ch := u.eng.ch[sel]
			u.with(func() { ch.Filter = !ch.Filter })
		case "tgModFX":
			ch := u.eng.ch[sel]
			u.with(func() { ch.ModFX = !ch.ModFX })
		case "tgAT":
			ch := u.eng.ch[sel]
			u.with(func() { ch.AT = !ch.AT })
		case "plPlay":
			if pl := u.eng.Player; pl.Playing() {
				pl.Pause()
			} else {
				pl.Play()
			}
		case "plStop":
			u.eng.Player.Stop()
		}
	}
	u.markDirty()
	u.host.Invalidate()
}

// ---------------------------------------------------------------- interface

// RightDown : clic droit. Sur un réglage, ouvre le petit menu « MIDI Learn ».
func (u *UI) RightDown(x, y int) {
	u.mx, u.my = x, y
	if u.ov != nil {
		return
	}
	for i := len(u.regions) - 1; i >= 0; i-- {
		r := u.regions[i]
		if r.r.has(x, y) && learnable(r.id) {
			u.openLearnMenu(r.id)
			return
		}
	}
}

func (u *UI) openLearnMenu(id string) {
	items := []olItem{{text: learnName(id), header: true}}
	items = append(items, olItem{text: T("Apprendre : bouge un potard de ton clavier"), sub: T("ou appuie sur un bouton / un pad"),
		action: func() { u.learnStart(id) }})
	if cc := u.learnCCOf(id); cc >= 0 {
		items = append(items, olItem{text: Tf("Oublier l'association (CC %d)", cc), action: func() {
			u.learnForget(id)
			u.Toast("Association effacée.")
		}})
	}
	items = append(items, olItem{text: T("Toutes les associations…"), action: u.openLearnList})
	u.ov = &overlay{title: T("MIDI Learn"), items: items, hint: T("Clique sur un choix · Échap pour fermer")}
	u.ov.sel = u.ov.firstSelectable()
	u.host.Invalidate()
}

// la liste de toutes les associations (menu Paramètres)
func (u *UI) openLearnList() {
	items := []olItem{{text: T("Comment faire"), header: true},
		{text: T("Clic droit sur un réglage (volume, pan, bouton…) → Apprendre."), action: func() {}},
		{text: T("Puis bouge le potard, le fader ou le bouton de ton clavier MIDI."), action: func() {}}}
	list := u.learnList()
	items = append(items, olItem{text: T("Associations"), header: true})
	if len(list) == 0 {
		items = append(items, olItem{text: T("Aucune association pour l'instant"), action: func() {}})
	}
	for _, l := range list {
		l := l
		items = append(items, olItem{text: Tf("CC %d  →  %s", l.CC, learnName(l.ID)), sub: T("touche pour oublier"), action: func() {
			u.learnForget(l.ID)
			u.openLearnList()
		}})
	}
	if len(list) > 0 {
		items = append(items, olItem{text: T("Tout effacer"), action: func() {
			u.learnClear()
			u.openLearnList()
		}})
	}
	u.ov = &overlay{title: T("MIDI Learn"), items: items, hint: T("Clique sur une association pour l'oublier · Échap pour fermer"), back: u.OpenSettings}
	u.ov.sel = u.ov.firstSelectable()
	u.host.Invalidate()
}

// repères dessinés sur les réglages : un point doré = déjà associé, un cadre doré = en attente d'un potard
func (u *UI) drawLearnMarks(c *Canvas) {
	tgt := u.learnTarget()
	m := u.learnMap()
	if tgt == "" && len(m) == 0 {
		return
	}
	mapped := map[string]bool{}
	for _, id := range m {
		mapped[id] = true
	}
	for _, r := range u.regions {
		if !learnable(r.id) {
			continue
		}
		if r.id == tgt {
			c.RRectStroke(float64(r.r.x-3), float64(r.r.y-3), float64(r.r.w+6), float64(r.r.h+6), 7, 2, colGold, 1)
		} else if mapped[r.id] {
			c.Circle(float64(r.r.x+r.r.w-2), float64(r.r.y+2), 3, colGoldHi, 0.95)
		}
	}
}
