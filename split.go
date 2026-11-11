package main

// Split du clavier : chaque piste joue sur toute l'étendue (TOUT), seulement à gauche du point de split (GAUCHE),
// ou seulement à droite (DROITE). Exemple : une basse SF2 à gauche, un piano à droite, un pad partout.
//
// Chaque piste garde sa propre plage de notes (KeyLo..KeyHi, notes jouées avant octave / transposition) :
// elle est donc enregistrée avec la piste dans les presets, les projets et l'état du plugin.
// Le bouton SPLIT déplace d'un coup la frontière de toutes les pistes GAUCHE et DROITE ; on le règle en jouant la note.

import (
	"strconv"
	"sync/atomic"
)

const (
	zoneAll = iota
	zoneLeft
	zoneRight
	zoneCustom // plage réglée autrement (fichier modifié à la main…)
)

const defaultSplit = 60 // do central (C4)

// zone de la piste d'après sa plage
func chanZone(lo, hi int) int {
	switch {
	case lo <= 0 && hi >= 127:
		return zoneAll
	case lo <= 0 && hi < 127:
		return zoneLeft
	case lo > 0 && hi >= 127:
		return zoneRight
	}
	return zoneCustom
}

// plage enregistrée : [0, 0] (ou rien) = toute l'étendue
func zoneRange(z [2]int) (lo, hi int) {
	if z[0] == 0 && z[1] == 0 {
		return 0, 127
	}
	lo, hi = clamp(z[0], 0, 127), clamp(z[1], 0, 127)
	if hi < lo {
		return 0, 127
	}
	return lo, hi
}

func rangeZone(lo, hi int) [2]int {
	if lo <= 0 && hi >= 127 {
		return [2]int{}
	}
	return [2]int{lo, hi}
}

// splitNote : le point de split actuel (première note de la zone de droite), d'après les pistes
func (u *UI) splitNote() int {
	for _, ch := range u.eng.ch {
		switch chanZone(ch.KeyLo, ch.KeyHi) {
		case zoneLeft:
			return ch.KeyHi + 1
		case zoneRight:
			return ch.KeyLo
		}
	}
	if u.splitDef > 0 {
		return u.splitDef
	}
	return defaultSplit
}

func (u *UI) splitActive() bool {
	for _, ch := range u.eng.ch {
		if chanZone(ch.KeyLo, ch.KeyHi) != zoneAll {
			return true
		}
	}
	return false
}

// setZone : TOUT / GAUCHE / DROITE pour la piste i
func (u *UI) setZone(i, z int) {
	s := u.splitNote()
	ch := u.eng.ch[i]
	u.with(func() {
		switch z {
		case zoneLeft:
			ch.KeyLo, ch.KeyHi = 0, s-1
		case zoneRight:
			ch.KeyLo, ch.KeyHi = s, 127
		default:
			ch.KeyLo, ch.KeyHi = 0, 127
		}
	})
	u.markDirty()
	u.host.Invalidate()
}

// cycleZone : TOUT → GAUCHE → DROITE → TOUT
func (u *UI) cycleZone(i int) {
	z := chanZone(u.eng.ch[i].KeyLo, u.eng.ch[i].KeyHi)
	switch z {
	case zoneAll:
		u.setZone(i, zoneLeft)
		u.Toast(Tf("Piste %d : seulement à gauche du split (%s). Bouton SPLIT pour changer la note.", i+1, u.noteName(u.splitNote())))
	case zoneLeft:
		u.setZone(i, zoneRight)
		u.Toast(Tf("Piste %d : seulement à droite du split (%s et au-dessus).", i+1, u.noteName(u.splitNote())))
	default:
		u.setZone(i, zoneAll)
	}
}

// setSplit : nouvelle frontière pour toutes les pistes GAUCHE et DROITE
func (u *UI) setSplit(s int) {
	s = clamp(s, 1, 127)
	u.splitDef = s
	u.with(func() {
		for _, ch := range u.eng.ch {
			switch chanZone(ch.KeyLo, ch.KeyHi) {
			case zoneLeft:
				ch.KeyHi = s - 1
			case zoneRight:
				ch.KeyLo = s
			}
		}
	})
	u.markDirty()
	u.host.Invalidate()
}

// splitLearnStart : la prochaine note jouée (clavier MIDI, clavier à l'écran ou du PC) devient le point de split
func (u *UI) splitLearnStart() {
	atomic.StoreInt32(&u.splitLearn, 1)
	u.Toast(T("SPLIT : joue la note où le clavier doit être coupé (elle commence la zone de droite)."))
	u.host.Invalidate()
}

func (u *UI) splitLearning() bool { return atomic.LoadInt32(&u.splitLearn) != 0 }

// splitCatch : appelé à chaque note jouée (n'importe quel fil) ; garde la note si on attend le point de split
func (u *UI) splitCatch(note int) {
	if atomic.CompareAndSwapInt32(&u.splitLearn, 1, 0) {
		atomic.StoreInt32(&u.splitGot, int32(note)+1)
	}
}

// splitTick : applique dans le fil de l'interface la note reçue
func (u *UI) splitTick() {
	if v := atomic.SwapInt32(&u.splitGot, 0); v > 0 {
		n := int(v) - 1
		u.setSplit(n)
		msg := Tf("Split réglé sur %s : la zone de droite commence à cette note.", u.noteName(n))
		if !u.splitActive() {
			msg += " " + T("Mets maintenant une piste en GAUCHE (basse) et une en DROITE (piano) avec le bouton ZONE.")
		}
		u.Toast(msg)
	}
}

// noteName : « C4 » en lettres, « Do3 » en solfège (numérotation française : le do central est Do3)
func (u *UI) noteName(n int) string {
	oct := n/12 - 1
	if u.namer.Names[0] != "C" {
		oct = n/12 - 2
	}
	return u.namer.nm(((n%12)+12)%12, false) + strconv.Itoa(oct)
}

func zoneLabel(z int) string {
	switch z {
	case zoneLeft:
		return "GAUCHE"
	case zoneRight:
		return "DROITE"
	case zoneCustom:
		return "ZONE"
	}
	return "TOUT"
}

// drawSplitMarker : sur le clavier à l'écran, la zone de gauche est teintée de bleu et la note de split est marquée
func (u *UI) drawSplitMarker(c *Canvas) {
	if !u.splitActive() && !u.splitLearning() {
		return
	}
	s := u.splitNote()
	x, ok := u.whiteX[s]
	if !ok { // touche noire : la frontière passe au milieu de la touche
		if wx, ok2 := u.whiteX[s-1]; ok2 {
			x = wx + whiteW - 1
		} else if s <= lowNote {
			x = kbX
		} else {
			return
		}
	}
	if x > kbX {
		c.Rect(kbX, kbY, x-kbX, whiteH-4, colBlue, 0.22) // zone de gauche
	}
	c.Glow(float64(x-1), float64(kbY-4), 3, whiteH, 2, 8, colBlue, 0.6)
	c.Rect(x-1, kbY-4, 3, whiteH, colBlue, 1)
	lbl := T("SPLIT") + " " + u.noteName(s)
	ly := kbY + whiteH - 42
	c.RRect(float64(x-34), float64(ly), 68, 16, 8, colBlue, 1)
	c.Text(x-34, ly, 68, 16, lbl, fontXs, colDark, 1)
}
