package main

// Synthétiseur SoundFont multitimbral (16 canaux MIDI) : interpolation
// cubique, enveloppes volume/modulation SF2, LFO, filtre passe-bas résonant,
// modulateurs par défaut SF2 (vélocité, volume, expression, panoramique,
// molette, pitch bend), réverbération et chorus.

import (
	"math"
)

const block = 64

type chanState struct {
	bankMSB, bankLSB int
	bankSet          bool
	prog             int
	drum             bool
	drumForced       int // -1 auto, 0 mélodique, 1 batterie (SysEx)
	preset           *Preset
	cc               [128]int
	bend             int // 0..16383
	pressure         int
	bendRange        float64 // demi-tons
	fineTune         float64 // cents
	coarseTune       float64 // demi-tons
	rpnMSB, rpnLSB   int
	nrpn             bool
	sustain          bool
	transpose        int
	lo, hi           int // limites de note (CASM)
}

func (c *chanState) reset(ch int) {
	*c = chanState{drumForced: -1, bend: 8192, bendRange: 2, rpnMSB: 127, rpnLSB: 127, hi: 127}
	c.cc[7] = 100
	c.cc[10] = 64
	c.cc[11] = 127
	c.cc[91] = 40
	c.cc[71], c.cc[74] = 64, 64
	c.drum = ch == 9
}

type env struct {
	stage      int // 0 délai, 1 attaque, 2 maintien, 3 déclin, 4 soutien, 5 relâche, 6 fini
	t          float64
	delay, att float64
	hold, dec  float64
	sus        float64 // vol : atténuation en cB ; mod : niveau 0..1
	rel        float64
	level      float64 // vol : atténuation cB (0 = plein) ; mod : 0..1
	attLin     float64 // vol : amplitude linéaire pendant l'attaque
	isVol      bool
	relFrom    float64
}

func tc2s(tc float64) float64 {
	if tc <= -12000 {
		return 0
	}
	if tc > 8000 {
		tc = 8000
	}
	return math.Pow(2, tc/1200)
}

// advance l'enveloppe de dt secondes.
func (e *env) advance(dt float64) {
	for e.stage < 6 { // les étapes de durée nulle sont franchies même si dt = 0
		switch e.stage {
		case 0:
			if e.t+dt < e.delay {
				e.t += dt
				e.setStart()
				return
			}
			dt -= e.delay - e.t
			e.stage, e.t = 1, 0
		case 1:
			if e.att <= 0 || e.t+dt >= e.att {
				if e.att > 0 {
					dt -= e.att - e.t
				}
				e.stage, e.t = 2, 0
				e.attLin = 1
				e.level = e.peak()
				continue
			}
			e.t += dt
			x := e.t / e.att
			if e.isVol {
				e.attLin = x
				e.level = 0
			} else {
				e.level = x
			}
			return
		case 2:
			if e.t+dt < e.hold {
				e.t += dt
				return
			}
			dt -= e.hold - e.t
			e.stage, e.t = 3, 0
		case 3:
			if e.isVol {
				// 1000 cB parcourus en « dec » secondes
				rate := 1000.0
				if e.dec > 0 {
					rate = 1000 / e.dec
				} else {
					e.level = e.sus
					e.stage = 4
					continue
				}
				e.level += rate * dt
				if e.level >= e.sus {
					e.level = e.sus
					e.stage = 4
				}
			} else {
				if e.dec <= 0 {
					e.level = e.sus
					e.stage = 4
					continue
				}
				e.level -= dt / e.dec
				if e.level <= e.sus {
					e.level = e.sus
					e.stage = 4
				}
			}
			return
		case 4:
			return
		case 5:
			if e.isVol {
				rate := 1000 / math.Max(e.rel, 0.004)
				e.level += rate * dt
				if e.level >= 960 {
					e.level = 960
					e.stage = 6
				}
			} else {
				r := math.Max(e.rel, 0.001)
				e.level -= dt / r
				if e.level <= 0 {
					e.level = 0
					e.stage = 6
				}
			}
			return
		}
	}
}

func (e *env) peak() float64 {
	if e.isVol {
		return 0
	}
	return 1
}

func (e *env) setStart() {
	if e.isVol {
		e.attLin = 0
		e.level = 0
	} else {
		e.level = 0
	}
}

