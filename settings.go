package main

// Fenêtre « Paramètres » : sortie audio, langue, notation des accords, MIDI. Démarrage de l'audio.

import (
	"fmt"
	"strings"
)

func (u *UI) setNotation() {
	n := u.cfg.Notation
	if n == "" { // par défaut : Do Ré Mi en français/espagnol, lettres en anglais
		if CurLang() == langEN {
			n = "abc"
		} else {
			n = "solfege"
		}
	}
	switch {
	case n == "abc":
		u.namer = lettersNamer
	case CurLang() == langES:
		u.namer = ChordNamer{Names: noteNamesSolfegeES, Sharps: noteNamesSolfegeESSharp, Spaced: true}
	default:
		u.namer = ChordNamer{Names: noteNamesSolfege, Sharps: noteNamesSolfegeSharp, Spaced: true}
	}
	u.chordKey = [2]uint64{^uint64(0), 0} // force le recalcul
}

// ---------------------------------------------------------------- audio

// ouvre la sortie audio selon la configuration (avec détection de plantage du pilote)
func (u *UI) StartAudio() {
	if u.cfg.AudioPending { // le démarrage précédent n'a jamais fini : le pilote a probablement planté
		u.cfg.Audio.API, u.cfg.Audio.Device = apiAuto, ""
		u.cfg.AudioPending = false
		u.Toast("Le dernier pilote audio a planté : retour au mode standard (WASAPI).")
	}
	risky := u.cfg.Audio.API == apiASIO || u.cfg.Audio.API == apiWASAPX
	if risky && u.cfgPath != "" {
		u.cfg.AudioPending = true
		u.Save() // écrit avant d'essayer : si le pilote fait tout planter, on le saura
	}
	info, err := u.host.AudioOpen(u.cfg.Audio)
	u.cfg.AudioPending = false
	if risky && u.cfgPath != "" {
		u.Save()
	}
	if err != nil {
		u.audioInfo, u.audioErr = "", err.Error()
		u.Toast(Tf("Son indisponible : %s", err.Error()))
	} else {
		u.audioInfo, u.audioErr = info, ""
	}
	u.host.Invalidate()
}

func (u *UI) audioAPI() string {
	if u.cfg.Audio.API == apiAuto {
		return defaultAPI
	}
	return u.cfg.Audio.API
}

func (u *UI) apiSub(api string) string {
	switch api {
	case apiASIO:
		return T("recommandé pour une carte son externe")
	case apiWASAPX:
		return T("latence minimale, bloque les autres logiciels")
	case apiMME:
		return T("ancien système, compatibilité")
	}
	if api == apiCoreAudio {
		return T("standard du Mac")
	}
	return T("standard, partagé avec Windows")
}

func (u *UI) bufferMs(n int) float64 { return float64(n) / float64(u.cfg.Audio.Rate) * 1000 }

func (u *UI) openAudioMenu() {
	a := &u.cfg.Audio
	dev := a.Device
	if dev == "" {
		dev = T(defaultDeviceName)
	}
	bufTxt := T("Tampon : automatique")
	if a.Buffer > 0 {
		bufTxt = Tf("Tampon : %d échantillons (%.1f ms)", a.Buffer, u.bufferMs(a.Buffer))
	}
	items := []olItem{
		{text: T("Appliquer et redémarrer l'audio"), sub: firstNonEmpty(u.audioInfo, u.audioErr), action: func() {
			u.StartAudio()
			u.openAudioMenu()
		}},
		{text: Tf("Pilote : %s", apiLabel(u.audioAPI())), sub: u.apiSub(u.audioAPI()), action: u.openAPIMenu},
		{text: Tf("Carte son : %s", dev), action: u.openDeviceMenu},
		{text: Tf("Fréquence : %d Hz", a.Rate), action: u.openRateMenu},
		{text: bufTxt, sub: T("Plus petit = moins de latence, mais plus de risque de craquements."), action: u.openBufferMenu},
	}
	items = append(items, olItem{text: T("Test du son : bip de 2 secondes"), sub: u.diagText(), action: func() {
		u.eng.StartTone()
		u.Toast(T("Bip en cours : l'entends-tu ? Puis rouvre ce menu pour lire le diagnostic."))
	}})
	if u.audioAPI() == apiASIO {
		items = append(items,
			olItem{text: Tf("Sorties ASIO : %d-%d", a.Pair*2+1, a.Pair*2+2), action: u.openPairMenu},
			olItem{text: T("Panneau de réglages du pilote"), action: func() { u.host.AudioPanel(); u.openAudioMenu() }})
	}
	u.ov = &overlay{title: T("Sortie audio"), items: items, hint: T("Clique sur un réglage · Échap pour fermer"), back: u.OpenSettings}
	u.ov.sel = u.ov.firstSelectable()
	u.host.Invalidate()
}

