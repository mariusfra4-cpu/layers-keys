package main

// Effet Leslie (cabine à haut-parleurs tournants, le son classique de l'orgue Hammond).
//
// Le son est séparé en deux bandes à 800 Hz, comme dans une vraie cabine :
//   - les aigus passent par le « pavillon » (horn) qui tourne vite : effet Doppler (le son monte et descend
//     légèrement) + variation de volume, différente à gauche et à droite (deux micros autour de la cabine) ;
//   - les graves passent par le « tambour » (drum), plus lourd et plus lent : variation de volume seulement.
// Deux vitesses : LENTE (chorale) et RAPIDE (trémolo). Le passage de l'une à l'autre n'est pas instantané :
// le pavillon accélère en moins d'une seconde, le tambour, plus lourd, met plusieurs secondes (c'est ce qui donne
// la « montée » caractéristique quand l'organiste passe en rapide).
// Aucune allocation pendant le jeu : le tampon du Doppler est préparé avec la chaîne d'effets.

import (
	"math"
	"sync/atomic"
)

// LeslieCfg : effet Leslie d'une piste (enregistré dans les presets et projets).
// La vitesse (lente / rapide) est commune à toutes les pistes, comme le seul inverseur d'un orgue :
// voir Engine.SetLeslieFast.
type LeslieCfg struct {
	On bool
}

// SetLeslieFast choisit la vitesse du Leslie de toutes les pistes (vrai = rapide). Sans risque depuis n'importe quel fil.
func (e *Engine) SetLeslieFast(fast bool) {
	v := int32(0)
	if fast {
		v = 1
	}
	atomic.StoreInt32(&e.leslieFast, v)
}

// LeslieFast : vrai si le Leslie est en vitesse rapide.
func (e *Engine) LeslieFast() bool { return atomic.LoadInt32(&e.leslieFast) != 0 }

const (
	leslieXover     = 800.0 // Hz : séparation graves (tambour) / aigus (pavillon), comme le filtre d'une Leslie 122
	leslieHornSlow  = 0.83  // tours par seconde
	leslieHornFast  = 6.7
	leslieDrumSlow  = 0.67
	leslieDrumFast  = 5.9
	leslieHornUp    = 0.55 // secondes (constante de temps) pour accélérer / ralentir
	leslieHornDown  = 0.9
	leslieDrumUp    = 2.6
	leslieDrumDown  = 3.6
	leslieDopplerMs = 0.62 // excursion du Doppler du pavillon (± ms) : rayon du pavillon / vitesse du son
	leslieMicAngle  = 2.6  // angle entre les deux micros (radians, ~150°) : large, mais pas « ping-pong »
)

// filtre du second ordre (Butterworth) pour la séparation graves / aigus et la couleur de la cabine
type lesBQ struct{ b0, b1, b2, a1, a2, z1, z2 float64 }

func (f *lesBQ) set(hp bool, fc, q float64) {
	w := 2 * math.Pi * fc / SR
	cw, al := math.Cos(w), math.Sin(w)/(2*q)
	a0 := 1 + al
	if hp {
		f.b0, f.b1, f.b2 = (1+cw)/2/a0, -(1+cw)/a0, (1+cw)/2/a0
	} else {
		f.b0, f.b1, f.b2 = (1-cw)/2/a0, (1-cw)/a0, (1-cw)/2/a0
	}
	f.a1, f.a2 = -2*cw/a0, (1-al)/a0
}

func (f *lesBQ) run(x float64) float64 {
	y := f.b0*x + f.z1
	f.z1 = f.b1*x - f.a1*y + f.z2
	f.z2 = f.b2*x - f.a2*y
	return y
}

type leslieState struct {
	buf      []float64 // aigus retardés (Doppler)
	idx      int
	lp1, lp2 float64 // (ancien filtre, gardé pour l'écoute avant / après)
	hornPh   float64 // phase du pavillon (tours)
	drumPh   float64
	hornSp   float64 // vitesse actuelle (tours/s)
	drumSp   float64
	started  bool
	ready    bool
	lo1, lo2 lesBQ      // Linkwitz-Riley 4 : deux passe-bas en série = tambour
	hi1, hi2 lesBQ      // deux passe-haut en série = pavillon (somme des deux = son d'origine, sans « bavure »)
	dir      [2]float64 // filtre de directivité du pavillon (un par micro)
	cabL     [2]lesBQ   // haut-parleur et coffre en bois : graves sous 50 Hz et extrême aigu coupés
	cabR     [2]lesBQ
}

func newLeslieState() leslieState {
	return leslieState{buf: make([]float64, int(0.006*192000)+8)}
}

func (s *leslieState) clear() {
	for i := range s.buf {
		s.buf[i] = 0
	}
	s.lp1, s.lp2 = 0, 0
	s.ready = false
}

