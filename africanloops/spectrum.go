package africanloops

import (
	"math"
	"math/cmplx"
)

// Réglages de l'analyse en fréquences.
const (
	fftSize = 2048
	hopSize = 512 // 4 fenêtres se chevauchent : reconstruction exacte
)

// fft calcule la transformée de Fourier sur place (taille puissance de 2).
func fft(x []complex128, inverse bool) {
	n := len(x)
	for i, j := 1, 0; i < n; i++ {
		bit := n >> 1
		for ; j&bit != 0; bit >>= 1 {
			j ^= bit
		}
		j ^= bit
		if i < j {
			x[i], x[j] = x[j], x[i]
		}
	}
	sign := -1.0
	if inverse {
		sign = 1
	}
	for size := 2; size <= n; size <<= 1 {
		w := cmplx.Exp(complex(0, sign*2*math.Pi/float64(size)))
		for start := 0; start < n; start += size {
			wk := complex(1, 0)
			for k := 0; k < size/2; k++ {
				a, b := x[start+k], x[start+k+size/2]*wk
				x[start+k], x[start+k+size/2] = a+b, a-b
				wk *= w
			}
		}
	}
	if inverse {
		inv := complex(1/float64(n), 0)
		for i := range x {
			x[i] *= inv
		}
	}
}

// fenêtre racine de Hann : appliquée à l'analyse ET à la reconstruction,
// la somme des fenêtres qui se chevauchent vaut exactement 2.
var sqrtHann = func() []float64 {
	w := make([]float64, fftSize)
	for i := range w {
		w[i] = math.Sqrt(0.5 - 0.5*math.Cos(2*math.Pi*float64(i)/fftSize))
	}
	return w
}()

// La tranche t commence à l'échantillon t*hopSize + frameStart : les premières
// tranches commencent avant le début du son, pour que chaque échantillon soit
// couvert par 4 tranches (reconstruction exacte jusqu'aux bords).
const frameStart = -(fftSize - hopSize)

// frameCenter renvoie l'échantillon au centre de la tranche t.
func frameCenter(t int) int { return t*hopSize + frameStart + fftSize/2 }

// frameAt renvoie la tranche centrée le plus près d'un échantillon.
func frameAt(sample float64) int {
	return int(math.Round((sample - float64(frameStart+fftSize/2)) / hopSize))
}

// stft découpe le signal en tranches et renvoie le spectre de chacune.
func stft(x []float32) [][]complex128 {
	n := len(x)
	frames := (n-frameStart)/hopSize + 1
	out := make([][]complex128, frames)
	buf := make([]complex128, fftSize)
	for t := 0; t < frames; t++ {
		start := t*hopSize + frameStart
		for i := 0; i < fftSize; i++ {
			j := start + i
			v := 0.0
			if j >= 0 && j < n {
				v = float64(x[j])
			}
			buf[i] = complex(v*sqrtHann[i], 0)
		}
		fft(buf, false)
		spec := make([]complex128, fftSize/2+1)
		copy(spec, buf[:fftSize/2+1])
		out[t] = spec
	}
	return out
}

// istft reconstruit un signal de longueur n à partir des spectres.
func istft(spec [][]complex128, n int) []float32 {
	acc := make([]float64, n)
	buf := make([]complex128, fftSize)
	for t, s := range spec {
		copy(buf, s)
		for k := 1; k < fftSize/2; k++ {
			buf[fftSize-k] = cmplx.Conj(s[k])
		}
		fft(buf, true)
		start := t*hopSize + frameStart
		for i := 0; i < fftSize; i++ {
			j := start + i
			if j >= 0 && j < n {
				acc[j] += real(buf[i]) * sqrtHann[i]
			}
		}
	}
	out := make([]float32, n)
	for i := range acc {
		out[i] = float32(acc[i] / 2)
	}
	return out
}

// binHz renvoie la fréquence (Hz) d'une case du spectre.
func binHz(k, rate int) float64 { return float64(k) * float64(rate) / fftSize }

// Bandes utilisées pour reconnaître les percussions.
const (
	bandSub  = iota // < 120 Hz : grosse caisse, basse
	bandBody        // 120–500 Hz : corps des tambours (djembé, conga, caisse claire)
	bandMid         // 500–2500 Hz : claquements, slaps
	bandPres        // 2,5–7 kHz : timbre de la caisse claire, clap
	bandAir         // > 7 kHz : charleston, shaker
	numBands
)

var bandEdges = [numBands + 1]float64{0, 120, 500, 2500, 7000, 1e9}

func bandOf(hz float64) int {
	for b := 0; b < numBands; b++ {
		if hz < bandEdges[b+1] {
			return b
		}
	}
	return numBands - 1
}