func firstNonEmpty(a ...string) string {
	for _, s := range a {
		if s != "" {
			return s
		}
	}
	return ""
}

func (u *UI) subMenu(title string, items []olItem) {
	u.ov = &overlay{title: T(title), items: items, hint: T("Clique sur un réglage · Échap pour fermer"), back: u.openAudioMenu}
	u.ov.sel = u.ov.firstSelectable()
	u.host.Invalidate()
}

func (u *UI) openAPIMenu() {
	var items []olItem
	for _, api := range u.host.AudioAPIs() {
		api := api
		mark := "   "
		if api == u.audioAPI() {
			mark = "•  "
		}
		items = append(items, olItem{text: mark + apiLabel(api), sub: u.apiSub(api), action: func() {
			u.cfg.Audio.API = api
			u.cfg.Audio.Device = ""
			if api == apiASIO { // 48 kHz et tampon court par défaut avec ASIO
				if u.cfg.Audio.Buffer == 0 {
					u.cfg.Audio.Buffer = 128
				}
				if names := u.host.AudioDevices(api); len(names) == 1 {
					u.cfg.Audio.Device = names[0]
				}
			}
			u.touchAudio()
			u.openAudioMenu()
		}})
	}
	u.subMenu("Choisir le pilote audio", items)
}

func (u *UI) openDeviceMenu() {
	items := []olItem{{text: T(defaultDeviceName), action: func() { u.cfg.Audio.Device = ""; u.touchAudio(); u.openAudioMenu() }}}
	if u.audioAPI() == apiASIO {
		items = nil
	}
	names := u.host.AudioDevices(u.audioAPI())
	for _, n := range names {
		n := n
		mark := "   "
		if n == u.cfg.Audio.Device {
			mark = "•  "
		}
		items = append(items, olItem{text: mark + n, action: func() { u.cfg.Audio.Device = n; u.touchAudio(); u.openAudioMenu() }})
	}
	if len(items) == 0 {
		items = []olItem{{text: T("Aucun pilote trouvé"), action: u.openAudioMenu}}
	}
	u.subMenu("Choisir la carte son", items)
}

func (u *UI) openRateMenu() {
	var items []olItem
	for _, r := range rateChoices {
		r := r
		mark := "   "
		if r == u.cfg.Audio.Rate {
			mark = "•  "
		}
		items = append(items, olItem{text: mark + fmt.Sprintf("%d Hz", r), action: func() { u.cfg.Audio.Rate = r; u.touchAudio(); u.openAudioMenu() }})
	}
	u.subMenu("Choisir la fréquence", items)
}

func (u *UI) openBufferMenu() {
	items := []olItem{}
	for _, b := range bufferChoices {
		b := b
		mark := "   "
		if b == u.cfg.Audio.Buffer {
			mark = "•  "
		}
		items = append(items, olItem{text: mark + Tf("Tampon : %d échantillons (%.1f ms)", b, u.bufferMs(b)),
			action: func() { u.cfg.Audio.Buffer = b; u.touchAudio(); u.openAudioMenu() }})
	}
	items = append(items, olItem{text: "   " + T("Tampon : automatique"), action: func() { u.cfg.Audio.Buffer = 0; u.touchAudio(); u.openAudioMenu() }})
	u.subMenu("Choisir la taille du tampon", items)
}

