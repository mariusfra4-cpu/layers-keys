package main

import (
	"fmt"
	"os"
	"testing"
)

// images du PLUGIN (u.Plugin = vrai) : mêmes écrans que le logiciel autonome (PLUGSHOTS=dossier)
func TestPluginShots(t *testing.T) {
	dir := os.Getenv("PLUGSHOTS")
	if dir == "" {
		t.Skip()
	}
	u, _, _ := newTestUI(t, "")
	u.Plugin = true
	u.PluginInfo = func() (int, bool, int) { return 12, true, 0 }
	u.OnShowLog = func() {}
	u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
	u.labels[0] = "Grand Piano"
	shotPNG(t, dir, "plugin_1_principal", u)
	var ss []LibSound
	for i := 0; i < 30; i++ {
		n := fmt.Sprintf("Lead %02d", i+1)
		ss = append(ss, LibSound{Name: n, File: "/sons/Leads/" + n + ".wav", Kind: "audio", Cat: catLead})
	}
	for _, n := range []string{"Concert Grand", "Rhodes Suitcase", "Hammond B3", "Percussive Organ", "Warm Pad", "String Ensemble", "Finger Bass", "Drum Kit", "Supersaw"} {
		ss = append(ss, LibSound{Name: n, File: "/sons/" + n + ".wav", Kind: "audio", Cat: libCat(n, "", false, 0, 0, 1, nil)})
	}
	sortLib(ss)
	u.libs.mu.Lock()
	u.libs.lib = &Library{Version: libVersion, Dir: "/sons", Sounds: ss, Pads: defaultPadCfg()}
	u.libs.mu.Unlock()
	u.openSoundMenu(0)
	cv := NewCanvas(WinW, WinH)
	u.Draw(cv)
	for i := range u.regions {
		if u.regions[i].id == fmt.Sprintf("libbar%d", catLead) {
			r := u.regions[i].r
			u.MouseMove(r.x+r.w/2, r.y+40, false)
		}
	}
	shotPNG(t, dir, "plugin_2_sons_barre_souris", u)
	u.closeOverlay()
	u.OpenSettings()
	u.Draw(cv)
	for i := range u.regions {
		if u.regions[i].id == "ovscroll" {
			r := u.regions[i].r
			u.MouseDown(r.x+r.w/2, r.y+5)
			u.MouseMove(r.x+r.w/2, r.y+r.h-5, true)
			u.MouseUp(r.x+r.w/2, r.y+r.h-5)
		}
	}
	shotPNG(t, dir, "plugin_3_parametres_bas", u)
}
