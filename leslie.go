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
	leslieXover     = 800.0 // Hz : séparation graves (tambour) / aigus (pavillon)
	leslieHornSlow  = 0.83  // tours par seconde
	leslieHornFast  = 6.7
	leslieDrumSlow  = 0.67
	leslieDrumFast  = 5.9
	leslieHornUp    = 0.55 // secondes (constante de temps) pour accélérer / ralentir
	leslieHornDown  = 0.9
	leslieDrumUp    = 2.6
	leslieDrumDown  = 3.6
	leslieDopplerMs = 0.45 // excursion du Doppler du pavillon (± ms)
)

type leslieState struct {
	buf      []float64 // aigus retardés (Doppler)
	idx      int
	lp1, lp2 float64 // filtre de séparation (deux passes)
	hornPh   float64 // phase du pavillon (tours)
	drumPh   float64
	hornSp   float64 // vitesse actuelle (tours/s)
	drumSp   float64
	started  bool
}

func newLeslieState() leslieState {
	return leslieState{buf: make([]float64, int(0.006*192000)+8)}
}

func (s *leslieState) clear() {
	for i := range s.buf {
		s.buf[i] = 0
	}
	s.lp1, s.lp2 = 0, 0
}

// process applique l'effet sur l, r (le son de la piste) en place.
func (s *leslieState) process(l, r []float64, fast bool) {
	if len(s.buf) == 0 {
		return
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
	a := 1 - math.Exp(-2*math.Pi*leslieXover/SR)
	base := (leslieDopplerMs*1.2 + 0.3) * 0.001 * SR // retard moyen (assez pour que le Doppler reste positif)
	dep := leslieDopplerMs * 0.001 * SR
	if base+dep+2 >= float64(n) {
		base = float64(n)/2 - 2
		dep = base * 0.8
	}
	const (
		hornAM = 0.38 // variation de volume du pavillon
		drumAM = 0.22
		gHorn  = 0.95
		gDrum  = 1.05
	)
	for i := range l {
		m := (l[i] + r[i]) * 0.5 // une cabine Leslie n'a qu'une entrée
		s.lp1 += (m - s.lp1) * a
		s.lp2 += (s.lp1 - s.lp2) * a
		lo := s.lp2
		hi := m - lo

		s.buf[s.idx] = hi

		s.hornSp += (hT - s.hornSp) * kH
		s.drumSp += (dT - s.drumSp) * kD
		hp := 2 * math.Pi * s.hornPh
		dp := 2 * math.Pi * s.drumPh

		// deux micros (gauche / droite) placés à 90° l'un de l'autre autour de la cabine
		var out [2]float64
		for ch := 0; ch < 2; ch++ {
			off := float64(ch) * math.Pi / 2
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
			// le pavillon est plus fort quand il est tourné vers le micro
			hAmp := 1 - hornAM*0.5*(1-math.Cos(hp+off))
			dAmp := 1 - drumAM*0.5*(1-math.Cos(dp+off+0.7))
			out[ch] = h*hAmp*gHorn + lo*dAmp*gDrum
		}
		l[i], r[i] = out[0], out[1]

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
