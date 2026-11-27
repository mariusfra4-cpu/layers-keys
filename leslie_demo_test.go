package main

import (
	"encoding/binary"
	"math"
	"os"
	"path/filepath"
	"testing"
)

// écoute avant / après du Leslie (LESLIEDEMO=dossier) : accord d'orgue, lent → rapide → lent
func TestLeslieDemo(t *testing.T) {
	dir := os.Getenv("LESLIEDEMO")
	if dir == "" {
		t.Skip()
	}
	secs := 11.0
	n := int(secs * SR)
	n -= n % 256
	notes := []float64{48, 60, 64, 67, 72} // Do grave, accord de Do majeur
	// orgue à tirettes : 16', 8', 5 1/3', 4', 2 2/3', 2'
	harm := []struct{ r, a float64 }{{0.5, 0.8}, {1, 1}, {1.5, 0.7}, {2, 0.6}, {3, 0.35}, {4, 0.3}}
	dry := make([]float64, n)
	for i := range dry {
		tt := float64(i) / SR
		v := 0.0
		for _, nt := range notes {
			f := 440 * math.Pow(2, (nt-69)/12)
			for _, h := range harm {
				v += h.a * math.Sin(2*math.Pi*f*h.r*tt)
			}
		}
		env := math.Min(1, tt/0.01) * math.Min(1, (secs-tt)/0.3)
		dry[i] = v * env
	}
	fastAt := func(i int) bool { tt := float64(i) / SR; return tt >= 3.5 && tt < 7.5 }
	render := func(old bool) ([]float64, []float64) {
		s := newLeslieState()
		l, r := make([]float64, n), make([]float64, n)
		copy(l, dry)
		copy(r, dry)
		for o := 0; o < n; o += 256 {
			if old {
				s.processOld(l[o:o+256], r[o:o+256], fastAt(o))
			} else {
				s.process(l[o:o+256], r[o:o+256], fastAt(o))
			}
		}
		return l, r
	}
	for _, v := range []struct {
		name string
		old  bool
	}{{"leslie_AVANT.wav", true}, {"leslie_APRES.wav", false}} {
		l, r := render(v.old)
		writeTestWav(t, filepath.Join(dir, v.name), l, r)
		for _, seg := range []struct {
			lab  string
			a, b float64
		}{{"lent", 1, 3.4}, {"rapide", 4.6, 7.4}} {
			s0, e0 := int(seg.a*SR), int(seg.b*SR)
			w := int(0.004 * SR)
			lo, hi := math.Inf(1), 0.0
			for o := s0; o+w <= e0; o += w {
				g := fxRMS(l[o:o+w]) / math.Max(fxRMS(dry[o:o+w]), 1e-9)
				lo, hi = math.Min(lo, g), math.Max(hi, g)
			}
			num, dl, dr := 0.0, 0.0, 0.0
			for i := s0; i < e0; i++ {
				num += l[i] * r[i]
				dl += l[i] * l[i]
				dr += r[i] * r[i]
			}
			t.Logf("%-17s %-6s mouvement %.1f dB · ressemblance gauche/droite %+.2f", v.name, seg.lab, 20*math.Log10(hi/lo), num/math.Sqrt(dl*dr))
		}
	}
}

func writeTestWav(t *testing.T, path string, l, r []float64) {
	peak := 1e-9
	for i := range l {
		peak = math.Max(peak, math.Max(math.Abs(l[i]), math.Abs(r[i])))
	}
	g := 0.7 / peak
	n := len(l)
	b := make([]byte, 44+n*4)
	copy(b[0:], "RIFF")
	binary.LittleEndian.PutUint32(b[4:], uint32(36+n*4))
	copy(b[8:], "WAVEfmt ")
	binary.LittleEndian.PutUint32(b[16:], 16)
	binary.LittleEndian.PutUint16(b[20:], 1)
	binary.LittleEndian.PutUint16(b[22:], 2)
	binary.LittleEndian.PutUint32(b[24:], uint32(SR))
	binary.LittleEndian.PutUint32(b[28:], uint32(SR)*4)
	binary.LittleEndian.PutUint16(b[32:], 4)
	binary.LittleEndian.PutUint16(b[34:], 16)
	copy(b[36:], "data")
	binary.LittleEndian.PutUint32(b[40:], uint32(n*4))
	for i := 0; i < n; i++ {
		binary.LittleEndian.PutUint16(b[44+i*4:], uint16(int16(math.Round(l[i]*g*32767))))
		binary.LittleEndian.PutUint16(b[46+i*4:], uint16(int16(math.Round(r[i]*g*32767))))
	}
	if err := os.WriteFile(path, b, 0o644); err != nil {
		t.Fatal(err)
	}
}
