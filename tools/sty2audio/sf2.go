package main

// Lecture des SoundFont SF2 (spécification 2.01/2.04) : préréglages,
// instruments, zones, générateurs et échantillons 16 bits.

import (
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"strings"
)

// Numéros des générateurs SF2 utilisés.
const (
	gStartOff       = 0
	gEndOff         = 1
	gLoopStartOff   = 2
	gLoopEndOff     = 3
	gStartCoarse    = 4
	gModLfoToPitch  = 5
	gVibLfoToPitch  = 6
	gModEnvToPitch  = 7
	gFilterFc       = 8
	gFilterQ        = 9
	gModLfoToFc     = 10
	gModEnvToFc     = 11
	gEndCoarse      = 12
	gModLfoToVol    = 13
	gChorusSend     = 15
	gReverbSend     = 16
	gPan            = 17
	gDelayModLFO    = 21
	gFreqModLFO     = 22
	gDelayVibLFO    = 23
	gFreqVibLFO     = 24
	gDelayModEnv    = 25
	gAttackModEnv   = 26
	gHoldModEnv     = 27
	gDecayModEnv    = 28
	gSustainModEnv  = 29
	gReleaseModEnv  = 30
	gKeyToModHold   = 31
	gKeyToModDecay  = 32
	gDelayVolEnv    = 33
	gAttackVolEnv   = 34
	gHoldVolEnv     = 35
	gDecayVolEnv    = 36
	gSustainVolEnv  = 37
	gReleaseVolEnv  = 38
	gKeyToVolHold   = 39
	gKeyToVolDecay  = 40
	gInstrument     = 41
	gKeyRange       = 43
	gVelRange       = 44
	gLoopStartCoars = 45
	gKeynum         = 46
	gVelocity       = 47
	gAttenuation    = 48
	gLoopEndCoarse  = 50
	gCoarseTune     = 51
	gFineTune       = 52
	gSampleID       = 53
	gSampleModes    = 54
	gScaleTuning    = 56
	gExclusiveClass = 57
	gRootKey        = 58
	gPitch          = 59 // pseudo-générateur : hauteur (cents), cible du pitch bend
	numGens         = 61
)

// Mod : modulateur SF2.
type Mod struct {
	Src, Dest, AmtSrc, Trans uint16
	Amount                   float64
}

func (m *Mod) same(o *Mod) bool {
	return m.Src == o.Src && m.Dest == o.Dest && m.AmtSrc == o.AmtSrc && m.Trans == o.Trans
}

// modulateurs par défaut (SF2.04 §8.4, réglages de FluidSynth)
var defaultMods = []Mod{
	{Src: 0x0502, Dest: gAttenuation, Amount: 960},                // vélocité → atténuation
	{Src: 0x0102, Dest: gFilterFc, AmtSrc: 0x0D02, Amount: -2400}, // vélocité → filtre (sous 64, comme FluidSynth)
	{Src: 0x000D, Dest: gVibLfoToPitch, Amount: 50},               // pression → vibrato
	{Src: 0x0081, Dest: gVibLfoToPitch, Amount: 50},               // molette → vibrato
	{Src: 0x0587, Dest: gAttenuation, Amount: 960},                // CC7 volume
	{Src: 0x028A, Dest: gPan, Amount: 1000},                       // CC10 panoramique
	{Src: 0x058B, Dest: gAttenuation, Amount: 960},                // CC11 expression
	{Src: 0x00DB, Dest: gReverbSend, Amount: 200},                 // CC91 réverbération
	{Src: 0x00DD, Dest: gChorusSend, Amount: 200},                 // CC93 chorus
	{Src: 0x020E, Dest: gPitch, AmtSrc: 0x0010, Amount: 12700},    // pitch bend
}

// mergeMods : remplace les modulateurs identiques, ajoute les autres.
func mergeMods(base []Mod, over []Mod) []Mod {
	out := append([]Mod(nil), base...)
	for _, m := range over {
		found := false
		for i := range out {
			if out[i].same(&m) {
				out[i] = m
				found = true
				break
			}
		}
		if !found {
			out = append(out, m)
		}
	}
	return out
}

type genSet struct {
	v   [numGens]int32
	set [numGens]bool
}

func (g *genSet) put(op uint16, amt int16, raw uint16) {
	if int(op) >= numGens {
		return
	}
	if op == gKeyRange || op == gVelRange {
		g.v[op] = int32(raw) // lo | hi<<8
	} else if op == gInstrument || op == gSampleID {
		g.v[op] = int32(raw)
	} else {
		g.v[op] = int32(amt)
	}
	g.set[op] = true
}

