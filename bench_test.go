package main

import (
	"testing"
	"time"
)

func TestDrawSpeed(t *testing.T) {
	u, _, _ := newTestUI(t, "")
	c := NewCanvas(WinW, WinH)
	u.Draw(c)
	t0 := time.Now()
	for i := 0; i < 20; i++ {
		u.Draw(c)
	}
	per := time.Since(t0) / 20
	u.openSoundMenu(0)
	t1 := time.Now()
	for i := 0; i < 20; i++ {
		u.Draw(c)
	}
	t.Logf("dessin : %v par image ; avec fenêtre ouverte : %v", per, time.Since(t1)/20)
	if per > 80*time.Millisecond {
		t.Errorf("dessin trop lent : %v", per)
	}
}
