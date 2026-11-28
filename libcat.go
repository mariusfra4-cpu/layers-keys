package main

// Familles de la bibliothèque de sons : une rangée par famille, chaque son à sa place.
// Le nom du son décide d'abord, puis le nom du fichier, puis les dossiers, puis le numéro General MIDI.

import "strings"

func init() {
	for k, v := range map[string][2]string{
		"PIANO ÉLEC.":           {"E. PIANO", "PIANO ELÉC."},
		"LEAD":                  {"LEAD", "LEAD"},
		"CUIVRES & VENTS":       {"BRASS & WINDS", "METALES Y VIENTOS"},
		"GUITARE":               {"GUITAR", "GUITARRA"},
		"CHŒURS":                {"CHOIR", "COROS"},
		"CLOCHES":               {"BELLS", "CAMPANAS"},
		"BATTERIE":              {"DRUMS", "BATERÍA"},
		"familles %d-%d sur %d": {"families %d-%d of %d", "familias %d-%d de %d"},
		"Clique sur un son pour le charger · molette = faire défiler une rangée · ◀ ▶ = autres familles · Échap pour fermer": {"Click a sound to load it · wheel = scroll a row · ◀ ▶ = other families · Esc to close", "Haz clic en un sonido para cargarlo · rueda = desplazar una fila · ◀ ▶ = otras familias · Esc para cerrar"},
	} {
		if _, ok := trTable[k]; !ok {
			trTable[k] = v
		}
	}
}

const (
	catPiano = iota
	catEPiano
	catOrgan
	catLead
	catSynth
	catPad
	catStrings
	catBrass
	catBass
	catGuitar
	catChoir
	catBells
	catDrums
	catOther
	nCat
)

var catNames = [nCat]string{"PIANO", "PIANO ÉLEC.", "ORGUE", "LEAD", "SYNTHÉ", "PAD", "STRINGS", "CUIVRES & VENTS",
	"BASSE", "GUITARE", "CHŒURS", "CLOCHES", "BATTERIE", "AUTRES"}

// icône de chaque famille (titres des rangées)
var catIcons = [nCat]int{icKeys, icUpright, icOrgan, icWave, icSquare, icCloud, icStrings, icBrass,
	icBass, icGuitar, icVoice, icBell, icDrum, icLayers}

type catRule struct {
	cat  int
	keys []string // un mot-clé qui commence par un espace doit commencer un mot
}

// l'ordre compte : « synth pad » = pad, « synth lead » = lead, « synth bass » = basse, « bass drum » = batterie,
// « electric piano » = piano électrique avant piano…
var catRules = []catRule{
	{catDrums, []string{"drum", "batterie", " kit ", "kick", "snare", "caisse claire", " tom ", "cymbal",
		"hihat", "hi hat", "conga", "bongo", "tambour", "timbal", "timpani", "cajon", "grosse caisse", "shaker", "tambourine", "clap"}},
	{catLead, []string{" lead", " ld ", " ld1", " ld2", " ld3", "square lead", "sq lead", "solo synth", "synth solo", "mono synth", "monosynth", "squash",
		"chiptune", " 8bit", "8 bit", "portamento", " glide", "whistle synth"}},
	{catPad, []string{" pad", " nappe", "atmos", "ambien", "ambiance", "halo", "sweep", "evolv", "drone", "soundscape",
		"texture", "celestial", "angelic", "ethereal", "worship", "warm str pad", "swell"}},
	{catBass, []string{"bass", "basse", "bajo", " sub ", "contrebasse", "fretless", "slap", "808"}},
	{catSynth, []string{"synth", "synthe", "saw", "supersaw", "pluck", " poly", "moog", "juno", "jupiter", "prophet",
		"oberheim", "trance", " arp", "arpeg", "wobble", "dubstep", " edm", "analog", " virus", "hoover", " stab",
		"sequence", " seq ", "pwm", "unison", "detune", "sine wave", "square wave", "onde carree", "brass synth",
		" jp8", "minimoog", "vintage synth"}},
	{catGuitar, []string{"guitar", "guitare", "guitarra", " gtr", "nylon", "strat", "telecaster", "ukulele", "banjo",
		"mandolin", "acoustic gt", "sitar", "koto", "dobro", "steel gt", "overdrive gt", "distortion gt"}},
	{catOrgan, []string{"organ", "orgue", "hammond", " b3", "pipe organ", "church org", "farfisa", "drawbar", "leslie",
		"accordion", "accordeon", "harmonium", "vox continental"}},
	{catChoir, []string{"choir", "choeur", "chorale", "voice", "voix", "vocal", " aah", " ooh", "gospel singer", "singer",
		" vox", "chant", "angel voice", "soprano", "alto voice", "tenor voice"}},
	{catStrings, []string{"string", "cordes", "violin", "violon", "viola", "cello", "violoncelle", "orchestra", "orchestre",
		"ensemble", "pizzicato", "tremolo str", "legato str", "cuerdas", "harpe", " harp", "contrabass str", "spiccato", "staccato str"}},
	{catBrass, []string{"brass", "cuivre", "trumpet", "trompette", " horn", "cor ", "trombone", "tuba", " sax", "saxophone",
		"bugle", "fanfare", "metaux", "flute", "flugel", "clarinet", "clarinette", "oboe", "hautbois", "bassoon", "basson",
		"whistle", "sifflet", "recorder", "piccolo", "pan flute", "ocarina", "harmonica", "bagpipe", "cornemuse", "woodwind", "bois"}},
	{catBells, []string{"bell", "cloche", "glock", "vibraphone", " vibes", "marimba", "xylo", "celesta", "music box",
		"boite a musique", "chime", "carillon", "kalimba", "tubular", "steel drum", "crystal", "mallet"}},
	{catEPiano, []string{"rhodes", "wurli", "e piano", " ep ", " ep1", " ep2", "epiano", "electric piano", "piano electrique",
		"piano elec", " dx ", "dx7", "fm piano", "fm ep", "clavinet", " clav ", " tine", "suitcase", "dyno", "cp80", "cp 80", "mark i", " mk1", " mk2"}},
	{catPiano, []string{"piano", "grand", "steinway", "bosendorfer", "yamaha", "kawai", "fazioli", "bechstein", "bluthner",
		"salamander", "concert", "klavier", "upright", "piano droit", "honky", " cfx", "harpsichord", "clavecin", " c7 ", " c3 "}},
	{catOther, []string{" fx", "effect", "effet", "noise", "bruit", "riser", "impact", "sfx", "scratch", "sweeper", "foley"}},
	{catDrums, []string{"percu", "perc "}}, // en dernier : « Percussive Organ » est un orgue, « Perc Kit » une batterie
}

