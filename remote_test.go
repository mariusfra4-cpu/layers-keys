package main

import (
	"os"
	"regexp"
	"strconv"
	"testing"
)

// les numéros doivent être identiques dans l'application Android et dans Layers Keys
func TestRemoteNumbersMatchAndroidApp(t *testing.T) {
	for _, f := range []string{"android-remote/src/com/jbpraise/layersremote/MidiMap.java", "ios-remote/Sources/MidiMap.swift"} {
		b, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		get := func(name string) int {
			m := regexp.MustCompile(`\b` + name + `\s*=\s*(\d+)`).FindSubmatch(b)
			if m == nil {
				t.Fatalf("%s introuvable dans %s", name, f)
			}
			n, _ := strconv.Atoi(string(m[1]))
			return n
		}
		for name, want := range map[string]int{"VOL_BASE": remVolBase, "MUTE_BASE": remMuteBase, "SOLO_BASE": remSoloBase,
			"HOLD_BASE": remHoldBase, "MASTER": remMaster, "REVERB": remReverb, "LESLIE": remLeslie} {
			if got := get(name); got != want {
				t.Errorf("%s : %d dans %s, %d dans Layers Keys", name, got, f, want)
			}
		}
	}
	for cc := range remoteMap() {
		if learnProtected(cc) {
			t.Errorf("CC %d réservé par Layers Keys", cc)
		}
	}
	if len(remoteMap()) != 35 {
		t.Errorf("%d commandes (35 attendues)", len(remoteMap()))
	}
}

func TestRemoteDrivesLayersKeys(t *testing.T) {
	u, _, e := newTestUI(t, "")
	u.learnStore(map[int]string{21: "reverb", 90: "master"}) // association faite à la main avant : 21 est remplacé, 90 gardé
	u.remoteLink()
	if !u.remoteLinked() {
		t.Fatal("la télécommande devrait être associée")
	}
	// le volume général passe par le CC 28 de la télécommande : l'ancienne association du même réglage (CC 90) est retirée
	if _, still := u.learnMap()[90]; still {
		t.Error("deux CC pour le même réglage : l'ancien aurait dû être retiré")
	}
	cc := func(n, v int) { u.MidiFast(0xB0 | uint32(n)<<8 | uint32(v)<<16) }
	cc(remVolBase+2, 0) // fader 3 en bas
	if e.ch[2].Vol > -59 {
		t.Errorf("volume de la piste 3 : %.1f dB", e.ch[2].Vol)
	}
	cc(remVolBase+2, 127)
	if e.ch[2].Vol < 5 {
		t.Errorf("volume de la piste 3 en haut : %.1f dB", e.ch[2].Vol)
	}
	// M de la piste 5 : appui (127) puis relâchement (0) = bascule
	cc(remMuteBase+4, 127)
	cc(remMuteBase+4, 0)
	if !e.ch[4].Mute {
		t.Fatal("la piste 5 devrait être muette")
	}
	cc(remMuteBase+4, 127)
	cc(remMuteBase+4, 0)
	if e.ch[4].Mute {
		t.Fatal("deuxième appui : la piste 5 devrait rejouer")
	}
	cc(remSoloBase, 127)
	cc(remSoloBase, 0)
	cc(remHoldBase+7, 127)
	cc(remHoldBase+7, 0)
	if !e.ch[0].Solo || !e.ch[7].Hold {
		t.Fatal("solo piste 1 / hold piste 8")
	}
	cc(remLeslie, 127)
	cc(remLeslie, 0)
	if !e.LeslieFast() {
		t.Fatal("Leslie rapide attendu")
	}
	cc(remMaster, 64)
	cc(remReverb, 0)
	if e.master < 0.49 || e.master > 0.52 || e.revAmt != 0 {
		t.Fatalf("général %.2f, réverbération %.2f", e.master, e.revAmt)
	}
	// preset 3 depuis la télécommande (Program Change 2)
	u.cfg.Presets[2].Chans[0].Vol = -12
	u.MidiFast(0xC0 | 2<<8)
	u.Tick()
	if u.cfg.Cur != 2 || e.ch[0].Vol != -12 {
		t.Fatalf("preset 3 : actuel %d, volume piste 1 %.1f", u.cfg.Cur, e.ch[0].Vol)
	}
	u.MidiFast(0xC0 | 40<<8) // au-delà des 8 presets : ignoré
	u.Tick()
	if u.cfg.Cur != 2 {
		t.Fatal("un programme hors des 8 presets ne doit rien changer")
	}
	// l'association est gardée dans le projet du DAW (plugin)
	u2, _, _ := newTestUI(t, "")
	if !u2.SetStateBytes(u.StateBytes()) || !u2.remoteLinked() {
		t.Fatal("association de la télécommande perdue dans l'état du plugin")
	}
	u.OpenSettings() // l'entrée des Paramètres s'affiche
	drawUI(u)
}