func (e *env) release() {
	if e.stage >= 5 {
		return
	}
	if e.isVol && e.stage <= 1 {
		// relâche pendant l'attaque : on convertit l'amplitude en cB
		a := e.attLin
		if e.stage == 0 || a < 1e-5 {
			e.level = 960
		} else {
			e.level = -200 * math.Log10(a)
		}
	}
	e.stage = 5
	e.t = 0
}

// gain linéaire de l'enveloppe de volume
func (e *env) gain() float64 {
	switch e.stage {
	case 0:
		return 0
	case 1:
		return e.attLin
	case 6:
		return 0
	}
	if e.level >= 960 {
		return 0
	}
	return math.Pow(10, -e.level/200)
}

type lfo struct {
	delay, freq float64
	t           float64
}

func (l *lfo) value() float64 {
	if l.t < l.delay || l.freq <= 0 {
		return 0
	}
	ph := math.Mod((l.t-l.delay)*l.freq, 1)
	// triangle partant de 0 vers +1
	switch {
	case ph < 0.25:
		return 4 * ph
	case ph < 0.75:
		return 2 - 4*ph
	default:
		return 4*ph - 4
	}
}

type voice struct {
	on       bool
	ch       int
	key      int
	vel      int
	noteID   int64
	released bool // note relâchée (touche)
	held     bool // retenue par la pédale
	data     []int16
	pos      float64
	start    int
	end      int
	ls, le   int
	mode     int
	pitchKey float64 // cents de base (sans pitch bend)
	rateMul  float64
	g        [numGens]int32
	mods     []Mod
	ms       [numGens]float64 // somme des modulateurs par générateur
	ccFilter bool             // CC71/74 gérés par la SoundFont
	volEnv   env
	modEnv   env
	vib, mod lfo
	// filtre
	z1L, z2L           float64
	fz                 [2]float64
	b0, b1, b2, a1, a2 float64
	prevGain           float64
	excl               int
	age                int64
	killFast           bool
}

type Synth struct {
	sf      *SoundFont
	rate    float64
	ch      [16]chanState
	voices  []*voice
	maxV    int
	age     int64
	rev     *freeverb
	cho     *chorus
	revMul  float64
	bufL    []float64
	bufR    []float64
	revL    []float64
	revR    []float64
	choL    []float64
	choR    []float64
	tmp     []float64
	missing map[int]bool
}

func NewSynth(sf *SoundFont, rate int) *Synth {
	s := &Synth{sf: sf, rate: float64(rate), maxV: 256, revMul: 1, missing: map[int]bool{}}
	for i := range s.ch {
		s.ch[i].reset(i)
	}
	s.rev = newFreeverb(rate)
	s.cho = newChorus(rate)
	s.bufL, s.bufR = make([]float64, block), make([]float64, block)
	s.revL, s.revR = make([]float64, block), make([]float64, block)
	s.choL, s.choR = make([]float64, block), make([]float64, block)
	s.tmp = make([]float64, block)
	return s
}

func (s *Synth) presetFor(c *chanState) *Preset {
	if c.preset == nil {
		bank := 0
		if !c.drum && c.bankMSB > 0 && c.bankMSB < 64 && c.bankMSB != 8 {
			// variations GS ; les banques Yamaha (Mega, etc.) n'existent pas en GM
			bank = c.bankMSB
		}
		prog := c.prog
		if c.drum && c.bankMSB == 126 {
			prog = 56 // kit SFX
		}
		c.preset = s.sf.FindPreset(bank, prog, c.drum)
	}
	return c.preset
}

func (s *Synth) updateDrum(i int) {
	c := &s.ch[i]
	d := i == 9
	if c.bankSet {
		d = c.bankMSB == 127 || c.bankMSB == 126 || c.bankMSB == 120
	}
	if c.drumForced >= 0 {
		d = c.drumForced == 1
	}
	if d != c.drum {
		c.drum = d
	}
	c.preset = nil
}

// ResetAll : réinitialisation GM/XG/GS.
func (s *Synth) ResetAll() {
	for i := range s.ch {
		t := s.ch[i].transpose
		lo, hi := s.ch[i].lo, s.ch[i].hi
		s.ch[i].reset(i)
		s.ch[i].transpose, s.ch[i].lo, s.ch[i].hi = t, lo, hi
	}
}