func (u *UI) openPairMenu() {
	var items []olItem
	for p := 0; p < 8; p++ {
		p := p
		mark := "   "
		if p == u.cfg.Audio.Pair {
			mark = "•  "
		}
		items = append(items, olItem{text: mark + Tf("Sorties ASIO : %d-%d", p*2+1, p*2+2), action: func() { u.cfg.Audio.Pair = p; u.touchAudio(); u.openAudioMenu() }})
	}
	u.subMenu("Choisir les sorties", items)
}

func (u *UI) touchAudio() {
	u.cfg.Audio.sanitize()
	u.dirty = true // enregistre dans les réglages du logiciel (pas dans le projet)
}

// ---------------------------------------------------------------- fenêtre principale des paramètres

func (u *UI) OpenSettings() {
	langs := []struct {
		l    int
		name string
	}{{langFR, "Français"}, {langEN, "English"}, {langES, "Español"}}
	var items []olItem
	if !u.Plugin {
		items = append(items, olItem{text: T("Sortie audio"), header: true})
		sub := firstNonEmpty(u.audioInfo, u.audioErr)
		items = append(items, olItem{text: T("Pilote, carte son, fréquence, tampon"), sub: sub, action: u.openAudioMenu})
	}
	items = append(items, olItem{text: T("Langue"), header: true})
	for _, l := range langs {
		l := l
		mark := "   "
		if CurLang() == l.l {
			mark = "•  "
		}
		items = append(items, olItem{text: mark + l.name, action: func() {
			SetLang(l.l)
			u.cfg.Lang = langCode(l.l)
			u.setNotation()
			u.dirty = true
			u.OpenSettings()
		}})
	}
	items = append(items, olItem{text: T("Accords"), header: true})
	notTxt := T("Notation des accords : Do Ré Mi")
	if strings.HasPrefix(u.namer.Names[0], "C") && u.namer.Names[0] == "C" {
		notTxt = T("Notation des accords : lettres (C D E)")
	}
	items = append(items, olItem{text: notTxt, sub: T("touche pour changer"), action: func() {
		if u.namer.Names[0] == "C" {
			u.cfg.Notation = "solfege"
		} else {
			u.cfg.Notation = "abc"
		}
		u.setNotation()
		u.dirty = true
		u.OpenSettings()
	}})
	items = append(items, u.playingSettingsItems()...)
	if !u.Plugin {
		items = append(items, olItem{text: "MIDI", header: true})
		ms := u.midiStatus
		if ms == "" {
			ms = T("MIDI : aucun clavier")
		}
		items = append(items, olItem{text: T("Actualiser MIDI"), sub: ms, action: func() {
			if u.OnRescan != nil {
				u.OnRescan()
			}
			u.OpenSettings()
		}})
		if u.OnBluetooth != nil { // Android : clavier MIDI Bluetooth à rechercher
			items = append(items, olItem{text: T("Connecter un clavier MIDI Bluetooth…"), sub: T("USB : branche-le, il est reconnu tout seul"), action: func() {
				u.ov = nil
				u.OnBluetooth()
			}})
		}
	}
	u.ov = &overlay{title: T("Paramètres de Layers Keys"), items: items, hint: T("Clique sur un réglage · Échap pour fermer")}
	u.ov.sel = u.ov.firstSelectable()
	u.host.Invalidate()
}

// texte de diagnostic du son : la carte son demande-t-elle du son ? le moteur en produit-il ? la licence l'autorise-t-elle ?
func (u *UI) diagText() string {
	pulled, peak := u.eng.DiagRead()
	lic := "licence ?"
	switch u.licSt.State {
	case LicLicensed:
		lic = "licencié"
	case LicDemo:
		lic = "démo"
	default:
		lic = "son verrouillé"
	}
	loaded := 0
	for i := range u.eng.ch {
		if src, _, _ := u.eng.ChInfo(i); src != nil {
			loaded++
		}
	}
	return Tf("demandé par la carte : %d · niveau : %.2f · %s · %d son(s)", pulled, peak, T(lic), loaded)
}