// famille d'après un texte (nom du son, du fichier, d'un dossier) ; -1 si rien ne correspond
func catFromName(name string) int {
	n := foldName(name)
	for _, r := range catRules {
		for _, k := range r.keys {
			if strings.Contains(n, k) {
				switch {
				case r.cat == catDrums && strings.Contains(n, "steel drum"):
					return catBells
				case r.cat == catBass && (strings.Contains(n, "bass drum") || strings.Contains(n, "grosse caisse")):
					return catDrums
				case r.cat == catBass && (strings.Contains(n, "bassoon") || strings.Contains(n, "basson")):
					return catBrass
				}
				return r.cat
			}
		}
	}
	return -1
}

// famille d'après le numéro de programme General MIDI (banque 0)
func catFromGM(prog int) int {
	switch {
	case prog < 0 || prog > 127:
		return -1
	case prog <= 3:
		return catPiano
	case prog <= 7:
		return catEPiano
	case prog <= 15:
		return catBells
	case prog <= 23:
		return catOrgan
	case prog <= 31:
		return catGuitar
	case prog <= 39:
		return catBass
	case prog == 46:
		return catStrings // harpe
	case prog == 47:
		return catDrums // timbales
	case prog <= 51:
		return catStrings
	case prog <= 54:
		return catChoir
	case prog == 55:
		return catOther // coup d'orchestre
	case prog <= 79:
		return catBrass
	case prog <= 87:
		return catLead
	case prog <= 95:
		return catPad
	case prog <= 103:
		return catSynth
	case prog <= 111:
		return catGuitar
	case prog <= 119:
		return catDrums
	}
	return catOther
}

// famille d'un son.
// presets = nombre de sons dans le SoundFont : un SoundFont « General MIDI » en a beaucoup (le numéro de programme
// y a un sens), un SoundFont d'un seul son en a 1 et son numéro (souvent 0 = « piano ») ne veut rien dire.
//   - SoundFont d'un seul son (ou fichier audio) : nom du fichier, puis dossiers, puis nom du son ;
//   - grand SoundFont : nom du son, puis numéro General MIDI, puis nom du fichier et dossiers.
//
// Un son sans aucun indice va dans AUTRES (jamais rangé au hasard dans PIANO).
func libCat(name, file string, isSF bool, bank, prog, presets int, dirs []string) int {
	if isSF && bank == 128 {
		return catDrums
	}
	fromFile := func() int {
		if k := catFromName(baseNoExt(file)); k >= 0 {
			return k
		}
		for i := len(dirs) - 1; i >= 0; i-- { // du dossier le plus proche au plus lointain
			if k := catFromName(dirs[i]); k >= 0 {
				return k
			}
		}
		return -1
	}
	// le numéro de programme n'a un sens que dans une vraie banque General MIDI (beaucoup de sons, ou un nom qui le dit) :
	// dans un pack de leads, de pads… les numéros commencent à 0 (= « piano » en General MIDI) et ne veulent rien dire
	gmBank := isSF && bank == 0 && (presets >= 64 || isGMFile(file))
	if !isSF || presets < 8 {
		if k := fromFile(); k >= 0 {
			return k
		}
		if k := catFromName(name); k >= 0 {
			return k
		}
		return catOther
	}
	if k := catFromName(name); k >= 0 {
		return k
	}
	if k := fromFile(); k >= 0 { // « Leads Collection.sf2 », dossier « Pads »… : avant le numéro
		return k
	}
	if gmBank {
		if k := catFromGM(prog); k >= 0 {
			return k
		}
	}
	return catOther
}

// isGMFile : le nom du fichier dit que c'est une banque General MIDI
func isGMFile(file string) bool {
	n := foldName(baseNoExt(file))
	for _, k := range []string{" gm ", " gm", "gm ", "general midi", "generaluser", "general user", " gs ", " xg ", "fluid", "timgm", "arachno", "sgm ", "musescore general"} {
		if strings.Contains(n, k) {
			return true
		}
	}
	return false
}
