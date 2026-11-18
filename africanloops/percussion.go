package africanloops

import (
	"math"
	"sort"
)

// Familles de percussions reconnues.
const (
	PercKick    = "KICK"       // grosse caisse (et basses très graves)
	PercSnare   = "SNARE/CLAP" // caisse claire, clap
	PercHat     = "HI-HAT/SHAKER"
	PercHand    = "DJEMBE/CONGA" // percussions à main (djembé, conga, bongo…)
	PercTalking = "TALKING DRUM" // tambour dont la note glisse
)

// Ordre d'affichage sur le mixer.
var PercOrder = []string{PercKick, PercSnare, PercHat, PercHand, PercTalking}

// Hit est un coup reconnu dans la loop.
type Hit struct {
	Time  float64  `json:"t"` // secondes
	Kinds []string `json:"k"`
	Force float64  `json:"f"`
}

// Percussion résume une famille trouvée dans la loop.
type Percussion struct {
	Kind  string  `json:"kind"`
	Hits  int     `json:"hits"`
	Share float64 `json:"share"` // part des coups de la loop (0..1)
}

type hitFeatures struct {
	rise     [numBands]float64 // énergie apportée par le coup, par bande
	flatPres float64           // 1 = bruit (caisse claire), 0 = note pure
	flatBody float64
	glide    float64 // glissement de la note (talking drum), en proportion
	force    float64
}

// bandRise : énergie ajoutée par le coup dans chaque bande (après − avant).
func (an *analysis) bandRise(t int) [numBands]float64 {
	T := len(an.bands)
	at := func(i, b int) float64 {
		if i < 0 || i >= T {
			return 0
		}
		return an.bands[i][b]
	}
	var r [numBands]float64
	for b := 0; b < numBands; b++ {
		before := 0.5 * (at(t-4, b) + at(t-3, b))
		after := math.Max(at(t+1, b), at(t+2, b))
		if d := after - before; d > 0 {
			r[b] = d
		}
	}
	return r
}

// features mesure le coup qui commence à l'échantillon start (tranche d'onset t).
func (an *analysis) features(t, start, stop int) hitFeatures {
	var h hitFeatures
	T := len(an.mag)
	ta := frameAt(float64(start + hopSize/2)) // tranche où le coup est bien présent
	if ta < 0 {
		ta = 0
	}
	h.rise = an.bandRise(ta)
	h.force = an.flux[t]
	fr := ta + 1
	if fr >= T {
		fr = T - 1
	}
	h.flatPres = an.flatness(fr, 2500, 9000)
	h.flatBody = an.flatness(fr, 120, 1200)
	h.glide = an.glide(ta, stop)
	return h
}

// flatness : 1 si le spectre est plat (bruit), proche de 0 s'il a des notes.
func (an *analysis) flatness(t int, lo, hi float64) float64 {
	m := an.mag[t]
	logSum, sum, n := 0.0, 0.0, 0.0
	for k := 1; k < len(m); k++ {
		hz := binHz(k, an.rate)
		if hz < lo || hz >= hi {
			continue
		}
		v := m[k] + 1e-9
		logSum += math.Log(v)
		sum += v
		n++
	}
	if n == 0 || sum <= 0 {
		return 0
	}
	return math.Exp(logSum/n) / (sum / n)
}

// glide mesure de combien la note principale (60–700 Hz) monte ou descend
// juste après le coup : c'est la signature du talking drum.
// La mesure s'arrête au coup suivant (tranche stop).
func (an *analysis) glide(t, stop int) float64 {
	T := len(an.mag)
	if stop > T {
		stop = T
	}
	fr := float64(an.rate) / hopSize
	span := int(0.25 * fr)
	var freqs []float64
	for dt := 1; dt <= span && t+dt < stop-1; dt++ {
		m := an.mag[t+dt]
		bestK, bestV, total := 0, 0.0, 0.0
		for k := 1; k < len(m); k++ {
			hz := binHz(k, an.rate)
			if hz < 60 || hz > 700 {
				continue
			}
			total += m[k]
			if m[k] > bestV {
				bestK, bestV = k, m[k]
			}
		}
		if bestK == 0 || bestV < 0.15*total {
			break // plus de note claire
		}
		// position fine du pic (interpolation parabolique)
		k := float64(bestK)
		if bestK > 1 && bestK+1 < len(m) {
			a, b, c := m[bestK-1], m[bestK], m[bestK+1]
			if d := a - 2*b + c; d != 0 {
				k += 0.5 * (a - c) / d
			}
		}
		freqs = append(freqs, k*float64(an.rate)/fftSize)
	}
	if len(freqs) < 4 {
		return 0
	}
	first := (freqs[0] + freqs[1]) / 2
	last := (freqs[len(freqs)-1] + freqs[len(freqs)-2]) / 2
	if first <= 0 {
		return 0
	}
	// la note doit glisser régulièrement dans un seul sens (pas sauter d'un son à l'autre)
	up, down := 0, 0
	for i := 1; i < len(freqs); i++ {
		r := freqs[i] / freqs[i-1]
		if r > 1.25 || r < 0.8 {
			return 0 // saut brutal : ce n'est pas un glissement
		}
		if r > 1.002 {
			up++
		} else if r < 0.998 {
			down++
		}
	}
	steps := float64(len(freqs) - 1)
	if float64(up) < 0.6*steps && float64(down) < 0.6*steps {
		return 0
	}
	return math.Abs(last-first) / first
}

