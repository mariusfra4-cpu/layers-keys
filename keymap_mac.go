package main

// Touches du clavier Mac → codes que comprend l'interface (codes de touches virtuelles et scancodes Windows).
// Les notes du clavier de l'ordinateur suivent la POSITION des touches (comme sur Windows), pas la lettre imprimée :
// un clavier AZERTY joue donc les mêmes notes qu'un clavier QWERTY.

type macKeyInfo struct {
	vk  int    // code de touche virtuelle (disposition américaine)
	sc  uint32 // scancode Windows de la même touche physique
	ext bool   // touche « étendue » (flèches, début, fin…)
}

// code matériel Mac (kVK_*) → touche
var macKeys = map[int]macKeyInfo{
	// ligne des chiffres
	50: {0xC0, 0x29, false}, 18: {'1', 0x02, false}, 19: {'2', 0x03, false}, 20: {'3', 0x04, false}, 21: {'4', 0x05, false},
	23: {'5', 0x06, false}, 22: {'6', 0x07, false}, 26: {'7', 0x08, false}, 28: {'8', 0x09, false}, 25: {'9', 0x0A, false},
	29: {'0', 0x0B, false}, 27: {0xBD, 0x0C, false}, 24: {0xBB, 0x0D, false}, 51: {0x08, 0x0E, false},
	// ligne du haut
	48: {0x09, 0x0F, false}, 12: {'Q', 0x10, false}, 13: {'W', 0x11, false}, 14: {'E', 0x12, false}, 15: {'R', 0x13, false},
	17: {'T', 0x14, false}, 16: {'Y', 0x15, false}, 32: {'U', 0x16, false}, 34: {'I', 0x17, false}, 31: {'O', 0x18, false},
	35: {'P', 0x19, false}, 33: {0xDB, 0x1A, false}, 30: {0xDD, 0x1B, false}, 36: {0x0D, 0x1C, false}, 42: {0xDC, 0x2B, false},
	// ligne du milieu
	0: {'A', 0x1E, false}, 1: {'S', 0x1F, false}, 2: {'D', 0x20, false}, 3: {'F', 0x21, false}, 5: {'G', 0x22, false},
	4: {'H', 0x23, false}, 38: {'J', 0x24, false}, 40: {'K', 0x25, false}, 37: {'L', 0x26, false}, 41: {0xBA, 0x27, false},
	39: {0xDE, 0x28, false},
	// ligne du bas
	6: {'Z', 0x2C, false}, 7: {'X', 0x2D, false}, 8: {'C', 0x2E, false}, 9: {'V', 0x2F, false}, 11: {'B', 0x30, false},
	45: {'N', 0x31, false}, 46: {'M', 0x32, false}, 43: {0xBC, 0x33, false}, 47: {0xBE, 0x34, false}, 44: {0xBF, 0x35, false},
	10: {0xE2, 0x56, false}, // touche « < > » des claviers ISO
	49: {0x20, 0x39, false}, // espace
	76: {0x0D, 0x1C, true},  // Entrée du pavé numérique
	53: {0x1B, 0x01, false}, // échap
	// déplacements
	123: {0x25, 0x4B, true}, 124: {0x27, 0x4D, true}, 125: {0x28, 0x50, true}, 126: {0x26, 0x48, true},
	115: {0x24, 0x47, true}, 119: {0x23, 0x4F, true}, 116: {0x21, 0x49, true}, 121: {0x22, 0x51, true}, 117: {0x2E, 0x53, true},
	// touches de fonction
	122: {0x70, 0x3B, false}, 120: {0x71, 0x3C, false}, 99: {0x72, 0x3D, false}, 118: {0x73, 0x3E, false},
	96: {0x74, 0x3F, false}, 97: {0x75, 0x40, false}, 98: {0x76, 0x41, false}, 100: {0x77, 0x42, false},
	101: {0x78, 0x43, false}, 109: {0x79, 0x44, false}, 103: {0x7A, 0x57, false}, 111: {0x7B, 0x58, false},
}

// macKeyToWin : (code virtuel, scancode, étendue, connue). ch = lettre tapée sans modificateur (peut être vide) :
// pour les raccourcis (Cmd+S…), on suit la lettre imprimée sur la touche, quelle que soit la disposition.
func macKeyToWin(code int, ch rune) (vk int, sc uint32, ext bool, ok bool) {
	k, found := macKeys[code]
	if !found {
		return 0, 0, false, false
	}
	vk = k.vk
	switch {
	case ch >= 'a' && ch <= 'z':
		vk = int(ch - 'a' + 'A')
	case ch >= 'A' && ch <= 'Z', ch >= '0' && ch <= '9':
		vk = int(ch)
	}
	return vk, k.sc, k.ext, true
}

// macKeyCodeForScan : scancode Windows → code matériel Mac (pour écrire la bonne lettre sur chaque touche du piano)
func macKeyCodeForScan(sc uint32) (int, bool) {
	for code, k := range macKeys {
		if k.sc == sc && !k.ext {
			return code, true
		}
	}
	return 0, false
}

// Modificateurs de NSEvent
const (
	macModShift   = 1 << 17
	macModControl = 1 << 18
	macModOption  = 1 << 19
	macModCommand = 1 << 20
)

// "ctrl" pour l'interface = Commande ou Contrôle sur Mac
func macMods(flags uint64) (ctrl, shift bool) {
	return flags&(macModCommand|macModControl) != 0, flags&macModShift != 0
}
