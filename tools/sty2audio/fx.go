package main

import "math"

// ---------------------------------------------------------------- réverbération (Freeverb)

type comb struct {
	buf   []float64
	i     int
	store float64
	fb    float64
	damp  float64
}

func (c *comb) process(x float64) float64 {
	y := c.buf[c.i]
	c.store = y*(1-c.damp) + c.store*c.damp
	c.buf[c.i] = x + c.store*c.fb
	c.i++
	if c.i == len(c.buf) {
		c.i = 0
	}
	return y
}

type allpass struct {
	buf []float64
	i   int
}

func (a *allpass) process(x float64) float64 {
	b := a.buf[a.i]
	y := -x + b
	a.buf[a.i] = x + b*0.5
	a.i++
	if a.i == len(a.buf) {
		a.i = 0
	}
	return y
}

type freeverb struct {
	cl, cr [8]comb
	al, ar [4]allpass
	wet1   float64
	wet2   float64
}

func newFreeverb(rate int) *freeverb {
	combT := []int{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}
	apT := []int{556, 441, 341, 225}
	k := float64(rate) / 44100
	f := &freeverb{}
	const room, damp, width = 0.82, 0.3, 1.0
	for i, t := range combT {
		f.cl[i] = comb{buf: make([]float64, int(float64(t)*k)), fb: room, damp: damp}
		f.cr[i] = comb{buf: make([]float64, int(float64(t+23)*k)), fb: room, damp: damp}
	}
	for i, t := range apT {
		f.al[i] = allpass{buf: make([]float64, int(float64(t)*k))}
		f.ar[i] = allpass{buf: make([]float64, int(float64(t+23)*k))}
	}
	wet := 3.0 // niveau de sortie (l'envoi est déjà dosé par CC91)
	f.wet1 = wet * (width/2 + 0.5)
	f.wet2 = wet * ((1 - width) / 2)
	return f
}

func (f *freeverb) process(inL, inR, outL, outR []float64, mul float64) {
	if mul <= 0 {
		return
	}
	for i := range inL {
		x := (inL[i] + inR[i]) * 0.015
		var l, r float64
		for k := range f.cl {
			l += f.cl[k].process(x)
			r += f.cr[k].process(x)
		}
		for k := range f.al {
			l = f.al[k].process(l)
			r = f.ar[k].process(r)
		}
		outL[i] += (l*f.wet1 + r*f.wet2) * mul
		outR[i] += (r*f.wet1 + l*f.wet2) * mul
	}
}

// ---------------------------------------------------------------- chorus

type chorus struct {
	bl, br []float64
	i      int
	ph     float64
	inc    float64
	base   float64
	depth  float64
}

func newChorus(rate int) *chorus {
	n := int(0.05 * float64(rate))
	return &chorus{
		bl: make([]float64, n), br: make([]float64, n),
		inc:  0.3 / float64(rate),
		base: 0.012 * float64(rate), depth: 0.004 * float64(rate),
	}
}

func (c *chorus) read(buf []float64, d float64) float64 {
	n := len(buf)
	p := float64(c.i) - d
	for p < 0 {
		p += float64(n)
	}
	i0 := int(p)
	f := p - float64(i0)
	return buf[i0%n]*(1-f) + buf[(i0+1)%n]*f
}

// process : entrée = envoi chorus ; sortie ajoutée au mélange et (un peu) à la réverbération.
func (c *chorus) process(inL, inR, outL, outR, revL, revR []float64) {
	for i := range inL {
		c.bl[c.i] = inL[i]
		c.br[c.i] = inR[i]
		s1 := math.Sin(2 * math.Pi * c.ph)
		s2 := math.Sin(2*math.Pi*c.ph + math.Pi/2)
		l := c.read(c.bl, c.base+c.depth*s1)
		r := c.read(c.br, c.base+c.depth*s2)
		outL[i] += l
		outR[i] += r
		revL[i] += l * 0.2
		revR[i] += r * 0.2
		c.ph += c.inc
		if c.ph >= 1 {
			c.ph -= 1
		}
		c.i++
		if c.i == len(c.bl) {
			c.i = 0
		}
	}
}
