package main

// Moteur de reconnaissance d'accords : triades, septièmes, neuvièmes, onzièmes, treizièmes,
// accords altérés, suspendus, « add », accords sur basse (slash) et renversements.

import (
	"sort"
	"strings"
)

var noteNamesLetters = [12]string{"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"}
var noteNamesSolfege = [12]string{"Do", "Réb", "Ré", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si"}
var noteNamesSolfegeES = [12]string{"Do", "Reb", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si"}

var noteNamesLettersSharp = [12]string{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}
var noteNamesSolfegeSharp = [12]string{"Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"}
var noteNamesSolfegeESSharp = [12]string{"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"}

// ChordResult : ce que l'on affiche pour un ensemble de notes tenues.
type ChordResult struct {
	Name  string   // « Cmaj7/E », vide s'il n'y a pas d'accord
	Notes []string // notes jouées, de la plus grave à la plus aiguë
}

// ChordNamer choisit l'écriture des notes (lettres ou Do Ré Mi).
// Names = écriture avec bémols, Sharps = écriture avec dièses (choisie selon la tonalité de l'accord),
// Spaced = « Do maj7 » au lieu de « Domaj7 ».
type ChordNamer struct {
	Names  [12]string
	Sharps [12]string
	Spaced bool
}

var lettersNamer = ChordNamer{Names: noteNamesLetters, Sharps: noteNamesLettersSharp}

func (n ChordNamer) nm(pc int, sharp bool) string {
	if sharp && n.Sharps[pc] != "" {
		return n.Sharps[pc]
	}
	return n.Names[pc]
}

func (n ChordNamer) join(root, suf string) string {
	if n.Spaced && suf != "" {
		return root + " " + suf
	}
	return root + suf
}

// sharpFamily : la tonalité de l'accord s'écrit-elle avec des dièses ? (Em/G#, E/G#, F#, C#m, G#m...)
func sharpFamily(root int, minor bool) bool {
	r := root
	if minor {
		r = (root + 3) % 12
	}
	switch r {
	case 2, 4, 7, 9, 11:
		return true
	case 6:
		return !(minor && root == 3)
	}
	return false
}

type cand struct {
	name string
	cost float64
}

// nameFrom construit le nom d'un accord pour une fondamentale donnée ; mask = intervalles présents (bit 0 = fondamentale).
func nameFrom(mask uint16, hasRoot bool) (suffix string, minor bool, cost float64) {
	has := func(i int) bool { return mask&(1<<uint(i)) != 0 }
	m3, M3 := has(3), has(4)
	s2, s4 := has(2), has(5)
	b5, p5, s5 := has(6), has(7), has(8)
	d7, m7, M7 := has(9), has(10), has(11)
	b9, n9, s9 := has(1), has(2), has(3)
	n11, s11 := has(5), has(6)
	_ = n9

	var quality string // "", "m", "dim", "aug", "sus2", "sus4", "5"
	third := true
	switch {
	case M3:
		quality = ""
	case m3:
		quality = "m"
	case s4:
		quality, third = "sus4", false
	case s2:
		quality, third = "sus2", false
	default:
		third = false
		if p5 && !(m7 || M7 || d7 || b9) {
			return "5", false, 0.5
		}
		cost += 6 // pas de tierce : lecture peu naturelle
	}
	if m3 && M3 { // tierce mineure ET majeure : la mineure est une #9
		quality = ""
	}

	// quinte
	fifth := ""
	switch {
	case p5:
	case b5 && !p5:
		fifth = "b5"
	case s5 && !p5:
		fifth = "#5"
	}
	// septième
	seventh := 0 // 0 aucune, 7 = b7, 11 = maj7, 6 = sixte, 9 = dim7
	switch {
	case m7:
		seventh = 7
	case M7:
		seventh = 11
	case d7 && quality == "m" && fifth == "b5" && !m7 && !M7:
		seventh = 9
	case d7:
		seventh = 6
	}

	// extensions (9, 11, 13)
	var alts []string
	ninth := false
	top := 0             // 9, 11, 13
	if has(2) && third { // 2 = 9 quand il y a une tierce
		ninth = true
		top = 9
	}
	if b9 {
		alts = append(alts, "b9")
		cost += 2
		if !third {
			cost += 5 // sus + b9 : c'est un cluster, pas un accord
		}
	}
	if s9 && M3 {
		alts = append(alts, "#9")
		cost += 2
	}
	if n11 && third {
		top = 11
		if seventh == 0 {
			top = -11 // add11
		}
		cost += 1
	}
	if s11 && (p5 || !b5) && third {
		alts = append(alts, "#11")
		cost += 2
	}
	if has(8) && p5 && (third || s11) { // b13 avec une quinte juste
		alts = append(alts, "b13")
		cost += 2
	}
	thirteen := false
	if has(9) && (seventh == 7 || seventh == 11) {
		thirteen = true
		top = 13
		cost += 1.2
	}
	if ninth && top < 13 {
		cost += 1
	}

	// assemblage
	var sb strings.Builder
	isDim := quality == "m" && fifth == "b5"
	isAug := quality == "" && fifth == "#5" && third
	switch {
	case isDim && seventh == 9:
		sb.WriteString("dim7")
		fifth = ""
		cost += 1
	case isDim && seventh == 7:
		sb.WriteString("m7b5")
		fifth = ""
		cost += 1
		if top >= 9 {
			sb.WriteString(map[int]string{9: "(9)", 11: "(11)", 13: "(13)"}[top])
			top = 0
		}
	case isDim && seventh == 0 && !ninth && top == 0:
		sb.WriteString("dim")
		fifth = ""
		cost += 0.5
	case isAug && seventh == 0 && top == 0:
		sb.WriteString("aug")
		fifth = ""
		cost += 0.5
	default:
		base := quality
		if quality == "sus4" || quality == "sus2" {
			base = quality
		}
		switch {
		case seventh == 6: // sixte
			cost += 1.5
			if !p5 {
				cost += 2 // sixte sans quinte : on préfère le renversement (C E A = Am/C)
			}
			if quality == "sus4" || quality == "sus2" {
				cost += 4
			}
			if ninth && top == 9 {
				sb.WriteString(base + "6/9")
				top = 0
			} else {
				sb.WriteString(base + "6")
			}
		case seventh == 7 || seventh == 11:
			cost++
			ext := 7
			if top == 9 || top == 11 || top == 13 {
				ext = top
			}
			mark := ""
			if seventh == 11 {
				mark = "maj"
			}
			if quality == "m" && seventh == 11 {
				sb.WriteString("m(maj" + itoa(ext) + ")")
			} else if quality == "m" || quality == "" {
				sb.WriteString(quality + mark + itoa(ext))
			} else {
				sb.WriteString(mark + itoa(ext) + quality)
			}
			if thirteen && ext == 13 {
				top = 0
			}
			top = 0
		default:
			sb.WriteString(base)
			switch {
			case top == 9:
				sb.WriteString("(add9)")
			case top == -11:
				sb.WriteString("(add11)")
			case top == 11:
				sb.WriteString("(add11)")
			}
			top = 0
		}
	}
	if fifth != "" {
		alts = append(alts, fifth)
		cost += 3.5
	}
	if len(alts) > 0 {
		sort.Strings(alts)
		sb.WriteString("(" + strings.Join(alts, ",") + ")")
	}
	_ = hasRoot
	return sb.String(), quality == "m", cost
}

func itoa(n int) string {
	if n < 10 {
		return string(rune('0' + n))
	}
	return string(rune('0'+n/10)) + string(rune('0'+n%10))
}

// Detect reconnaît l'accord formé par des notes MIDI tenues.
func (n ChordNamer) Detect(notes []int) ChordResult {
	if len(notes) == 0 {
		return ChordResult{}
	}
	ns := append([]int(nil), notes...)
	sort.Ints(ns)
	res := ChordResult{}
	seen := map[int]bool{}
	for _, v := range ns {
		pc := ((v % 12) + 12) % 12
		seen[pc] = true
	}
	spell := func(sharp bool) {
		res.Notes = res.Notes[:0]
		for _, v := range ns {
			res.Notes = append(res.Notes, n.nm(((v%12)+12)%12, sharp))
		}
	}
	spell(false)
	bass := ((ns[0] % 12) + 12) % 12
	if len(seen) == 1 {
		res.Name = n.Names[bass]
		return res
	}
	if len(seen) == 2 {
		var pcs []int
		for p := 0; p < 12; p++ {
			if seen[p] {
				pcs = append(pcs, p)
			}
		}
		a, b := pcs[0], pcs[1]
		root := -1
		if (b-a+12)%12 == 7 {
			root = a
		} else if (a-b+12)%12 == 7 {
			root = b
		}
		if root >= 0 && (bass == a || bass == b) {
			sh := sharpFamily(root, false)
			spell(sh)
			res.Name = n.nm(root, sh) + "5"
		}
		return res // deux notes quelconques : pas d'accord, on montre les notes
	}
	best := cand{cost: 1e9}
	bestRoot, bestMinor, bestSuf := -1, false, ""
	for root := 0; root < 12; root++ {
		if !seen[root] {
			continue
		}
		var mask uint16
		for p := range seen {
			mask |= 1 << uint((p-root+12)%12)
		}
		suf, minor, cost := nameFrom(mask, true)
		if root != bass {
			cost += 3
		}
		if cost < best.cost || (cost == best.cost && root == bass) {
			best = cand{"", cost}
			bestRoot, bestMinor, bestSuf = root, minor, suf
		}
	}
	if best.cost >= 6.5 { // amas de notes : aucun nom réaliste, on montre les notes
		return res
	}
	sh := sharpFamily(bestRoot, bestMinor)
	spell(sh)
	name := n.join(n.nm(bestRoot, sh), bestSuf)
	if bestRoot != bass {
		name += "/" + n.nm(bass, sh)
	}
	res.Name = name
	return res
}
