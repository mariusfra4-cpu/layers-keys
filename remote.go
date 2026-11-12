package main

// Télécommande Android « Layers Remote » (dossier android-remote) : association automatique de ses commandes,
// sans passer par MIDI Learn bouton par bouton. Numéros identiques à android-remote/.../MidiMap.java.
//
//   Volume piste 1-8 : CC 20-27   Muet : CC 102-109   Solo : CC 110-117   Hold : CC 70-77
//   Volume général : CC 28        Réverbération : CC 29   Leslie lent / rapide : CC 30
//   Presets 1-8 : Program Change 0-7 (fonctionne aussi avec n'importe quel pédalier ou clavier qui envoie des PC)

import (
	"fmt"
	"sync/atomic"
)

const (
	remVolBase, remMuteBase, remSoloBase, remHoldBase = 20, 102, 110, 70
	remMaster, remReverb, remLeslie                   = 28, 29, 30
)

// remoteMap : les associations CC → réglage de la télécommande
func remoteMap() map[int]string {
	m := map[int]string{remMaster: "master", remReverb: "reverb", remLeslie: "lesFast"}
	for i := 0; i < nCh; i++ {
		m[remVolBase+i] = fmt.Sprintf("vol%d", i)
		m[remMuteBase+i] = fmt.Sprintf("mute%d", i)
		m[remSoloBase+i] = fmt.Sprintf("solo%d", i)
		m[remHoldBase+i] = fmt.Sprintf("hold%d", i)
	}
	return m
}

// remoteLink : ajoute les commandes de la télécommande au MIDI Learn (les autres associations sont gardées,
// sauf celles qui utilisaient les mêmes numéros ou les mêmes réglages).
func (u *UI) remoteLink() {
	rm := remoteMap()
	ids := map[string]bool{}
	for _, id := range rm {
		ids[id] = true
	}
	m := map[int]string{}
	for cc, id := range u.learnMap() {
		if _, taken := rm[cc]; !taken && !ids[id] {
			m[cc] = id
		}
	}
	for cc, id := range rm {
		m[cc] = id
	}
	u.learnStore(m)
	u.dirty = true
	u.Toast(T("Télécommande Layers Remote associée : faders, M / S / H, général, réverbération, Leslie et presets."))
}

// remoteLinked : vrai si toutes les commandes de la télécommande sont associées
func (u *UI) remoteLinked() bool {
	m := u.learnMap()
	for cc, id := range remoteMap() {
		if m[cc] != id {
			return false
		}
	}
	return true
}

// programChange : appelé depuis le fil MIDI ; le preset est chargé dans le fil de l'interface (pcTick)
func (u *UI) programChange(prog int) {
	if prog >= 0 && prog < nPresets {
		atomic.StoreInt32(&u.pcPending, int32(prog)+1)
		u.host.Invalidate()
	}
}

func (u *UI) pcTick() {
	if v := atomic.SwapInt32(&u.pcPending, 0); v > 0 {
		i := int(v) - 1
		u.applyPreset(i)
		u.Toast(Tf("Preset %d « %s »", i+1, u.cfg.Presets[i].Name))
	}
}