func rangeOf(g *genSet, op int) (lo, hi int) {
	if !g.set[op] {
		return 0, 127
	}
	return int(g.v[op] & 0xFF), int(g.v[op] >> 8 & 0xFF)
}

type zone struct {
	gens   genSet
	mods   []Mod
	keyLo  int
	keyHi  int
	velLo  int
	velHi  int
	target int // instrument (préréglage) ou échantillon (instrument)
}

func (z *zone) match(key, vel int) bool {
	return key >= z.keyLo && key <= z.keyHi && vel >= z.velLo && vel <= z.velHi
}

type Instrument struct {
	Name   string
	gmods  []Mod
	global *genSet
	zones  []zone
}

type Preset struct {
	Name       string
	Bank, Prog int
	gmods      []Mod
	global     *genSet
	zones      []zone
}

type Sample struct {
	Name               string
	Start, End         int
	LoopStart, LoopEnd int
	Rate               int
	Pitch              int
	Correction         int
	Type               int
}

type SoundFont struct {
	Path    string
	Name    string
	Data    []int16
	Samples []Sample
	Insts   []*Instrument
	Presets []*Preset
	byKey   map[int]*Preset
}

// VoiceParams : générateurs finaux d'une voix (instrument + préréglage).
type VoiceParams struct {
	G      [numGens]int32
	Mods   []Mod
	Sample *Sample
}

