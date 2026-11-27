package main

import (
	"math"
	"testing"
)

// le Leslie doit vraiment « tourner » : mouvement net du pavillon (aigus) et du tambour (graves), stéréo large
func TestLeslieStrongEnough(t *testing.T) {
	for _, c := range []struct {
		f, minDB float64
	}{{2000, 8}, {200, 5}} {
		for _, fast := range []bool{false, true} {
			n := int(4*SR) - int(4*SR)%256
			l, r := fxSine(c.f, n)
			s := newLeslieState()
			for o := 0; o < n; o += 256 {
				s.process(l[o:o+256], r[o:o+256], fast)
			}
			db := 20 * math.Log10(lesDepth(lesEnvelope(l[n/2:])))
			if db < c.minDB {
				t.Errorf("%.0f Hz rapide=%v : mouvement %.1f dB (au moins %.0f dB attendu)", c.f, fast, db, c.minDB)
			}
			num, dl, dr := 0.0, 0.0, 0.0
			for i := n / 2; i < n; i++ {
				num += l[i] * r[i]
				dl += l[i] * l[i]
				dr += r[i] * r[i]
			}
			if corr := num / math.Sqrt(dl*dr); corr > 0.9 {
				t.Errorf("%.0f Hz rapide=%v : gauche et droite trop semblables (%.2f)", c.f, fast, corr)
			}
		}
	}
}
