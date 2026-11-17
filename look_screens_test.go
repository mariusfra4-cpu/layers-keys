//go:build !windows

package main

import (
	"os"
	"testing"
)

// captures de contrôle de tous les écrans avec un thème (LOOKSHOTS=dossier)
func TestLookScreens(t *testing.T) {
	dir := os.Getenv("LOOKSHOTS")
	if dir == "" {
		t.Skip()
	}
	u, _, _ := newTestUI(t, "")
	u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
	u.setLook(func(c *Config) { c.LookTheme = 2 })
	defer u.setLook(func(c *Config) { c.LookTheme = 0 })
	for _, s := range []struct {
		name string
		open func()
	}{
		{"30_sons", func() { u.openSoundMenu(0) }},
		{"31_pads", u.OpenPads},
		{"32_eq", u.OpenEQ},
		{"33_effets", u.OpenFXWin},
		{"34_velocite", u.OpenVelocity},
		{"35_audio", u.openAudioMenu},
		{"36_projet", u.OpenProjectMenu},
	} {
		s.open()
		shotPNG(t, dir, s.name, u)
		u.closeOverlay()
	}
	u.licShow = true
	shotPNG(t, dir, "37_licence", u)
	u.licShow = false

	d := newTestDroid(t)
	d.post(func() { d.ui.setLook(func(c *Config) { c.LookTheme = 2 }) })
	os.Setenv("DSHOTS", dir)
	defer os.Unsetenv("DSHOTS")
	droidShot(t, d, "38_android")
	d.post(func() { d.ui.setLook(func(c *Config) { c.LookTheme = 0 }) })
	d.sync()
}
