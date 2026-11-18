package africanloops

import (
	"math"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

// D'où vient le tempo trouvé.
const (
	TempoFromFile     = "fichier" // écrit dans le WAV par le fabricant de la loop
	TempoFromName     = "nom"     // écrit dans le nom du fichier (ex. « Groove_96bpm.wav »)
	TempoFromLength   = "durée"   // la loop tombe pile sur un nombre entier de temps
	TempoFromAnalysis = "analyse" // déduit des coups de batterie uniquement
	TempoUnknown      = "inconnu"
)

// Tempo décrit le BPM d'une loop.
type Tempo struct {
	BPM          float64   `json:"bpm"`
	Beats        int       `json:"beats"`      // nombre de temps dans la loop (0 = inconnu)
	Bars         int       `json:"bars"`       // nombre de mesures en 4/4 (0 = pas un nombre entier)
	Source       string    `json:"source"`     // fichier, nom, durée, analyse
	Confidence   float64   `json:"confidence"` // 0..1
	Alternatives []float64 `json:"alternatives,omitempty"`
}

const (
	minBPM = 60.0
	maxBPM = 200.0
)

var (
	reStrongBPM = regexp.MustCompile(`(?i)(?:^|[^0-9])(\d{2,3}(?:[.,]\d{1,2})?)\s*[-_ ]?\s*bpm`)
	reBPMFirst  = regexp.MustCompile(`(?i)bpm\s*[-_ ]?\s*(\d{2,3}(?:[.,]\d{1,2})?)(?:[^0-9]|$)`)
	reNumber    = regexp.MustCompile(`(?:^|[^0-9a-zA-Z])(\d{2,3})(?:[^0-9a-zA-Z]|$)`)
)

// TempoFromFilename cherche un BPM dans le nom du fichier.
// strong = le mot « bpm » est écrit à côté du nombre.
func TempoFromFilename(name string) (bpm float64, strong bool) {
	base := strings.TrimSuffix(filepath.Base(name), filepath.Ext(name))
	parse := func(s string) float64 {
		v, err := strconv.ParseFloat(strings.ReplaceAll(s, ",", "."), 64)
		if err != nil || v < 40 || v > 300 {
			return 0
		}
		return v
	}
	if m := reStrongBPM.FindStringSubmatch(base); m != nil {
		if v := parse(m[1]); v > 0 {
			return v, true
		}
	}
	if m := reBPMFirst.FindStringSubmatch(base); m != nil {
		if v := parse(m[1]); v > 0 {
			return v, true
		}
	}
	// un nombre seul, par ex. « Worship_96_Am.wav » : à confirmer par la durée
	for _, m := range reNumber.FindAllStringSubmatch(base, -1) {
		if v := parse(m[1]); v >= minBPM && v <= maxBPM {
			return v, false
		}
	}
	return 0, false
}

// tempoScorer mesure à quel point les coups se répètent à un tempo donné.
type tempoScorer struct {
	ac []float64 // autocorrélation circulaire normalisée de la courbe d'attaques
	fr float64   // tranches par seconde
}

func newTempoScorer(flux []float64, rate int) *tempoScorer {
	T := len(flux)
	ts := &tempoScorer{fr: float64(rate) / hopSize}
	if T < 8 {
		return ts
	}
	mean := 0.0
	for _, v := range flux {
		mean += v
	}
	mean /= float64(T)
	x := make([]float64, T)
	for i, v := range flux {
		x[i] = v - mean
	}
	maxLag := int(4*60*ts.fr/minBPM) + 2
	if maxLag > T-1 {
		maxLag = T - 1
	}
	ts.ac = make([]float64, maxLag+1)
	for l := 0; l <= maxLag; l++ {
		s := 0.0
		for i := 0; i < T; i++ {
			s += x[i] * x[(i+l)%T] // circulaire : une loop se répète
		}
		ts.ac[l] = s
	}
	if ts.ac[0] > 0 {
		inv := 1 / ts.ac[0]
		for l := range ts.ac {
			ts.ac[l] *= inv
		}
	}
	return ts
}

func (ts *tempoScorer) at(lag float64) float64 {
	if lag < 0 || int(lag)+1 >= len(ts.ac) {
		return 0
	}
	i := int(lag)
	f := lag - float64(i)
	return ts.ac[i]*(1-f) + ts.ac[i+1]*f
}

// score : les coups reviennent-ils tous les 1, 2, 3, 4 temps ?
func (ts *tempoScorer) score(bpm float64) float64 {
	if len(ts.ac) == 0 {
		return 0
	}
	p := 60 * ts.fr / bpm
	s := 0.0
	n := 0.0
	for k := 1; k <= 4; k++ {
		if lag := p * float64(k); int(lag)+1 < len(ts.ac) {
			s += ts.at(lag)
			n++
		}
		// le demi-temps (croches) renforce aussi le bon tempo
		if k == 1 {
			s += 0.5 * ts.at(p/2)
			n += 0.5
		}
	}
	if n == 0 {
		return 0
	}
	return s / n
}

// préférence douce pour les tempos courants (autour de 105 BPM)
func tempoPrior(bpm float64) float64 {
	d := math.Log2(bpm/105) / 0.8
	return math.Exp(-0.5 * d * d)
}

// DetectTempo trouve le BPM d'une loop.
func DetectTempo(a *Audio, an *analysis, hits []Hit, filename string) Tempo {
	dur := a.Seconds()
	finish := func(t Tempo) Tempo {
		if t.Source == TempoFromLength || t.Source == TempoFromAnalysis {
			if backbeatTooFast(t.BPM, hits) {
				t.BPM /= 2
				if t.Beats%2 == 0 {
					t.Beats /= 2
				} else {
					t.Beats = 0
				}
			}
		}
		if dur > 0 && t.BPM > 0 && t.Beats == 0 {
			b := dur * t.BPM / 60
			if rb := math.Round(b); rb >= 1 && math.Abs(b-rb) < 0.03*rb {
				t.Beats = int(rb)
			}
		}
		if t.Beats > 0 && t.Beats%4 == 0 {
			t.Bars = t.Beats / 4
		}
		for _, alt := range []float64{t.BPM / 2, t.BPM * 2} {
			if alt >= 45 && alt <= 230 {
				t.Alternatives = append(t.Alternatives, math.Round(alt*100)/100)
			}
		}
		return t
	}

	// 1. tempo écrit dans le fichier
	if a.MetaBPM > 0 {
		return finish(Tempo{BPM: a.MetaBPM, Beats: a.MetaBeats, Source: TempoFromFile, Confidence: 1})
	}
	// 2. tempo écrit dans le nom
	nameBPM, strong := TempoFromFilename(filename)
	if strong {
		return finish(Tempo{BPM: nameBPM, Source: TempoFromName, Confidence: 0.95})
	}

	ts := newTempoScorer(an.flux, a.Rate)
	// 3. meilleur tempo d'après les coups seuls
	best, bestS := 0.0, -1.0
	var all []float64
	for b := minBPM; b <= maxBPM+1e-9; b += 0.05 {
		s := ts.score(b) * tempoPrior(b)
		all = append(all, s)
		if s > bestS {
			best, bestS = b, s
		}
	}
	conf := 0.0
	if len(all) > 0 && bestS > 0 {
		sorted := append([]float64(nil), all...)
		sort.Float64s(sorted)
		med := sorted[len(sorted)/2]
		conf = clamp01((bestS - med) / bestS)
	}

	// 4. la durée de la loop : une loop bien coupée dure un nombre entier de temps
	if dur > 0.5 {
		candBPM, candS, candBeats := 0.0, -1.0, 0
		for _, beats := range []int{1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128} {
			b := 60 * float64(beats) / dur
			if b < minBPM || b > maxBPM {
				continue
			}
			s := ts.score(b) * tempoPrior(b)
			if s > candS {
				candBPM, candS, candBeats = b, s, beats
			}
		}
		// nombre seul dans le nom qui tombe pile sur la durée : on le prend
		if nameBPM > 0 {
			beats := dur * nameBPM / 60
			if rb := math.Round(beats); rb >= 1 && math.Abs(beats-rb) < 0.01*rb {
				return finish(Tempo{BPM: nameBPM, Beats: int(rb), Source: TempoFromName, Confidence: 0.9})
			}
		}
		if candBPM > 0 && (bestS <= 0 || candS >= 0.7*bestS) {
			bpm := candBPM
			if r := math.Round(bpm); math.Abs(bpm-r) < 0.02 {
				bpm = r
			} else {
				bpm = math.Round(bpm*100) / 100
			}
			c := 0.6 + 0.4*conf
			if bestS > 0 && math.Abs(candBPM-best)/best < 0.02 {
				c = math.Max(c, 0.9) // les deux méthodes sont d'accord
			}
			return finish(Tempo{BPM: bpm, Beats: candBeats, Source: TempoFromLength, Confidence: clamp01(c)})
		}
	}
	if bestS <= 0 {
		return Tempo{Source: TempoUnknown}
	}
	// 5. coups seuls (loop mal coupée)
	bpm := math.Round(best*10) / 10
	if r := math.Round(bpm); math.Abs(bpm-r) <= 0.3 {
		bpm = r
	}
	return finish(Tempo{BPM: bpm, Source: TempoFromAnalysis, Confidence: conf * 0.8})
}

// backbeatTooFast : dans presque tous les rythmes, la caisse claire (ou le clap)
// tombe sur les temps 2 et 4, donc tous les 2 temps. Si, au tempo trouvé, elle
// revient tous les 4 temps, c'est qu'on a compté deux fois trop vite.
func backbeatTooFast(bpm float64, hits []Hit) bool {
	if bpm/2 < 50 {
		return false
	}
	var times []float64
	for _, h := range hits {
		for _, k := range h.Kinds {
			if k == PercSnare {
				times = append(times, h.Time)
			}
		}
	}
	if len(times) < 3 {
		return false
	}
	var gaps []float64
	for i := 1; i < len(times); i++ {
		if g := times[i] - times[i-1]; g > 0.1 {
			gaps = append(gaps, g*bpm/60)
		}
	}
	if len(gaps) < 2 {
		return false
	}
	sort.Float64s(gaps)
	med := gaps[len(gaps)/2]
	return math.Abs(med-4) < 0.4
}

func clamp01(x float64) float64 {
	if x < 0 {
		return 0
	}
	if x > 1 {
		return 1
	}
	return x
}
