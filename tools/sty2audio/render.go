package main

// Lecture des sections du style par le synthétiseur.

import (
	"math"
	"sort"
	"strings"
)

type Options struct {
	Rate        int
	Repeats     int     // répétitions des sections Main
	FillRepeats int     // répétitions des Fill In / Break
	BPM         float64 // 0 = tempo d'origine
	Transpose   int     // demi-tons ; ou tonalité cible si KeySet
	KeySet      bool
	Key         int // tonalité cible 0..11
	Reverb      float64
	TailMax     float64 // secondes de queue maximum
}

type Audio struct {
	L, R []float32
}

func (a *Audio) Len() int { return len(a.L) }

// initEnd : fin de la zone d'initialisation (SFF1/SInt).
func (st *Style) initEnd() int64 {
	e := int64(math.MaxInt64)
	for _, s := range st.Sections {
		if s.Start < e {
			e = s.Start
		}
	}
	if e == math.MaxInt64 {
		e = 0
	}
	return e
}

type player struct {
	st     *Style
	syn    *Synth
	opt    Options
	out    Audio
	uspqn  float64
	tempoK float64
	frac   float64 // reste de trame
	lb, rb []float64
}

func newPlayer(st *Style, sf *SoundFont, opt Options) *player {
	p := &player{st: st, syn: NewSynth(sf, opt.Rate), opt: opt, uspqn: st.Tempos[0].USPQN, tempoK: 1}
	p.syn.revMul = opt.Reverb
	if opt.BPM > 0 {
		p.tempoK = (60e6 / p.uspqn) / opt.BPM
	}
	p.lb, p.rb = make([]float64, block), make([]float64, block)
	return p
}

// advance : produit la durée correspondant à dTicks.
func (p *player) advanceTicks(d int64) {
	if d <= 0 {
		return
	}
	sec := float64(d) * p.uspqn * p.tempoK / 1e6 / float64(p.st.PPQN)
	p.advanceSec(sec)
}

func (p *player) advanceSec(sec float64) {
	f := sec*float64(p.opt.Rate) + p.frac
	n := int(f)
	p.frac = f - float64(n)
	p.renderFrames(n)
}

func (p *player) renderFrames(n int) {
	for n > 0 {
		k := n
		if k > block {
			k = block
		}
		l, r := p.lb[:k], p.rb[:k]
		for i := range l {
			l[i], r[i] = 0, 0
		}
		p.syn.Render(l, r)
		for i := 0; i < k; i++ {
			p.out.L = append(p.out.L, float32(l[i]))
			p.out.R = append(p.out.R, float32(r[i]))
		}
		n -= k
	}
}

// apply : envoie un événement au synthétiseur. notes=false : état seulement.
func (p *player) apply(e *Event, ch int, notes bool, xpose int, lim *CtabInfo, sysex bool) {
	s := p.syn
	switch {
	case e.Status == 0xFF:
		if e.Meta == 0x51 && len(e.Bytes) >= 3 {
			us := float64(int(e.Bytes[0])<<16 | int(e.Bytes[1])<<8 | int(e.Bytes[2]))
			if us > 0 {
				p.uspqn = us
			}
		}
	case e.Status == 0xF0 || e.Status == 0xF7:
		if sysex {
			s.SysEx(e.Bytes)
		}
	default:
		switch e.Kind() {
		case 0x90, 0x80:
			if !notes {
				return
			}
			key := int(e.Data1)
			if !s.ch[ch].drum {
				key += xpose
				if lim != nil && xpose != 0 {
					for key > lim.HiLim && key-12 >= 0 {
						key -= 12
					}
					for key < lim.LoLim && key+12 <= 127 {
						key += 12
					}
				}
			}
			if key < 0 || key > 127 {
				return
			}
			if e.Kind() == 0x90 && e.Data2 > 0 {
				s.NoteOn(ch, key, int(e.Data2))
			} else {
				s.NoteOff(ch, key)
			}
		case 0xB0:
			s.Control(ch, int(e.Data1), int(e.Data2))
		case 0xC0:
			s.Program(ch, int(e.Data1))
		case 0xE0:
			s.Bend(ch, int(e.Data2)<<7|int(e.Data1))
		case 0xD0:
			s.Pressure(ch, int(e.Data1))
		}
	}
}

// setup : état initial (SFF1 + SInt).
func (p *player) setup(first bool) {
	end := p.st.initEnd()
	for i := range p.st.Events {
		e := &p.st.Events[i]
		if e.Tick >= end {
			break
		}
		if !first && e.Status == 0xFF {
			continue
		}
		p.apply(e, e.Channel(), false, 0, nil, first)
	}
}

// cutNotes : fin de boucle, les notes tenues sont coupées comme sur le clavier.
func (p *player) cutNotes() {
	for _, v := range p.syn.voices {
		if v.on && !v.released {
			v.held = false
			p.syn.releaseVoice(v)
		}
	}
	for i := range p.syn.ch {
		p.syn.ch[i].sustain = false
	}
}

