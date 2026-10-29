package main

// Réverbération (algorithme Freeverb, domaine public) : donne de l'espace aux sons.

type combF struct {
	buf          []float64
	idx          int
	store        float64
	damp1, damp2 float64
	fb           float64
}

func (c *combF) process(in float64) float64 {
	out := c.buf[c.idx]
	c.store = out*c.damp2 + c.store*c.damp1
	c.buf[c.idx] = in + c.store*c.fb
	c.idx++
	if c.idx >= len(c.buf) {
		c.idx = 0
	}
	return out
}

type allpassF struct {
	buf []float64
	idx int
}

func (a *allpassF) process(in float64) float64 {
	b := a.buf[a.idx]
	out := -in + b
	a.buf[a.idx] = in + b*0.5
	a.idx++
	if a.idx >= len(a.buf) {
		a.idx = 0
	}
	return out
}

type reverb struct {
	cl, cr [8]combF
	al, ar [4]allpassF
}

var combTune = [8]int{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}
var allpassTune = [4]int{556, 441, 341, 225}

const reverbSpread = 23

func newReverb() *reverb {
	r := &reverb{}
	sc := func(n int) int { return int(float64(n)*SR/44100 + 0.5) }
	room, damp := 0.80, 0.40
	for i := 0; i < 8; i++ {
		r.cl[i] = combF{buf: make([]float64, sc(combTune[i])), fb: room*0.28 + 0.7, damp1: damp * 0.4, damp2: 1 - damp*0.4}
		r.cr[i] = combF{buf: make([]float64, sc(combTune[i])+reverbSpread), fb: room*0.28 + 0.7, damp1: damp * 0.4, damp2: 1 - damp*0.4}
	}
	for i := 0; i < 4; i++ {
		r.al[i] = allpassF{buf: make([]float64, sc(allpassTune[i]))}
		r.ar[i] = allpassF{buf: make([]float64, sc(allpassTune[i])+reverbSpread)}
	}
	return r
}

// ajoute la réverbération à (l, r) ; amt = 0..1 ; send = signal envoyé en plus à la réverbération (peut être nil)
func (r *reverb) process(l, rr, send []float64, amt, sendAmt float64) {
	wet := amt * 2.2
	sw := sendAmt * 2.2
	for i := range l {
		in := (l[i] + rr[i]) * 0.015 * wet
		if send != nil {
			in += send[i] * 0.03 * sw
		}
		var ol, or float64
		for k := 0; k < 8; k++ {
			ol += r.cl[k].process(in)
			or += r.cr[k].process(in)
		}
		for k := 0; k < 4; k++ {
			ol = r.al[k].process(ol)
			or = r.ar[k].process(or)
		}
		l[i] += ol
		rr[i] += or
	}
}