// classify décide quelles percussions jouent sur un coup. ref[b] est la plus
// forte montée d'énergie de la bande b dans toute la loop, et top la plus forte
// toutes bandes confondues : chaque bande est jugée par rapport à elle-même.
func classify(h hitFeatures, ref [numBands]float64, top float64) []string {
	if top <= 0 {
		return nil
	}
	var on [numBands]bool
	var rel [numBands]float64
	any := false
	for b := 0; b < numBands; b++ {
		if ref[b] > 0 {
			rel[b] = h.rise[b] / ref[b]
		}
		on[b] = rel[b] >= 0.2 && h.rise[b] >= 1e-3*top
		any = any || on[b]
	}
	if !any {
		return nil
	}
	var kinds []string
	kick := on[bandSub] && h.rise[bandSub] >= h.rise[bandBody]
	snare := on[bandPres] && h.flatPres > 0.3 && (on[bandBody] || on[bandMid])
	hat := on[bandAir] && !snare && h.rise[bandAir] >= 0.5*h.rise[bandPres] &&
		h.rise[bandBody] < h.rise[bandAir]+h.rise[bandPres]
	hand := !snare && (on[bandBody] || on[bandMid]) && h.flatBody < 0.4 &&
		!(kick && h.rise[bandBody] < 0.7*h.rise[bandSub])
	talking := hand && h.glide > 0.12

	if kick {
		kinds = append(kinds, PercKick)
	}
	if snare {
		kinds = append(kinds, PercSnare)
	}
	if hat {
		kinds = append(kinds, PercHat)
	}
	if talking {
		kinds = append(kinds, PercTalking)
	} else if hand {
		kinds = append(kinds, PercHand)
	}
	return kinds
}

// detectHits reconnaît chaque coup de la loop.
func (an *analysis) detectHits() []Hit {
	feats := make([]hitFeatures, len(an.onsets))
	var ref [numBands]float64
	top := 0.0
	for i, t := range an.onsets {
		stop := len(an.mag)
		if i+1 < len(an.onsets) {
			stop = frameAt(float64(an.starts[i+1]))
		}
		feats[i] = an.features(t, an.starts[i], stop)
		for b := 0; b < numBands; b++ {
			ref[b] = math.Max(ref[b], feats[i].rise[b])
			top = math.Max(top, feats[i].rise[b])
		}
	}
	var hits []Hit
	for i := range an.onsets {
		kinds := classify(feats[i], ref, top)
		if len(kinds) == 0 {
			continue
		}
		hits = append(hits, Hit{Time: float64(an.starts[i]) / float64(an.rate), Kinds: kinds, Force: feats[i].force})
	}
	return hits
}

// summarize compte les coups par famille. Une famille doit apparaître au
// moins 2 fois (ou 5 % des coups) pour être retenue.
func summarize(hits []Hit) []Percussion {
	count := map[string]int{}
	for _, h := range hits {
		for _, k := range h.Kinds {
			count[k]++
		}
	}
	var out []Percussion
	for _, k := range PercOrder {
		c := count[k]
		if c >= 2 && (len(hits) == 0 || float64(c) >= 0.05*float64(len(hits))) {
			out = append(out, Percussion{Kind: k, Hits: c, Share: float64(c) / float64(len(hits))})
		}
	}
	return out
}

// Stem est une partie séparée de la loop.
type Stem struct {
	Kind  string
	Audio *Audio
}