func (p *player) playSection(sec Section, repeats int) {
	route := p.st.Routing(sec.Name)
	// tempo en vigueur au début de la section
	for _, t := range p.st.Tempos {
		if t.Tick <= sec.Start {
			p.uspqn = t.USPQN
		}
	}
	var evs []*Event
	for i := range p.st.Events {
		e := &p.st.Events[i]
		if e.Tick >= sec.Start && e.Tick < sec.End {
			evs = append(evs, e)
		}
	}
	if repeats < 1 {
		repeats = 1
	}
	for r := 0; r < repeats; r++ {
		pos := sec.Start
		for _, e := range evs {
			p.advanceTicks(e.Tick - pos)
			pos = e.Tick
			ch := e.Channel()
			xpose := 0
			var lim *CtabInfo
			if e.Status < 0xF0 {
				if t, ok := route[ch]; ok {
					ch = t.Dest
					lim = &t
					if t.NTT != 0 && !t.Drum {
						xpose = p.transposeFor(t.RootNote)
					}
				} else if len(route) == 0 && ch != 8 && ch != 9 {
					xpose = p.transposeFor(0)
				}
			}
			p.apply(e, ch, true, xpose, lim, true)
		}
		p.advanceTicks(sec.End - pos)
		p.cutNotes()
	}
}

func (p *player) transposeFor(root int) int {
	if p.opt.KeySet {
		d := ((p.opt.Key-root)%12 + 12) % 12
		if d > 6 {
			d -= 12 // on reste au plus près (-5..+6 demi-tons)
		}
		return d
	}
	return p.opt.Transpose
}

// tail : laisse s'éteindre les notes et la réverbération (au plus TailMax secondes).
func (p *player) tail() {
	p.syn.ReleaseAll()
	maxN := int(p.opt.TailMax * float64(p.opt.Rate))
	start := p.out.Len()
	quiet := 0
	for p.out.Len()-start < maxN {
		n0 := p.out.Len()
		p.renderFrames(block * 8)
		pk := 0.0
		for i := n0; i < p.out.Len(); i++ {
			pk = math.Max(pk, math.Max(math.Abs(float64(p.out.L[i])), math.Abs(float64(p.out.R[i]))))
		}
		if !p.syn.Busy() && pk < 3e-5 {
			if quiet++; quiet > 4 {
				break
			}
		} else {
			quiet = 0
		}
	}
	cut := p.out.Len()-start >= maxN
	// coupe le silence final (sous -70 dB de la crête du fichier)
	if pk := p.out.Peak(); pk > 0 {
		th := float32(pk * math.Pow(10, -70.0/20))
		last := start
		for i := p.out.Len() - 1; i >= start; i-- {
			if abs32(p.out.L[i]) > th || abs32(p.out.R[i]) > th {
				last = i
				break
			}
		}
		if keep := last + p.opt.Rate/20; keep < p.out.Len() {
			p.out.L, p.out.R = p.out.L[:keep], p.out.R[:keep]
			cut = false
		}
	}
	// fondu de sortie : 1,5 s si le son a été coupé, sinon 10 ms
	n := p.out.Len()
	f := p.opt.Rate / 100
	if cut {
		f = p.opt.Rate * 3 / 2
	}
	if f > n-start {
		f = n - start
	}
	for i := 0; i < f; i++ {
		g := float32(0.5 - 0.5*math.Cos(math.Pi*float64(i)/float64(f)))
		p.out.L[n-1-i] *= g
		p.out.R[n-1-i] *= g
	}
}

// RenderChain : joue une suite de sections avec un seul synthétiseur.
func RenderChain(st *Style, sf *SoundFont, secs []Section, opt Options) (*Audio, map[int]bool) {
	p := newPlayer(st, sf, opt)
	p.setup(true)
	for i, s := range secs {
		if i > 0 {
			p.setup(false)
		}
		p.playSection(s, repeatsFor(s.Name, opt))
	}
	p.tail()
	return &p.out, p.syn.missing
}

func repeatsFor(name string, opt Options) int {
	u := strings.ToUpper(name)
	switch {
	case strings.HasPrefix(u, "MAIN"):
		return opt.Repeats
	case strings.HasPrefix(u, "FILL"), strings.HasPrefix(u, "BREAK"):
		return opt.FillRepeats
	}
	return 1
}

// DemoChain : Intro → Main A → Fill → Main B … → Ending.
func (st *Style) DemoChain() []Section {
	by := map[string]Section{}
	for _, s := range st.Sections {
		by[strings.ToUpper(s.Name)] = s
	}
	var chain []Section
	pick := func(names ...string) bool {
		for _, n := range names {
			if s, ok := by[strings.ToUpper(n)]; ok {
				chain = append(chain, s)
				return true
			}
		}
		return false
	}
	pick("Intro A", "Intro B", "Intro C", "Intro D")
	prev := ""
	for _, x := range []string{"A", "B", "C", "D"} {
		if _, ok := by["MAIN "+x]; !ok {
			continue
		}
		if prev != "" {
			pick("Fill In "+prev+x, "Fill In "+x+x, "Fill In "+prev+prev)
		}
		pick("Main " + x)
		prev = x
	}
	pick("Ending A", "Ending B", "Ending C", "Ending D")
	if len(chain) < 2 {
		chain = append([]Section(nil), st.Sections...)
		sort.SliceStable(chain, func(i, j int) bool { return chain[i].Start < chain[j].Start })
	}
	return chain
}

func abs32(x float32) float32 {
	if x < 0 {
		return -x
	}
	return x
}