func (s *Synth) SysEx(b []byte) {
	if len(b) > 0 && b[0] == 0xF0 {
		b = b[1:]
	}
	if len(b) >= 4 && b[0] == 0x7E && b[2] == 0x09 && (b[3] == 0x01 || b[3] == 0x03) {
		s.ResetAll() // GM on
		return
	}
	if len(b) >= 7 && b[0] == 0x43 && b[1]&0xF0 == 0x10 && b[2] == 0x4C {
		ah, am, al, v := b[3], b[4], b[5], int(b[6])
		if ah == 0 && am == 0 && al == 0x7E {
			s.ResetAll() // XG system on
			return
		}
		if ah == 0x08 && int(am) < 16 {
			ch := int(am)
			c := &s.ch[ch]
			switch al {
			case 0x01:
				c.bankMSB, c.bankSet = v, true
				s.updateDrum(ch)
			case 0x02:
				c.bankLSB = v
			case 0x03:
				c.prog = v
				c.preset = nil
			case 0x07:
				if v == 0 {
					c.drumForced = 0
				} else {
					c.drumForced = 1
				}
				s.updateDrum(ch)
			case 0x0B:
				c.cc[7] = v
			case 0x0E:
				if v == 0 {
					v = 64
				}
				c.cc[10] = v
			case 0x12:
				c.cc[93] = v
			case 0x13:
				c.cc[91] = v
			}
		}
		return
	}
	if len(b) >= 8 && b[0] == 0x41 && b[2] == 0x42 && b[3] == 0x12 {
		if b[4] == 0x40 && b[5] == 0x00 && b[6] == 0x7F {
			s.ResetAll() // GS reset
			return
		}
		if b[4] == 0x40 && b[5]&0xF0 == 0x10 && b[6] == 0x15 {
			part := int(b[5] & 0x0F)
			ch := part - 1
			if part == 0 {
				ch = 9
			} else if part <= 9 {
				ch = part - 1
			} else {
				ch = part
			}
			if ch >= 0 && ch < 16 {
				if b[7] == 0 {
					s.ch[ch].drumForced = 0
				} else {
					s.ch[ch].drumForced = 1
				}
				s.updateDrum(ch)
			}
		}
	}
}

func (s *Synth) Control(ch, cc, v int) {
	c := &s.ch[ch]
	c.cc[cc] = v
	switch cc {
	case 0:
		c.bankMSB, c.bankSet = v, true
		s.updateDrum(ch)
	case 32:
		c.bankLSB = v
	case 6, 38:
		if !c.nrpn {
			s.dataEntry(c)
		}
	case 64:
		c.sustain = v >= 64
		if !c.sustain {
			for _, vo := range s.voices {
				if vo.on && vo.ch == ch && vo.held {
					vo.held = false
					s.releaseVoice(vo)
				}
			}
		}
	case 98, 99:
		c.nrpn = true
	case 100:
		c.rpnLSB, c.nrpn = v, false
	case 101:
		c.rpnMSB, c.nrpn = v, false
	case 120:
		for _, vo := range s.voices {
			if vo.ch == ch {
				vo.on = false
			}
		}
	case 121:
		c.bend = 8192
		c.cc[1], c.cc[11] = 0, 127
		c.sustain = false
		c.cc[64] = 0
	case 123, 124, 125, 126, 127:
		for _, vo := range s.voices {
			if vo.on && vo.ch == ch {
				vo.held = false
				s.releaseVoice(vo)
			}
		}
	}
}

func (s *Synth) dataEntry(c *chanState) {
	msb, lsb := c.cc[6], c.cc[38]
	switch {
	case c.rpnMSB == 0 && c.rpnLSB == 0:
		c.bendRange = float64(msb) + float64(lsb)/100
		if c.bendRange > 24 {
			c.bendRange = 24
		}
	case c.rpnMSB == 0 && c.rpnLSB == 1:
		c.fineTune = float64((msb<<7|lsb)-8192) / 8192 * 100
	case c.rpnMSB == 0 && c.rpnLSB == 2:
		c.coarseTune = float64(msb - 64)
	}
}

func (s *Synth) Program(ch, p int) {
	s.ch[ch].prog = p
	s.ch[ch].preset = nil
}