func (s *leslieState) setup() {
	s.lo1, s.lo2, s.hi1, s.hi2 = lesBQ{}, lesBQ{}, lesBQ{}, lesBQ{}
	s.lo1.set(false, leslieXover, math.Sqrt2/2)
	s.lo2.set(false, leslieXover, math.Sqrt2/2)
	s.hi1.set(true, leslieXover, math.Sqrt2/2)
	s.hi2.set(true, leslieXover, math.Sqrt2/2)
	for c := 0; c < 2; c++ {
		s.cabL[c], s.cabR[c] = lesBQ{}, lesBQ{}
	}
	s.cabL[0].set(true, 45, 0.7)
	s.cabR[0].set(true, 45, 0.7)
	s.cabL[1].set(false, 9000, 0.6)
	s.cabR[1].set(false, 9000, 0.6)
	s.dir = [2]float64{}
	s.ready = true
}

// process applique l'effet sur l, r (le son de la piste) en place.
func (s *leslieState) process(l, r []float64, fast bool) {
	if len(s.buf) == 0 {
		return
	}
	if !s.ready {
		s.setup()
	}
	n := len(s.buf)
	if !s.started { // premier passage : on démarre directement à la bonne vitesse
		s.started = true
		s.hornSp, s.drumSp = leslieHornSlow, leslieDrumSlow
		if fast {
			s.hornSp, s.drumSp = leslieHornFast, leslieDrumFast
		}
	}
	hT, dT := leslieHornSlow, leslieDrumSlow
	hTau, dTau := leslieHornDown, leslieDrumDown
	if fast {
		hT, dT = leslieHornFast, leslieDrumFast
		hTau, dTau = leslieHornUp, leslieDrumUp
	}
	kH := 1 - math.Exp(-1/(hTau*SR))
	kD := 1 - math.Exp(-1/(dTau*SR))
	base := (leslieDopplerMs*1.2 + 0.3) * 0.001 * SR // retard moyen (assez pour que le Doppler reste positif)
	dep := leslieDopplerMs * 0.001 * SR
	if base+dep+2 >= float64(n) {
		base = float64(n)/2 - 2
		dep = base * 0.8
	}
	// directivité du pavillon : tourné vers le micro, le son est brillant (12 kHz) ; tourné à l'opposé, sourd (2,2 kHz)
	cFace := 1 - math.Exp(-2*math.Pi*12000/SR)
	cAway := 1 - math.Exp(-2*math.Pi*2200/SR)
	const (
		hornAM = 0.55 // variation de volume du pavillon (le « tourbillon »)
		drumAM = 0.55 // le tambour (rotor des graves) : la « pulsation » qu'on sent dans le corps
		gHorn  = 1.08
		gDrum  = 1.12
		drive  = 1.35 // préampli à lampes de la cabine : légère chaleur, sans distorsion audible à volume normal
	)
	for i := range l {
		m := (l[i] + r[i]) * 0.5 // une cabine Leslie n'a qu'une entrée
		m = math.Tanh(m*drive) / drive
		lo := s.lo2.run(s.lo1.run(m))
		hi := -s.hi2.run(s.hi1.run(m)) // Linkwitz-Riley : le passe-haut est inversé pour s'additionner proprement

		s.buf[s.idx] = hi

		s.hornSp += (hT - s.hornSp) * kH
		s.drumSp += (dT - s.drumSp) * kD
		hp := 2 * math.Pi * s.hornPh
		dp := 2 * math.Pi * s.drumPh

		// deux micros autour de la cabine (gauche / droite)
		var out [2]float64
		for ch := 0; ch < 2; ch++ {
			off := float64(ch) * leslieMicAngle
			sh := math.Sin(hp + off)
			d := base + dep*sh
			p := float64(s.idx) - d
			for p < 0 {
				p += float64(n)
			}
			i0 := int(p)
			fr := p - float64(i0)
			if i0 >= n {
				i0 -= n
			}
			i1 := i0 + 1
			if i1 >= n {
				i1 -= n
			}
			h := s.buf[i0]*(1-fr) + s.buf[i1]*fr
			face := 0.5 * (1 + math.Cos(hp+off)) // 1 = pavillon tourné vers ce micro
			s.dir[ch] += (h - s.dir[ch]) * (cAway + (cFace-cAway)*face)
			h = s.dir[ch]
			hAmp := 1 - hornAM*(1-face)
			dAmp := 1 - drumAM*0.5*(1-math.Cos(dp+float64(ch)*math.Pi*0.85+0.7)) // le rotor passe d'un micro à l'autre
			out[ch] = h*hAmp*gHorn + lo*dAmp*gDrum
		}
		l[i] = s.cabL[1].run(s.cabL[0].run(out[0]))
		r[i] = s.cabR[1].run(s.cabR[0].run(out[1]))

		s.idx++
		if s.idx >= n {
			s.idx = 0
		}
		s.hornPh += s.hornSp / SR
		if s.hornPh >= 1 {
			s.hornPh -= 1
		}
		s.drumPh += s.drumSp / SR
		if s.drumPh >= 1 {
			s.drumPh -= 1
		}
	}
}
