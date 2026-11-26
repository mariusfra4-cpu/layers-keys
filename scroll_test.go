package main

import "testing"

// Paramètres : la barre de défilement s'attrape et fait descendre la liste, la molette aussi
func TestSettingsScrollBar(t *testing.T) {
	u, _, _ := newTestUI(t, "")
	u.OpenSettings()
	cv := NewCanvas(WinW, WinH)
	u.Draw(cv)
	var sb *region
	for i := range u.regions {
		if u.regions[i].id == "ovscroll" {
			r := u.regions[i] // copie : la liste des zones est réécrite à chaque dessin
			sb = &r
		}
	}
	if sb == nil {
		t.Fatal("la liste des Paramètres devrait avoir une barre de défilement")
	}
	x := sb.r.x + sb.r.w/2
	u.MouseDown(x, sb.r.y+2)
	u.MouseMove(x, sb.r.y+sb.r.h-2, true)
	u.MouseUp(x, sb.r.y+sb.r.h-2)
	if u.ov.scroll == 0 {
		t.Fatal("en tirant la barre vers le bas, la liste devrait descendre")
	}
	u.Draw(cv)
	u.mx, u.my = sb.r.x-100, sb.r.y+20
	before := u.ov.scroll
	u.Wheel(120)
	if u.ov.scroll >= before {
		t.Fatal("la molette vers le haut devrait faire remonter la liste")
	}
}