func LoadSoundFont(path string) (*SoundFont, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	if len(b) < 12 || string(b[0:4]) != "RIFF" || string(b[8:12]) != "sfbk" {
		return nil, errors.New("ce fichier n'est pas une SoundFont SF2")
	}
	sf := &SoundFont{Path: path, byKey: map[int]*Preset{}}
	var phdr, pbag, pgen, pmod, inst, ibag, igen, imod, shdr []byte
	var smpl []byte
	isVorbis := false
	var walk func(d []byte)
	walk = func(d []byte) {
		p := 0
		for p+8 <= len(d) {
			id := string(d[p : p+4])
			n := int(binary.LittleEndian.Uint32(d[p+4 : p+8]))
			p += 8
			if n < 0 || p+n > len(d) {
				n = len(d) - p
			}
			c := d[p : p+n]
			switch id {
			case "LIST":
				if len(c) >= 4 {
					walk(c[4:])
				}
			case "INAM":
				sf.Name = strings.TrimRight(string(c), "\x00 ")
			case "ifil":
				if len(c) >= 2 && binary.LittleEndian.Uint16(c) >= 3 {
					isVorbis = true
				}
			case "smpl":
				smpl = c
			case "phdr":
				phdr = c
			case "pbag":
				pbag = c
			case "pgen":
				pgen = c
			case "pmod":
				pmod = c
			case "imod":
				imod = c
			case "inst":
				inst = c
			case "ibag":
				ibag = c
			case "igen":
				igen = c
			case "shdr":
				shdr = c
			}
			p += n
			if n%2 == 1 {
				p++
			}
		}
	}
	walk(b[12:])
	if isVorbis {
		return nil, errors.New("les SoundFont compressées SF3 ne sont pas prises en charge : utilisez la version .sf2")
	}
	if phdr == nil || inst == nil || shdr == nil || smpl == nil {
		return nil, errors.New("SoundFont incomplète ou endommagée")
	}
	sf.Data = make([]int16, len(smpl)/2)
	for i := range sf.Data {
		sf.Data[i] = int16(binary.LittleEndian.Uint16(smpl[2*i:]))
	}
	smpl = nil

	// Échantillons
	for i := 0; i+46 <= len(shdr); i += 46 {
		r := shdr[i:]
		s := Sample{
			Name:       cstr(r[0:20]),
			Start:      int(binary.LittleEndian.Uint32(r[20:])),
			End:        int(binary.LittleEndian.Uint32(r[24:])),
			LoopStart:  int(binary.LittleEndian.Uint32(r[28:])),
			LoopEnd:    int(binary.LittleEndian.Uint32(r[32:])),
			Rate:       int(binary.LittleEndian.Uint32(r[36:])),
			Pitch:      int(r[40]),
			Correction: int(int8(r[41])),
			Type:       int(binary.LittleEndian.Uint16(r[44:])),
		}
		sf.Samples = append(sf.Samples, s)
	}
	if len(sf.Samples) > 0 {
		sf.Samples = sf.Samples[:len(sf.Samples)-1] // EOS
	}

	readGens := func(gens []byte, from, to int) []struct {
		op  uint16
		amt int16
		raw uint16
	} {
		var out []struct {
			op  uint16
			amt int16
			raw uint16
		}
		for k := from; k < to && 4*k+4 <= len(gens); k++ {
			r := gens[4*k:]
			raw := binary.LittleEndian.Uint16(r[2:])
			out = append(out, struct {
				op  uint16
				amt int16
				raw uint16
			}{binary.LittleEndian.Uint16(r), int16(raw), raw})
		}
		return out
	}
	bagGen := func(bags []byte, i int) int {
		if 4*i+2 > len(bags) {
			return 0
		}
		return int(binary.LittleEndian.Uint16(bags[4*i:]))
	}
	bagMod := func(bags []byte, i int) int {
		if 4*i+4 > len(bags) {
			return 0
		}
		return int(binary.LittleEndian.Uint16(bags[4*i+2:]))
	}
	readMods := func(mods []byte, from, to int) []Mod {
		var out []Mod
		for k := from; k < to && 10*k+10 <= len(mods); k++ {
			r := mods[10*k:]
			m := Mod{
				Src:    binary.LittleEndian.Uint16(r),
				Dest:   binary.LittleEndian.Uint16(r[2:]),
				Amount: float64(int16(binary.LittleEndian.Uint16(r[4:]))),
				AmtSrc: binary.LittleEndian.Uint16(r[6:]),
				Trans:  binary.LittleEndian.Uint16(r[8:]),
			}
			if m.Dest >= numGens || m.Dest == gPitch {
				continue // modulateurs liés ou cibles inconnues : ignorés
			}
			out = mergeMods(out, []Mod{m})
		}
		return out
	}
	buildZones := func(bags, gens, mods []byte, b0, b1 int, term uint16) (*genSet, []Mod, []zone) {
		var global *genSet
		var gmods []Mod
		var zs []zone
		for bi := b0; bi < b1; bi++ {
			g0, g1 := bagGen(bags, bi), bagGen(bags, bi+1)
			var gs genSet
			for _, g := range readGens(gens, g0, g1) {
				gs.put(g.op, g.amt, g.raw)
			}
			ms := readMods(mods, bagMod(bags, bi), bagMod(bags, bi+1))
			if !gs.set[term] {
				if bi == b0 && global == nil {
					cp := gs
					global = &cp
					gmods = ms
				}
				continue
			}
			z := zone{gens: gs, mods: ms, target: int(gs.v[term])}
			z.keyLo, z.keyHi = rangeOf(&gs, gKeyRange)
			z.velLo, z.velHi = rangeOf(&gs, gVelRange)
			zs = append(zs, z)
		}
		return global, gmods, zs
	}

	// Instruments
	nInst := len(inst)/22 - 1
	for i := 0; i < nInst; i++ {
		r := inst[22*i:]
		b0 := int(binary.LittleEndian.Uint16(r[20:]))
		b1 := int(binary.LittleEndian.Uint16(inst[22*(i+1)+20:]))
		in := &Instrument{Name: cstr(r[:20])}
		in.global, in.gmods, in.zones = buildZones(ibag, igen, imod, b0, b1, gSampleID)
		sf.Insts = append(sf.Insts, in)
	}
	// Préréglages
	nP := len(phdr)/38 - 1
	for i := 0; i < nP; i++ {
		r := phdr[38*i:]
		b0 := int(binary.LittleEndian.Uint16(r[24:]))
		b1 := int(binary.LittleEndian.Uint16(phdr[38*(i+1)+24:]))
		pr := &Preset{
			Name: cstr(r[:20]),
			Prog: int(binary.LittleEndian.Uint16(r[20:])),
			Bank: int(binary.LittleEndian.Uint16(r[22:])),
		}
		pr.global, pr.gmods, pr.zones = buildZones(pbag, pgen, pmod, b0, b1, gInstrument)
		sf.Presets = append(sf.Presets, pr)
		k := pr.Bank<<8 | pr.Prog
		if _, dup := sf.byKey[k]; !dup {
			sf.byKey[k] = pr
		}
	}
	if len(sf.Presets) == 0 {
		return nil, errors.New("la SoundFont ne contient aucun son")
	}
	return sf, nil
}