// separate découpe la loop en une piste par famille de percussions trouvée.
// La somme de toutes les pistes redonne exactement la loop d'origine.
func (an *analysis) separate(percs []Percussion, hits []Hit) []Stem {
	present := map[string]bool{}
	for _, p := range percs {
		present[p.Kind] = true
	}
	var kinds []string
	for _, k := range PercOrder {
		if present[k] {
			kinds = append(kinds, k)
		}
	}
	if len(kinds) == 0 {
		return nil
	}
	T := len(an.specL)
	fr := float64(an.rate) / hopSize
	idx := map[string]int{}
	for i, k := range kinds {
		idx[k] = i
	}
	// activité de chaque famille au cours du temps (monte au coup, puis s'éteint)
	act := make([][]float64, len(kinds))
	for i := range act {
		act[i] = make([]float64, T)
	}
	decay := map[string]float64{PercKick: 0.25, PercSnare: 0.18, PercHat: 0.08, PercHand: 0.2, PercTalking: 0.35}
	for _, h := range hits {
		t0 := frameAt(h.Time * float64(an.rate))
		for _, k := range h.Kinds {
			i, ok := idx[k]
			if !ok {
				continue
			}
			tau := decay[k] * fr
			for t := t0 - 1; t < T && t < t0+int(6*tau); t++ {
				if t < 0 {
					continue
				}
				v := math.Exp(-math.Max(0, float64(t-t0)) / tau)
				if v > act[i][t] {
					act[i][t] = v
				}
			}
		}
	}
	// qui « possède » chaque zone de fréquences, quand personne ne joue
	fallback := func(band int) int {
		pref := map[int][]string{
			bandSub:  {PercKick, PercHand, PercTalking, PercSnare, PercHat},
			bandBody: {PercHand, PercTalking, PercSnare, PercKick, PercHat},
			bandMid:  {PercHand, PercSnare, PercTalking, PercHat, PercKick},
			bandPres: {PercSnare, PercHat, PercHand, PercTalking, PercKick},
			bandAir:  {PercHat, PercSnare, PercHand, PercTalking, PercKick},
		}[band]
		for _, k := range pref {
			if i, ok := idx[k]; ok {
				return i
			}
		}
		return 0
	}
	// importance de chaque famille dans chaque zone de fréquences
	affinity := map[string][numBands]float64{
		PercKick:    {1, 0.35, 0.05, 0.02, 0.01},
		PercSnare:   {0.05, 0.8, 0.8, 1, 0.5},
		PercHat:     {0.01, 0.02, 0.15, 0.5, 1},
		PercHand:    {0.3, 1, 1, 0.3, 0.1},
		PercTalking: {0.4, 1, 0.8, 0.2, 0.05},
	}
	bins := fftSize/2 + 1
	bandOfBin := make([]int, bins)
	for k := range bandOfBin {
		bandOfBin[k] = bandOf(binHz(k, an.rate))
	}
	outL := make([][][]complex128, len(kinds))
	outR := make([][][]complex128, len(kinds))
	for i := range kinds {
		outL[i] = make([][]complex128, T)
		outR[i] = make([][]complex128, T)
	}
	w := make([]float64, len(kinds))
	for t := 0; t < T; t++ {
		for i := range kinds {
			outL[i][t] = make([]complex128, bins)
			outR[i][t] = make([]complex128, bins)
		}
		var weights [numBands][]float64
		for b := 0; b < numBands; b++ {
			sum := 0.0
			for i, k := range kinds {
				w[i] = act[i][t] * affinity[k][b]
				sum += w[i]
			}
			ws := make([]float64, len(kinds))
			if sum < 0.05 {
				ws[fallback(b)] = 1
			} else {
				for i := range ws {
					ws[i] = w[i] / sum
				}
			}
			weights[b] = ws
		}
		for k := 0; k < bins; k++ {
			ws := weights[bandOfBin[k]]
			for i := range kinds {
				if ws[i] == 0 {
					continue
				}
				g := complex(ws[i], 0)
				outL[i][t][k] = an.specL[t][k] * g
				outR[i][t][k] = an.specR[t][k] * g
			}
		}
	}
	stems := make([]Stem, len(kinds))
	for i, k := range kinds {
		stems[i] = Stem{Kind: k, Audio: &Audio{Rate: an.rate, L: istft(outL[i], an.n), R: istft(outR[i], an.n)}}
	}
	return stems
}

// LoopInfo est le résultat complet de l'analyse d'une loop.
type LoopInfo struct {
	Tempo       Tempo        `json:"tempo"`
	Seconds     float64      `json:"seconds"`
	Rate        int          `json:"rate"`
	Percussions []Percussion `json:"percussions"`
	Hits        []Hit        `json:"hits,omitempty"`
	Slices      []float64    `json:"slices,omitempty"` // débuts des coups (secondes) : servent au changement de tempo
}

func analyzeLoop(a *Audio, filename string) (LoopInfo, *analysis, []Hit) {
	an := analyze(a)
	hits := an.detectHits()
	info := LoopInfo{
		Tempo:       DetectTempo(a, an, hits, filename),
		Seconds:     a.Seconds(),
		Rate:        a.Rate,
		Percussions: summarize(hits),
		Hits:        hits,
	}
	for _, st := range an.starts {
		info.Slices = append(info.Slices, float64(st)/float64(a.Rate))
	}
	return info, an, hits
}

// Analyze analyse une loop déjà chargée (tempo, percussions, coups).
func Analyze(a *Audio, filename string) LoopInfo {
	info, _, _ := analyzeLoop(a, filename)
	return info
}

// Separate analyse la loop et renvoie une piste par percussion trouvée.
// Si aucune percussion n'est reconnue, la loop entière est renvoyée seule.
func Separate(a *Audio, filename string) (LoopInfo, []Stem) {
	info, an, hits := analyzeLoop(a, filename)
	stems := an.separate(info.Percussions, hits)
	if len(stems) == 0 {
		stems = []Stem{{Kind: "LOOP", Audio: a}}
	}
	return info, stems
}

// sortedKinds renvoie les familles d'une liste, dans l'ordre du mixer.
func sortedKinds(p []Percussion) []string {
	out := make([]string, 0, len(p))
	for _, x := range p {
		out = append(out, x.Kind)
	}
	sort.SliceStable(out, func(i, j int) bool { return orderOf(out[i]) < orderOf(out[j]) })
	return out
}

func orderOf(k string) int {
	for i, o := range PercOrder {
		if o == k {
			return i
		}
	}
	return len(PercOrder)
}
