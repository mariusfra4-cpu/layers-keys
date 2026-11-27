package main

import "testing"

// temps de dessin d'un écran complet (le dessin partage le verrou de l'interface avec l'arrivée des notes MIDI)
func BenchmarkDrawMain(b *testing.B) {
	u, _, _ := newTestUI(b, "")
	c := NewCanvas(WinW, WinH)
	for _, n := range []int{53, 57, 60, 64} {
		u.counts[n] = 1
	}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		u.Draw(c)
		c.Texts = c.Texts[:0]
		c.AllTexts = c.AllTexts[:0]
	}
}

func BenchmarkDrawThemed(b *testing.B) {
	u, _, _ := newTestUI(b, "")
	u.setLook(func(c *Config) { c.LookTheme = 2 })
	defer u.setLook(func(c *Config) { c.LookTheme = 0 })
	c := NewCanvas(WinW, WinH)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		u.Draw(c)
		c.Texts = c.Texts[:0]
		c.AllTexts = c.AllTexts[:0]
	}
}