func (s *Synth) Bend(ch, v int) { s.ch[ch].bend = v }

func (s *Synth) Pressure(ch, v int) { s.ch[ch].pressure = v }

func (s *Synth) NoteOff(ch, key int) {
	c := &s.ch[ch]
	for _, vo := range s.voices {
		if vo.on && vo.ch == ch && vo.key == key && !vo.released {
			vo.released = true
			if c.sustain {
				vo.held = true
				continue
			}
			s.releaseVoice(vo)
		}
	}
}

func (s *Synth) releaseVoice(vo *voice) {
	vo.released = true
	vo.volEnv.release()
	vo.modEnv.release()
}

func (s *Synth) allocVoice() *voice {
	for _, v := range s.voices {
		if !v.on {
			return v
		}
	}
	if len(s.voices) < s.maxV {
		v := &voice{}
		s.voices = append(s.voices, v)
		return v
	}
	// vol de voix : la plus faible / la plus ancienne relâchée
	var best *voice
	bestScore := math.Inf(1)
	for _, v := range s.voices {
		sc := float64(v.age) - 1e12*b2f(v.released) + 1e9*v.volEnv.gain()
		if sc < bestScore {
			bestScore, best = sc, v
		}
	}
	return best
}

func b2f(b bool) float64 {
	if b {
		return 1
	}
	return 0
}

func (s *Synth) NoteOn(ch, key, vel int) {
	if vel == 0 {
		s.NoteOff(ch, key)
		return
	}
	c := &s.ch[ch]
	p := s.presetFor(c)
	if p == nil {
		k := ch<<16 | c.bankMSB<<8 | c.prog
		if !s.missing[k] {
			s.missing[k] = true
		}
		return
	}
	// même note déjà jouée sur ce canal : on la relâche
	for _, vo := range s.voices {
		if vo.on && vo.ch == ch && vo.key == key && !vo.volEnv.isFinished() {
			vo.held = false
			s.releaseVoice(vo)
		}
	}
	s.age++
	for _, vp := range s.sf.Voices(p, key, vel) {
		if ex := int(vp.G[gExclusiveClass]); ex != 0 {
			for _, vo := range s.voices {
				if vo.on && vo.ch == ch && vo.excl == ex && vo.age != s.age {
					vo.killFast = true
					vo.volEnv.rel = 0.004
					s.releaseVoice(vo)
				}
			}
		}
		v := s.allocVoice()
		s.startVoice(v, ch, key, vel, &vp)
	}
}

func (e *env) isFinished() bool { return e.stage >= 6 }