// analysis regroupe tout ce qu'on calcule une seule fois par loop.
type analysis struct {
	rate   int
	n      int
	specL  [][]complex128
	specR  [][]complex128
	mag    [][]float64         // amplitude du mélange mono
	bands  [][numBands]float64 // énergie par bande et par tranche
	flux   []float64           // force des attaques (toutes bandes)
	bflux  [][numBands]float64 // force des attaques par bande
	onsets []int               // tranches où un coup commence
	starts []int               // échantillon exact du début de chaque coup
	mono   []float32
}

func analyze(a *Audio) *analysis {
	an := &analysis{rate: a.Rate, n: a.Frames()}
	an.specL = stft(a.L)
	an.specR = stft(a.R)
	T := len(an.specL)
	bins := fftSize/2 + 1
	bandOfBin := make([]int, bins)
	for k := range bandOfBin {
		bandOfBin[k] = bandOf(binHz(k, a.Rate))
	}
	an.mag = make([][]float64, T)
	an.bands = make([][numBands]float64, T)
	an.flux = make([]float64, T)
	an.bflux = make([][numBands]float64, T)
	prev := make([]float64, bins)
	for t := 0; t < T; t++ {
		m := make([]float64, bins)
		for k := 0; k < bins; k++ {
			m[k] = cmplx.Abs(an.specL[t][k]+an.specR[t][k]) * 0.5
			an.bands[t][bandOfBin[k]] += m[k] * m[k]
			// montée d'énergie en échelle logarithmique (comme l'oreille)
			d := math.Log1p(100*m[k]) - math.Log1p(100*prev[k])
			if d > 0 {
				an.flux[t] += d
				an.bflux[t][bandOfBin[k]] += d
			}
		}
		an.mag[t] = m
		prev = m
	}
	// chaque bande compte autant dans la courbe d'attaques : sinon les centaines
	// de cases des aigus (bruit, souffle) noient les notes graves des tambours
	var cnt [numBands]float64
	for k := 1; k < bins; k++ {
		cnt[bandOfBin[k]]++
	}
	for t := 0; t < T; t++ {
		an.flux[t] = 0
		for b := 0; b < numBands; b++ {
			if cnt[b] > 0 {
				an.flux[t] += an.bflux[t][b] / cnt[b]
			}
		}
	}
	an.onsets = pickOnsets(an.flux, a.Rate)
	an.mono = a.Mono()
	for _, t := range an.onsets {
		an.starts = append(an.starts, refineOnset(an.mono, frameCenter(t)))
	}
	return an
}

// refineOnset trouve l'échantillon exact où le coup démarre, autour du centre
// de la tranche : là où l'énergie monte le plus vite (avec 1 ms d'avance).
func refineOnset(x []float32, center int) int {
	const blk = 32
	lo, hi := center-hopSize, center+5*hopSize/2
	if lo < 0 {
		lo = 0
	}
	if hi > len(x)-blk {
		hi = len(x) - blk
	}
	energyAt := func(i int) float64 {
		s := 0.0
		for j := i; j < i+blk && j < len(x); j++ {
			if j >= 0 {
				s += float64(x[j]) * float64(x[j])
			}
		}
		return s
	}
	best, bestD := center, 0.0
	prev := energyAt(lo - blk)
	for i := lo; i <= hi; i += blk / 2 {
		e := energyAt(i)
		if d := math.Log(e+1e-7) - math.Log(prev+1e-7); d > bestD {
			best, bestD = i, d
		}
		prev = energyAt(i - blk/2)
	}
	best -= blk
	if best < 0 {
		best = 0
	}
	return best
}

// pickOnsets trouve les pics de la courbe d'attaques (seuil adaptatif).
func pickOnsets(flux []float64, rate int) []int {
	T := len(flux)
	if T == 0 {
		return nil
	}
	fr := float64(rate) / hopSize
	win := int(0.1 * fr) // ±100 ms autour de chaque tranche
	minGap := int(0.045 * fr)
	if minGap < 1 {
		minGap = 1
	}
	maxF := 0.0
	for _, v := range flux {
		maxF = math.Max(maxF, v)
	}
	if maxF <= 0 {
		return nil
	}
	var out []int
	last := -minGap - 1
	for t := 0; t < T; t++ {
		lo, hi := t-win, t+win
		if lo < 0 {
			lo = 0
		}
		if hi >= T {
			hi = T - 1
		}
		isMax := true
		sum := 0.0
		for j := lo; j <= hi; j++ {
			sum += flux[j]
			if j >= t-minGap && j <= t+minGap && (flux[j] > flux[t] || (flux[j] == flux[t] && j < t)) {
				isMax = false
			}
		}
		mean := sum / float64(hi-lo+1)
		if isMax && flux[t] > 1.4*mean+0.06*maxF && t-last >= minGap {
			out = append(out, t)
			last = t
		}
	}
	return out
}
