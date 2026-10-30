package main

import (
	"sort"
	"testing"
)

type tmpl struct {
	suf   string
	iv    []int
	minor bool
	inv   bool // les renversements ont un nom sans ambiguïté
}

var chordTemplates = []tmpl{
	{"", []int{0, 4, 7}, false, true},
	{"m", []int{0, 3, 7}, true, true},
	{"dim", []int{0, 3, 6}, true, true},
	{"aug", []int{0, 4, 8}, false, false},
	{"sus2", []int{0, 2, 7}, false, false},
	{"sus4", []int{0, 5, 7}, false, false},
	{"Maj7", []int{0, 4, 7, 11}, false, true},
	{"7", []int{0, 4, 7, 10}, false, true},
	{"m7", []int{0, 3, 7, 10}, true, false}, // m7 = 6 d'une autre tonalité
	{"m7b5", []int{0, 3, 6, 10}, true, false},
	{"dim7", []int{0, 3, 6, 9}, true, false},
	{"m(Maj7)", []int{0, 3, 7, 11}, true, false},
	{"6", []int{0, 4, 7, 9}, false, false},
	{"m6", []int{0, 3, 7, 9}, true, false},
	{"6/9", []int{0, 2, 4, 7, 9}, false, false},
	{"(add9)", []int{0, 2, 4, 7}, false, false},
	{"m(add9)", []int{0, 2, 3, 7}, true, false},
	{"9", []int{0, 2, 4, 7, 10}, false, true},
	{"Maj9", []int{0, 2, 4, 7, 11}, false, true},
	{"m9", []int{0, 2, 3, 7, 10}, true, false},
	{"11", []int{0, 2, 4, 5, 7, 10}, false, false},
	{"13", []int{0, 2, 4, 7, 9, 10}, false, false},
	{"7sus4", []int{0, 5, 7, 10}, false, false},
	{"7(b9)", []int{0, 1, 4, 7, 10}, false, true},
	{"7(#9)", []int{0, 3, 4, 7, 10}, false, true},
	{"7(#11)", []int{0, 4, 6, 7, 10}, false, true},
	{"7(#5)", []int{0, 4, 8, 10}, false, true},
	{"Maj7(#11)", []int{0, 4, 6, 7, 11}, false, true},
}

// accord en position fondamentale, dans les 12 tonalités : le nom doit être exact
func TestChordsAllRoots(t *testing.T) {
	for root := 0; root < 12; root++ {
		for _, tp := range chordTemplates {
			var notes []int
			for _, i := range tp.iv {
				notes = append(notes, 48+root+i)
			}
			sh := sharpFamily(root, tp.minor)
			want := lettersNamer.join(lettersNamer.nm(root, sh), tp.suf)
			if got := lettersNamer.Detect(notes).Name; got != want {
				t.Errorf("racine %d %q : %q (attendu %q) notes %v", root, tp.suf, got, want, notes)
			}
		}
	}
}

// renversements : on doit retrouver la vraie fondamentale (« F Maj7/A », jamais « Am(b13) »)
func TestChordsInversions(t *testing.T) {
	for root := 0; root < 12; root++ {
		for _, tp := range chordTemplates {
			if !tp.inv {
				continue
			}
			sh := sharpFamily(root, tp.minor)
			for k := 1; k < len(tp.iv); k++ {
				if tp.suf == "7(#5)" && tp.iv[k] == 8 {
					continue // gamme par tons : Ab aug(9) et C7(#5)/Ab sont deux lectures aussi justes
				}
				switch tp.iv[k] {
				case 3, 4, 6, 7, 8, 10, 11:
				default:
					continue // la basse doit être une note de l'accord (tierce, quinte, septième)
				}
				var notes []int
				for j := k; j < len(tp.iv); j++ {
					notes = append(notes, 48+root+tp.iv[j])
				}
				for j := 0; j < k; j++ {
					notes = append(notes, 60+root+tp.iv[j])
				}
				want := lettersNamer.join(lettersNamer.nm(root, sh), tp.suf) + "/" + lettersNamer.nm((root+tp.iv[k])%12, sh)
				if got := lettersNamer.Detect(notes).Name; got != want {
					t.Errorf("renversement racine %d %q basse +%d : %q (attendu %q) notes %v", root, tp.suf, tp.iv[k], got, want, notes)
				}
			}
		}
	}
}

// les cas reportés comme faux par l'utilisateur
func TestChordsRapportes(t *testing.T) {
	for _, c := range []struct {
		notes []int
		want  string
	}{
		{[]int{53, 57, 60, 64}, "F Maj7"},
		{[]int{45, 48, 52, 53}, "F Maj7/A"},
		{[]int{48, 52, 53, 57}, "F Maj7/C"},
		{[]int{52, 53, 57, 60}, "F Maj7/E"},
		{[]int{52, 55, 59, 60}, "C Maj7/E"},
		{[]int{47, 50, 53, 55}, "G7/B"},
		{[]int{48, 52, 55, 57}, "C6"},
		{[]int{57, 60, 64, 67}, "Am7"},
		{[]int{52, 55, 57, 60}, "Am7/E"},
		{[]int{50, 53, 57, 60}, "Dm7"},
		{[]int{53, 57, 60, 62}, "F6"},
		{[]int{45, 48, 53}, "F/A"},
		{[]int{43, 47, 50, 53}, "G7"},
		{[]int{40, 43, 47, 50}, "Em7"},
		{[]int{52, 56, 59, 62}, "E7"},
	} {
		if got := lettersNamer.Detect(c.notes).Name; got != c.want {
			t.Errorf("%v : %q (attendu %q)", c.notes, got, c.want)
		}
	}
}

// vérité : le nom ne cache jamais une note. Pour tout ensemble de notes, soit pas de nom, soit un nom qui les explique toutes.
func TestChordsPasDeNoteCachee(t *testing.T) {
	// toutes les combinaisons de 3 à 5 notes dans une octave, fondamentale 0
	var rec func(start int, cur []int)
	checked := 0
	rec = func(start int, cur []int) {
		if len(cur) >= 3 {
			notes := append([]int(nil), cur...)
			sort.Ints(notes)
			var mask uint16
			for _, v := range notes {
				mask |= 1 << uint(v%12)
			}
			r := lettersNamer.Detect(notes)
			if r.Name != "" {
				// retrouver la fondamentale et vérifier que describe accepte tout
				ok := false
				for root := 0; root < 12; root++ {
					var m uint16
					for _, v := range notes {
						m |= 1 << uint(((v%12)-root+12)%12)
					}
					if _, _, _, good := describe(m); good {
						ok = true
					}
				}
				if !ok {
					t.Errorf("nom %q sans lecture complète pour %v", r.Name, notes)
				}
			}
			checked++
		}
		if len(cur) == 5 {
			return
		}
		for v := start; v < 12; v++ {
			rec(v+1, append(cur, 48+v))
		}
	}
	rec(0, nil)
	if checked < 500 {
		t.Fatalf("trop peu de combinaisons testées : %d", checked)
	}
}