func (s *Synth) startVoice(v *voice, ch, key, vel int, vp *VoiceParams) {
	g := vp.G
	sm := vp.Sample
	*v = voice{on: true, ch: ch, key: key, vel: vel, g: g, mods: vp.Mods, data: s.sf.Data, age: s.age}
	if g[gVelocity] >= 0 && g[gVelocity] <= 127 {
		v.vel = int(g[gVelocity])
	}
	for _, m := range v.mods {
		for _, src := range []uint16{m.Src, m.AmtSrc} {
			if src&0x80 != 0 && (src&0x7F == 71 || src&0x7F == 74) {
				v.ccFilter = true
			}
		}
	}
	s.evalMods(v)
	// générateurs effectifs (fichier + modulateurs) pour les paramètres fixés au départ
	for op := range g {
		g[op] += int32(math.Round(v.ms[op]))
	}
	v.excl = int(g[gExclusiveClass])
	v.start = sm.Start + int(g[gStartOff]) + 32768*int(g[gStartCoarse])
	v.end = sm.End + int(g[gEndOff]) + 32768*int(g[gEndCoarse])
	v.ls = sm.LoopStart + int(g[gLoopStartOff]) + 32768*int(g[gLoopStartCoars])
	v.le = sm.LoopEnd + int(g[gLoopEndOff]) + 32768*int(g[gLoopEndCoarse])
	if v.end > len(v.data) {
		v.end = len(v.data)
	}
	if v.start < 0 {
		v.start = 0
	}
	if v.start >= v.end {
		v.on = false
		return
	}
	v.mode = int(g[gSampleModes]) & 3
	if v.mode == 2 {
		v.mode = 0
	}
	if v.ls < v.start {
		v.ls = v.start
	}
	if v.le > v.end {
		v.le = v.end
	}
	if v.le-v.ls < 2 {
		v.mode = 0
	}
	v.pos = float64(v.start)

	root := int(sm.Pitch)
	if g[gRootKey] >= 0 && g[gRootKey] <= 127 {
		root = int(g[gRootKey])
	}
	if root > 127 {
		root = 60
	}
	k := float64(key)
	if g[gKeynum] >= 0 && g[gKeynum] <= 127 {
		k = float64(g[gKeynum])
	}
	v.pitchKey = (k-float64(root))*float64(g[gScaleTuning]) + float64(g[gCoarseTune])*100 +
		float64(g[gFineTune]) + float64(sm.Correction)
	rate := float64(sm.Rate)
	if rate <= 0 {
		rate = 44100
	}
	v.rateMul = rate / s.rate

	keyF := float64(key)
	if g[gKeynum] >= 0 && g[gKeynum] <= 127 {
		keyF = float64(g[gKeynum])
	}
	v.volEnv = env{isVol: true,
		delay: tc2s(float64(g[gDelayVolEnv])),
		att:   tc2s(float64(g[gAttackVolEnv])),
		hold:  tc2s(float64(g[gHoldVolEnv]) + (60-keyF)*float64(g[gKeyToVolHold])),
		dec:   tc2s(float64(g[gDecayVolEnv]) + (60-keyF)*float64(g[gKeyToVolDecay])),
		sus:   clamp(float64(g[gSustainVolEnv]), 0, 1440),
		rel:   tc2s(float64(g[gReleaseVolEnv])),
	}
	if v.volEnv.sus > 960 {
		v.volEnv.sus = 960
	}
	v.modEnv = env{
		delay: tc2s(float64(g[gDelayModEnv])),
		att:   tc2s(float64(g[gAttackModEnv])),
		hold:  tc2s(float64(g[gHoldModEnv]) + (60-keyF)*float64(g[gKeyToModHold])),
		dec:   tc2s(float64(g[gDecayModEnv]) + (60-keyF)*float64(g[gKeyToModDecay])),
		sus:   1 - clamp(float64(g[gSustainModEnv]), 0, 1000)/1000,
		rel:   tc2s(float64(g[gReleaseModEnv])),
	}
	v.volEnv.advance(0)
	v.modEnv.advance(0)
	v.vib = lfo{delay: tc2s(float64(g[gDelayVibLFO])), freq: absCents(float64(g[gFreqVibLFO]))}
	v.mod = lfo{delay: tc2s(float64(g[gDelayModLFO])), freq: absCents(float64(g[gFreqModLFO]))}
	v.prevGain = -1
}

func absCents(c float64) float64 { return 8.176 * math.Pow(2, c/1200) }

func clamp(x, lo, hi float64) float64 {
	if x < lo {
		return lo
	}
	if x > hi {
		return hi
	}
	return x
}

// Render : calcule n trames (n ≤ block) et les ajoute à l, r.
func (s *Synth) Render(l, r []float64) {
	n := len(l)
	for i := 0; i < n; i++ {
		s.bufL[i], s.bufR[i], s.revL[i], s.revR[i], s.choL[i], s.choR[i] = 0, 0, 0, 0, 0, 0
	}
	dt := float64(n) / s.rate
	for _, v := range s.voices {
		if v.on {
			s.renderVoice(v, n, dt)
		}
	}
	// effets : la réverbération et le chorus reçoivent leurs envois
	s.cho.process(s.choL[:n], s.choR[:n], s.bufL[:n], s.bufR[:n], s.revL[:n], s.revR[:n])
	s.rev.process(s.revL[:n], s.revR[:n], s.bufL[:n], s.bufR[:n], s.revMul)
	for i := 0; i < n; i++ {
		l[i] += s.bufL[i]
		r[i] += s.bufR[i]
	}
}

