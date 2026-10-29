package main

// Reconnaissance automatique du type d'un son (lead, pad, basse, guitare, piano, orgue…) :
// d'après son nom, le nom du fichier, et pour les SoundFont le numéro de programme General MIDI.

import "strings"

var accentFold = strings.NewReplacer("é", "e", "è", "e", "ê", "e", "ë", "e", "à", "a", "â", "a", "ä", "a",
	"ô", "o", "ö", "o", "î", "i", "ï", "i", "ù", "u", "û", "u", "ü", "u", "ç", "c", "œ", "oe", "ñ", "n",
	"_", " ", "-", " ", ".", " ", "/", " ", "(", " ", ")", " ", "[", " ", "]", " ")

func foldName(s string) string {
	return " " + strings.Join(strings.Fields(accentFold.Replace(strings.ToLower(s))), " ") + " "
}

type kindRule struct {
	icon int
	keys []string // un mot-clé qui commence par un espace doit commencer un mot
}

// l'ordre compte : « bass drum » = batterie, « pad de cordes » = pad, « lead guitar » = lead…
var kindRules = []kindRule{
	{icDrum, []string{"drum", "batterie", " kit ", "percu", "perc ", "kick", "snare", "caisse claire", " tom ", "cymbal", "hihat", "hi hat", "conga", "bongo", "tambour", "timbal", "cajon"}},
	{icSquare, []string{"square lead", "sq lead", "square wave", "onde carree", "chiptune", " 8bit", "8 bit"}},
	{icWave, []string{" lead", "squash", "solo synth", "sawtooth", "saw wave", "sync ", "mono synth", "synth solo", "portamento"}},
	{icCloud, []string{" pad", " nappe", "atmos", "ambien", "ambiance", "halo", "sweep", "evolv", "drone", "soundscape", "texture", "celestial", "angelic", "ethereal", "worship"}},
	{icBass, []string{"bass", "basse", "bajo", " sub ", "contrebasse", "upright bass", "fretless", "slap"}},
	{icGuitar, []string{"guitar", "guitare", "guitarra", " gtr", "nylon", "strat", "telecaster", "ukulele", "banjo", "mandolin", "acoustic gt", "banjo", "sitar", "koto", "harpe", " harp"}},
	{icOrgan, []string{"organ", "orgue", "hammond", " b3", "pipe organ", "church org", "farfisa", "drawbar", "leslie", "accordion", "accordeon", "harmonium"}},
	{icVoice, []string{"choir", "choeur", "chorus", "voice", "voix", "vocal", "aah", "ooh", "gospel singer", "singer", "vox", "chant", "angel voice"}},
	{icStrings, []string{"string", "cordes", "violin", "violon", "viola", "cello", "violoncelle", "orchestra", "orchestre", "ensemble", "pizzicato", "tremolo str", "legato str", "cuerdas"}},
	{icBrass, []string{"brass", "cuivre", "trumpet", "trompette", " horn", "cor ", "trombone", "tuba", " sax", "saxophone", "bugle", "fanfare", "metaux"}},
	{icWind, []string{"flute", "clarinet", "clarinette", "oboe", "hautbois", "bassoon", "basson", "whistle", "sifflet", "recorder", "piccolo", "pan flute", "ocarina", "harmonica", "bagpipe", "cornemuse"}},
	{icBell, []string{"bell", "cloche", "glock", "vibraphone", " vibes", "marimba", "xylo", "celesta", "music box", "boite a musique", "chime", "carillon", "kalimba", "tubular", "steel drum", "crystal", "mallet"}},
	{icUpright, []string{"upright", "piano droit", "honky", "rhodes", "wurli", "e piano", " ep ", "epiano", "electric piano", "piano electrique", "piano elec", " dx ", "fm piano", "clav", "harpsichord", "clavecin", " tine"}},
	{icKeys, []string{"piano", "grand", "steinway", "bosendorfer", "yamaha c", " c7", " c3", "concert", "klavier"}},
	{icLayers, []string{" fx", "effect", "effet", "noise", "bruit", "riser", "impact", "sfx", "scratch", "sweeper", "foley"}},
}

// icône d'après un texte (nom du son, nom du fichier…) ; -1 si rien ne correspond
func iconFromName(name string) int {
	n := foldName(name)
	for _, r := range kindRules {
		for _, k := range r.keys {
			kk := k
			if kk == "" {
				continue
			}
			if strings.Contains(n, kk) {
				// « bass drum » : la grosse caisse, pas une basse
				if r.icon == icBass {
					if strings.Contains(n, "bass drum") || strings.Contains(n, "grosse caisse") {
						return icDrum
					}
					if strings.Contains(n, "bassoon") || strings.Contains(n, "basson") {
						return icWind
					}
				}
				return r.icon
			}
		}
	}
	return -1
}

// icône d'après le numéro de programme General MIDI (banque 0 seulement : ailleurs, les numéros n'ont pas de sens fixe)
func iconFromGM(prog int) int {
	switch {
	case prog < 0 || prog > 127:
		return -1
	case prog <= 2:
		return icKeys
	case prog <= 7:
		return icUpright
	case prog <= 15:
		return icBell
	case prog <= 23:
		return icOrgan
	case prog <= 31:
		return icGuitar
	case prog <= 39:
		return icBass
	case prog == 46: // harpe
		return icGuitar
	case prog == 47: // timbales
		return icDrum
	case prog <= 51:
		return icStrings
	case prog <= 54:
		return icVoice
	case prog == 55:
		return icStrings
	case prog <= 63:
		return icBrass
	case prog <= 67:
		return icBrass
	case prog <= 79:
		return icWind
	case prog == 80:
		return icSquare
	case prog <= 87:
		return icWave
	case prog <= 95:
		return icCloud
	case prog <= 103:
		return icLayers
	case prog <= 111:
		return icGuitar
	case prog <= 119:
		return icDrum
	}
	return icLayers
}

// soundIcon : choix final. fileName = nom du fichier (SoundFont ou audio), bank/prog = numéros du preset SoundFont.
func soundIcon(name, fileName string, isSF bool, bank, prog int) int {
	if isSF && bank == 128 { // banque des batteries
		return icDrum
	}
	if k := iconFromName(name); k >= 0 {
		return k
	}
	if isSF && bank == 0 {
		if k := iconFromGM(prog); k >= 0 && !genericName(name) {
			return k
		}
	}
	if k := iconFromName(fileName); k >= 0 {
		return k
	}
	if isSF && bank == 0 {
		if k := iconFromGM(prog); k >= 0 {
			return k
		}
	}
	if isSF {
		return icKeys
	}
	return icBars
}

// un nom comme « Preset 5 » ou « 012 » ne dit rien : le numéro de programme décide
func genericName(name string) bool {
	n := strings.TrimSpace(foldName(name))
	n = strings.TrimPrefix(n, "preset")
	n = strings.TrimPrefix(n, "program")
	n = strings.TrimSpace(n)
	if n == "" {
		return true
	}
	for _, r := range n {
		if r < '0' || r > '9' {
			return false
		}
	}
	return true
}
