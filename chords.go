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
	Root  int      // fondamentale (0 = do … 11 = si) ; -1 s'il n'y a pas d'accord (sert aux pads continus)
	Minor bool     // accord mineur
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
	if suf != "" && (n.Spaced || strings.HasPrefix(suf, "Maj")) {
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

// describe donne le nom d'un accord pour une fondamentale choisie ; mask = intervalles présents (bit 0 = fondamentale).
// Règle de vérité : CHAQUE note jouée doit être expliquée par le nom (tierce, quinte, septième, extension ou altération).
// Si une note reste sans explication, ok = false : cette lecture est refusée, on ne cache jamais une note.
func describe(mask uint16) (suffix string, minor bool, cost float64, ok bool) {
	has := func(i int) bool { return mask&(1<<uint(i)) != 0 }
	used := uint16(1)
	use := func(i int) { used |= 1 << uint(i) }
	var alts []string

	// 1. tierce ou suspension
	quality := ""
	third := false
	switch {
	case has(4):
		third = true
		use(4)
		if has(3) { // tierce mineure en plus de la majeure : c'est une #9
			use(3)
			alts = append(alts, "#9")
			cost += 2.5
		}
	case has(3):
		quality, third = "m", true
		use(3)
	case has(5):
		quality = "sus4"
		use(5)
		cost += 1
	case has(2):
		quality = "sus2"
		use(2)
		cost += 1
	default:
		if has(7) && mask&^(1|1<<7) == 0 {
			return "5", false, 0.5, true
		}
		return "", false, 0, false
	}

	// 2. quinte
	five := ""
	switch {
	case has(7):
		use(7)
	case has(6):
		five = "b5"
		use(6)
	case has(8):
		five = "#5"
		use(8)
	}
	noFifth := five == "" && !has(7)

	// 3. septième ou sixte
	seventh := ""
	sixth := false
	switch {
	case has(10) && !has(11):
		seventh = "7"
		use(10)
	case has(11) && !has(10):
		seventh = "maj7"
		use(11)
	case has(9) && quality == "m" && five == "b5":
		seventh = "dim7"
		use(9)
	case has(9):
		sixth = true
		use(9)
	}
	hasSev := seventh == "7" || seventh == "maj7"

	// 4. extensions et altérations
	top := 0 // 9, 11 ou 13 : extension la plus haute
	add := ""
	if has(2) && quality != "sus2" {
		use(2)
		top = 9
		if !third { // sus4 + 9
			cost += 1
		}
	}
	if has(5) && quality != "sus4" && third {
		use(5)
		top = 11
		cost += 1.5
	}
	if has(9) && hasSev {
		use(9)
		top = 13
		cost += 1.2
	}
	if has(1) { // b9
		use(1)
		alts = append(alts, "b9")
		cost += 2
		if !third {
			cost += 5
		}
	}
	if has(6) && five != "b5" { // #11 (avec quinte juste)
		use(6)
		alts = append(alts, "#11")
		cost += 2
	}
	if has(8) && five != "#5" { // b13
		use(8)
		alts = append(alts, "b13")
		cost += 4
	}
	if has(3) && !third { // m3 sans lecture : refusé
		return "", false, 0, false
	}
	if used != mask {
		return "", false, 0, false // une note n'est pas expliquée
	}

	// 5. assemblage du nom : tout ce qui a été « consommé » plus haut doit apparaître dans le nom
	if has(1) || (has(3) && has(4)) {
		if seventh != "7" { // b9 ou #9 sans septième de dominante : lecture peu naturelle
			cost += 3
		}
	}
	if top == 13 && !has(2) {
		cost += 1.5 // 13 sans 9
	}
	if top == 11 && !has(2) && hasSev {
		cost += 2 // 11 sans 9
	}
	var sb strings.Builder
	isDim := quality == "m" && five == "b5"
	isAug := quality == "" && third && five == "#5"
	suffixExt := func() { // extension qui n'a pas de place dans le nom court : on l'écrit entre parenthèses
		if top > 0 {
			sb.WriteString("(" + itoa(top) + ")")
			cost += 4.5
			top = 0
		}
	}
	switch {
	case isDim && seventh == "dim7":
		sb.WriteString("dim7")
		five = ""
		cost += 1
		suffixExt()
	case isDim && seventh == "7":
		sb.WriteString("m7b5")
		five = ""
		cost += 1
		suffixExt()
	case isDim && seventh == "" && !sixth:
		sb.WriteString("dim")
		five = ""
		cost += 0.5
		suffixExt()
	case isAug && seventh == "" && !sixth:
		sb.WriteString("aug")
		five = ""
		cost += 0.5
		suffixExt()
	case hasSev:
		cost++
		ext := 7
		if top > 0 {
			ext = top
		}
		mark := ""
		if seventh == "maj7" {
			mark = "Maj"
		}
		switch {
		case quality == "m" && seventh == "maj7":
			sb.WriteString("m(Maj" + itoa(ext) + ")")
			if ext > 7 {
				cost += 2
			}
		case quality == "m" || quality == "":
			sb.WriteString(quality + mark + itoa(ext))
		default:
			sb.WriteString(mark + itoa(ext) + quality)
		}
		top = 0
	case sixth:
		cost += 1.5
		if noFifth {
			cost += 2 // sixte sans quinte : on préfère le renversement (C E A = Am/C)
		}
		if quality == "sus4" || quality == "sus2" {
			cost += 4
		}
		if top == 9 {
			sb.WriteString(quality + "6/9")
			top = 0
		} else {
			sb.WriteString(quality + "6")
			if top == 11 {
				sb.WriteString("(add11)")
				cost += 2
				top = 0
			}
		}
	default:
		sb.WriteString(quality)
		switch top {
		case 9:
			add = "add9"
			cost += 1.2
		case 11:
			add = "add11"
			cost += 1
		}
		top = 0
	}
	if top != 0 {
		return "", false, 0, false // sécurité : une extension serait cachée
	}
	if add != "" {
		sb.WriteString("(" + add + ")")
	}
	if five != "" {
		alts = append(alts, five)
		cost += 3.5
		if quality == "sus4" || quality == "sus2" {
			cost += 6
		} else if !hasSev {
			cost += 3
		}
	}
	if len(alts) > 0 {
		sort.Strings(alts)
		sb.WriteString("(" + strings.Join(alts, ",") + ")")
	}
	if noFifth && !hasSev && !sixth && seventh == "" {
		cost += 0.8 // triade sans quinte
	} else if noFifth {
		cost += 0.4
	}
	return sb.String(), quality == "m", cost, true
}

func itoa(n int) string {
	if n < 10 {
		return string(rune('0' + n))
	}
	return string(rune('0'+n/10)) + string(rune('0'+n%10))
}

// Detect reconnaît l'accord formé par des notes MIDI tenues.
// Il choisit la lecture la plus naturelle : fondamentale à la basse si possible, sinon accord sur basse (renversement, « /E »)
// avec la vraie fondamentale. Il n'affiche jamais un nom qui cache une note jouée.
func (n ChordNamer) Detect(notes []int) ChordResult {
	if len(notes) == 0 {
		return ChordResult{Root: -1}
	}
	ns := append([]int(nil), notes...)
	sort.Ints(ns)
	res := ChordResult{Root: -1}
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
		res.Root = bass
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
			res.Name = n.join(n.nm(root, sh), "5")
			res.Root = root
		}
		return res // deux notes quelconques : pas d'accord, on montre les notes
	}
	bestCost := 1e9
	bestRoot, bestMinor, bestSuf := -1, false, ""
	for root := 0; root < 12; root++ {
		if !seen[root] {
			continue
		}
		var mask uint16
		for p := range seen {
			mask |= 1 << uint((p-root+12)%12)
		}
		suf, minor, cost, ok := describe(mask)
		if !ok {
			continue
		}
		if root != bass {
			// accord sur basse : petit coût si la basse est une note de l'accord (tierce, quinte, septième), plus grand sinon
			switch (bass - root + 12) % 12 {
			case 3, 4, 6, 7, 8, 10, 11:
				cost += 1.5
			default:
				cost += 3.5
			}
		}
		if cost < bestCost-1e-9 || (cost < bestCost+1e-9 && root == bass) {
			bestCost = cost
			bestRoot, bestMinor, bestSuf = root, minor, suf
		}
	}
	if bestRoot < 0 || bestCost >= 8 { // amas de notes : aucun nom réaliste, on montre les notes
		return res
	}
	sh := sharpFamily(bestRoot, bestMinor)
	spell(sh)
	name := n.join(n.nm(bestRoot, sh), bestSuf)
	if bestRoot != bass {
		name += "/" + n.nm(bass, sh)
	}
	res.Name = name
	res.Root, res.Minor = bestRoot, bestMinor
	return res
}