func (s *Synth) renderVoice(v *voice, n int, dt float64) {
	c := &s.ch[v.ch]
	g := &v.g

	// modulateurs SF2 (au début du bloc)
	s.evalMods(v)
	ms := &v.ms
	eff := func(op int) float64 { return float64(g[op]) + ms[op] }
	modEnv := v.modEnv.level
	vib := v.vib.value()
	mod := v.mod.value()

	cents := v.pitchKey + ms[gPitch] + c.fineTune + c.coarseTune*100 +
		modEnv*eff(gModEnvToPitch) +
		vib*eff(gVibLfoToPitch) +
		mod*eff(gModLfoToPitch)
	inc := v.rateMul * math.Pow(2, cents/1200)

	// atténuation (cB) : générateur ×0,4 (convention EMU/FluidSynth) + modulateurs
	att := float64(g[gAttenuation])*0.4 + ms[gAttenuation] + mod*eff(gModLfoToVol)
	if att < 0 {
		att = 0
	}

	// avance des enveloppes et LFO
	v.volEnv.advance(dt)
	v.modEnv.advance(dt)
	v.vib.t += dt
	v.mod.t += dt
	envGain := v.volEnv.gain()
	gain := envGain * math.Pow(10, -att/200)
	if v.prevGain < 0 {
		v.prevGain = 0 // premier bloc : montée depuis 0 (pas de clic)
	}

	// filtre
	fc := eff(gFilterFc) + modEnv*eff(gModEnvToFc) + mod*eff(gModLfoToFc)
	qdB := clamp(eff(gFilterQ)/10, 0, 96)
	if !v.ccFilter {
		fc += float64(c.cc[74]-64) * 40 // brillance (XG/GS)
		qdB = math.Max(0, qdB+float64(c.cc[71]-64)*0.15)
	}
	useFilter := (fc < 13500 || qdB > 0.01)
	if useFilter {
		hz := clamp(absCents(fc), 5, 0.45*s.rate)
		q := math.Pow(10, (qdB-3.01)/20)
		w := 2 * math.Pi * hz / s.rate
		alpha := math.Sin(w) / (2 * q)
		cw := math.Cos(w)
		a0 := 1 + alpha
		comp := 1 / math.Sqrt(math.Max(q, 0.7071)/0.7071)
		v.b0 = (1 - cw) / 2 / a0 * comp
		v.b1 = (1 - cw) / a0 * comp
		v.b2 = v.b0
		v.a1 = -2 * cw / a0
		v.a2 = (1 - alpha) / a0
	}

	// panoramique (puissance constante)
	pan := clamp(eff(gPan), -500, 500)
	th := (pan + 500) / 1000 * math.Pi / 2
	pl, pr := math.Cos(th), math.Sin(th)
	revSend := clamp(eff(gReverbSend)/1000, 0, 1)
	choSend := clamp(eff(gChorusSend)/1000, 0, 1)

	// lecture de l'échantillon (interpolation cubique d'Hermite)
	out := s.tmp[:n]
	d := v.data
	loopOn := v.mode == 1 || (v.mode == 3 && !v.released)
	ls, le := float64(v.ls), float64(v.le)
	loopLen := le - ls
	end := float64(v.end)
	done := false
	at := func(i int) float64 {
		if loopOn && i >= v.le {
			i -= v.le - v.ls
		}
		if i < v.start || i >= v.end {
			return 0
		}
		return float64(d[i])
	}
	for i := 0; i < n; i++ {
		if done {
			out[i] = 0
			continue
		}
		ip := int(v.pos)
		f := v.pos - float64(ip)
		var y0, y1, y2, y3 float64
		if ip-1 >= v.start && ip+2 < v.le && ip+2 < v.end {
			y0, y1, y2, y3 = float64(d[ip-1]), float64(d[ip]), float64(d[ip+1]), float64(d[ip+2])
		} else {
			y0, y1, y2, y3 = at(ip-1), at(ip), at(ip+1), at(ip+2)
		}
		c1 := 0.5 * (y2 - y0)
		c2 := y0 - 2.5*y1 + 2*y2 - 0.5*y3
		c3 := 0.5*(y3-y0) + 1.5*(y1-y2)
		out[i] = ((c3*f+c2)*f+c1)*f + y1
		v.pos += inc
		if loopOn {
			for v.pos >= le {
				v.pos -= loopLen
			}
		} else if v.pos >= end-1 {
			done = true
		}
	}
	if useFilter {
		b0, b1, b2, a1, a2 := v.b0, v.b1, v.b2, v.a1, v.a2
		z1, z2 := v.z1L, v.z2L
		for i := 0; i < n; i++ {
			x := out[i]
			y := b0*x + z1
			z1 = b1*x - a1*y + z2
			z2 = b2*x - a2*y
			out[i] = y
		}
		if math.IsNaN(z1) || math.IsInf(z1, 0) {
			z1, z2 = 0, 0
		}
		v.z1L, v.z2L = z1, z2
	}
	// gain interpolé sur le bloc (pas de crépitements)
	g0, g1 := v.prevGain, gain
	step := (g1 - g0) / float64(n)
	const scale = 1.0 / 32768
	for i := 0; i < n; i++ {
		x := out[i] * (g0 + step*float64(i+1)) * scale
		xl, xr := x*pl, x*pr
		s.bufL[i] += xl
		s.bufR[i] += xr
		if revSend > 0 {
			s.revL[i] += xl * revSend
			s.revR[i] += xr * revSend
		}
		if choSend > 0 {
			s.choL[i] += xl * choSend
			s.choR[i] += xr * choSend
		}
	}
	v.prevGain = gain
	if done || v.volEnv.isFinished() || (v.volEnv.stage >= 3 && v.volEnv.level >= 960) {
		v.on = false
	}
}

