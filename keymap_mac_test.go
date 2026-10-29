package main

import "testing"

func TestMacKeyMap(t *testing.T) {
	// chaque touche piano du clavier de l'ordinateur doit exister sur le Mac
	for _, sc := range append(append([]uint32{}, scBottom...), scTop...) {
		code, ok := macKeyCodeForScan(sc)
		if !ok {
			t.Fatalf("scancode %#x sans touche Mac", sc)
		}
		_, sc2, _, ok := macKeyToWin(code, 0)
		if !ok || sc2 != sc {
			t.Fatalf("aller-retour du scancode %#x : %v %#x", sc, ok, sc2)
		}
	}
	// scancodes tous différents
	seen := map[uint32]int{}
	for code, k := range macKeys {
		key := k.sc
		if k.ext {
			key |= 0x100
		}
		if o, dup := seen[key]; dup {
			t.Fatalf("scancode %#x donné aux touches %d et %d", k.sc, o, code)
		}
		seen[key] = code
	}
	// Z = touche 6 (même position sur QWERTY) ; A de l'AZERTY est à la place de Q
	if vk, sc, _, _ := macKeyToWin(6, 0); vk != 'Z' || sc != 0x2C {
		t.Fatalf("Z : %v %v", vk, sc)
	}
	if vk, sc, _, _ := macKeyToWin(12, 'a'); vk != 'A' || sc != 0x10 {
		t.Fatalf("A sur AZERTY (touche Q) : %v %v", vk, sc)
	}
	if vk, _, ext, _ := macKeyToWin(126, 0); vk != 0x26 || !ext {
		t.Fatal("flèche haut")
	}
	if c, s := macMods(macModCommand | macModShift); !c || !s {
		t.Fatal("modificateurs")
	}
	if c, _ := macMods(macModOption); c {
		t.Fatal("Option n'est pas Ctrl")
	}
}
