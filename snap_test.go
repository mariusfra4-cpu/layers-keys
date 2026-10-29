package main

import (
	"os"
	"testing"
)

// instantané de l'écran pour vérifier qu'une optimisation du dessin ne change pas l'image
func snapScreens(t *testing.T) []uint32 {
	u, _, _ := newTestUI(t, "")
	c := NewCanvas(WinW, WinH)
	var all []uint32
	u.Draw(c)
	all = append(all, append([]uint32(nil), c.Pix...)...)
	u.openSoundMenu(0)
	u.Draw(c)
	all = append(all, append([]uint32(nil), c.Pix...)...)
	u.OpenVelocity()
	u.Draw(c)
	all = append(all, append([]uint32(nil), c.Pix...)...)
	return all
}

func TestSnapshot(t *testing.T) {
	ref := os.Getenv("SNAP_REF")
	if ref == "" {
		t.Skip()
	}
	cur := snapScreens(t)
	if os.Getenv("SNAP_WRITE") != "" {
		b := make([]byte, 0, len(cur)*4)
		for _, p := range cur {
			b = append(b, byte(p), byte(p>>8), byte(p>>16), byte(p>>24))
		}
		os.WriteFile(ref, b, 0o644)
		return
	}
	b, err := os.ReadFile(ref)
	if err != nil || len(b) != len(cur)*4 {
		t.Fatal("référence absente")
	}
	maxd, bad := 0, 0
	for i, p := range cur {
		q := uint32(b[4*i]) | uint32(b[4*i+1])<<8 | uint32(b[4*i+2])<<16 | uint32(b[4*i+3])<<24
		for s := uint(0); s < 24; s += 8 {
			d := int(p>>s&255) - int(q>>s&255)
			if d < 0 {
				d = -d
			}
			if d > maxd {
				maxd = d
			}
			if d > 2 {
				bad++
			}
		}
	}
	t.Logf("écart max %d, pixels hors tolérance %d", maxd, bad)
	if bad > 0 {
		t.Fatalf("l'image a changé (écart max %d)", maxd)
	}
}