// Busy : vrai tant qu'une voix sonne encore.
func (s *Synth) Busy() bool {
	for _, v := range s.voices {
		if v.on {
			return true
		}
	}
	return false
}

func (s *Synth) ReleaseAll() {
	for i := range s.ch {
		s.ch[i].sustain = false
	}
	for _, v := range s.voices {
		if v.on {
			v.held = false
			s.releaseVoice(v)
		}
	}
}

// evalMods : calcule la contribution de chaque modulateur.
func (s *Synth) evalMods(v *voice) {
	for i := range v.ms {
		v.ms[i] = 0
	}
	c := &s.ch[v.ch]
	for i := range v.mods {
		m := &v.mods[i]
		a := s.srcValue(m.Src, v, c)
		if a == 0 {
			continue
		}
		b := s.srcValue(m.AmtSrc, v, c)
		x := m.Amount * a * b
		if m.Trans == 2 && x < 0 {
			x = -x // transformation « valeur absolue »
		}
		v.ms[m.Dest] += x
	}
}

func concaveN(x float64) float64 {
	if x >= 1 {
		return 1
	}
	if x <= 0 {
		return 0
	}
	y := -(400.0 / 960) * math.Log10(1-x)
	if y > 1 {
		y = 1
	}
	return y
}

func convexN(x float64) float64 { return 1 - concaveN(1-x) }

// srcValue : valeur d'une source de modulateur, après transformation.
func (s *Synth) srcValue(src uint16, v *voice, c *chanState) float64 {
	idx := int(src & 0x7F)
	var raw, rng float64
	if src&0x80 != 0 {
		raw, rng = float64(c.cc[idx]), 128
	} else {
		switch idx {
		case 0:
			return 1 // « aucun contrôleur » = 1
		case 2:
			raw, rng = float64(v.vel), 128
		case 3:
			raw, rng = float64(v.key), 128
		case 10:
			return 0
		case 13:
			raw, rng = float64(c.pressure), 128
		case 14:
			raw, rng = float64(c.bend), 16384
		case 16:
			raw, rng = c.bendRange, 127
		default:
			return 0
		}
	}
	neg := src&0x100 != 0
	bip := src&0x200 != 0
	typ := src >> 10
	var x float64
	switch typ {
	case 1, 2: // concave / convexe : échelle 0..127 → 0..1
		x = raw / (rng - 1)
	default:
		x = raw / rng
	}
	if neg {
		if typ == 1 || typ == 2 {
			x = 1 - x
		} else {
			x = 1 - raw/rng
		}
	}
	x = clamp(x, 0, 1)
	f := func(x float64) float64 {
		switch typ {
		case 1:
			return concaveN(x)
		case 2:
			return convexN(x)
		case 3:
			if x >= 0.5 {
				return 1
			}
			return 0
		}
		return x
	}
	if !bip {
		return f(x)
	}
	switch typ {
	case 1, 2:
		if x > 0.5 {
			return f(2*x - 1)
		}
		return -f(1 - 2*x)
	case 3:
		if x >= 0.5 {
			return 1
		}
		return -1
	}
	return 2*x - 1
}
