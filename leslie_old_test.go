package main

import "math"

// processOld : l'ancien Leslie (avant la refonte), gardé seulement pour l'écoute avant / après.
func (s *leslieState) processOld(l, r []float64, fast bool) {
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