func cstr(b []byte) string {
	if i := strings.IndexByte(string(b), 0); i >= 0 {
		b = b[:i]
	}
	return strings.TrimSpace(string(b))
}

// FindPreset : son pour banque/programme, avec repli (banque 0, programme 0…).
func (sf *SoundFont) FindPreset(bank, prog int, drum bool) *Preset {
	if drum {
		if p := sf.byKey[128<<8|prog]; p != nil {
			return p
		}
		// kits XG/GS : repli sur le kit de base du groupe (ex. 26 → 24, 41 → 40)
		if p := sf.byKey[128<<8|(prog&^7)]; p != nil {
			return p
		}
		if p := sf.byKey[128<<8]; p != nil {
			return p
		}
		for _, p := range sf.Presets {
			if p.Bank == 128 {
				return p
			}
		}
		return nil
	}
	if p := sf.byKey[bank<<8|prog]; p != nil {
		return p
	}
	if p := sf.byKey[prog]; p != nil {
		return p
	}
	// variations GS : banque inférieure la plus proche
	for b := bank - 1; b > 0; b-- {
		if p := sf.byKey[b<<8|prog]; p != nil {
			return p
		}
	}
	return sf.byKey[0]
}

// gens qui ne s'ajoutent pas au niveau préréglage
func presetSkip(op int) bool {
	switch op {
	case gStartOff, gEndOff, gLoopStartOff, gLoopEndOff, gStartCoarse, gEndCoarse,
		gLoopStartCoars, gLoopEndCoarse, gKeynum, gVelocity, gSampleModes,
		gExclusiveClass, gRootKey, gInstrument, gSampleID, gKeyRange, gVelRange:
		return true
	}
	return false
}

var genDefaults = func() [numGens]int32 {
	var d [numGens]int32
	d[gFilterFc] = 13500
	for _, op := range []int{gDelayModLFO, gDelayVibLFO, gDelayModEnv, gAttackModEnv, gHoldModEnv,
		gDecayModEnv, gReleaseModEnv, gDelayVolEnv, gAttackVolEnv, gHoldVolEnv, gDecayVolEnv, gReleaseVolEnv} {
		d[op] = -12000
	}
	d[gKeynum] = -1
	d[gVelocity] = -1
	d[gScaleTuning] = 100
	d[gRootKey] = -1
	return d
}()

// Voices : toutes les voix à déclencher pour une note.
func (sf *SoundFont) Voices(p *Preset, key, vel int) []VoiceParams {
	var out []VoiceParams
	for pi := range p.zones {
		pz := &p.zones[pi]
		if !pz.match(key, vel) || pz.target >= len(sf.Insts) {
			continue
		}
		in := sf.Insts[pz.target]
		for ii := range in.zones {
			iz := &in.zones[ii]
			if !iz.match(key, vel) || iz.target >= len(sf.Samples) {
				continue
			}
			smp := &sf.Samples[iz.target]
			if smp.Type&0x8000 != 0 || smp.End <= smp.Start || smp.End > len(sf.Data) {
				continue // échantillon ROM ou invalide
			}
			vp := VoiceParams{G: genDefaults, Sample: smp}
			if in.global != nil {
				for op := 0; op < numGens; op++ {
					if in.global.set[op] {
						vp.G[op] = in.global.v[op]
					}
				}
			}
			for op := 0; op < numGens; op++ {
				if iz.gens.set[op] {
					vp.G[op] = iz.gens.v[op]
				}
			}
			for op := 0; op < numGens; op++ {
				if presetSkip(op) {
					continue
				}
				if pz.gens.set[op] {
					vp.G[op] += pz.gens.v[op]
				} else if p.global != nil && p.global.set[op] {
					vp.G[op] += p.global.v[op]
				}
			}
			// modulateurs : défaut ← instrument (global puis zone), puis préréglage ajouté
			im := mergeMods(defaultMods, mergeMods(in.gmods, iz.mods))
			pm := mergeMods(p.gmods, pz.mods)
			for _, m := range pm {
				added := false
				for i := range im {
					if im[i].same(&m) {
						im[i].Amount += m.Amount
						added = true
						break
					}
				}
				if !added {
					im = append(im, m)
				}
			}
			vp.Mods = im
			out = append(out, vp)
		}
	}
	return out
}

func (sf *SoundFont) String() string {
	n := sf.Name
	if n == "" {
		n = "?"
	}
	return fmt.Sprintf("%s (%d sons)", n, len(sf.Presets))
}
