package main

// Lecture des SoundFont SF2 (format 2.x). Les sons sont lus à la demande :
// seuls les échantillons du son choisi sont chargés en mémoire.

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math"
	"os"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"
	"unsafe"

	"minipiano/oggvorbis"
)

var ErrSF3 = errors.New("SoundFont SF3 illisible")

type sfBag struct{ gen, mod uint16 }

type sfGen struct {
	op     uint16
	lo, hi uint8
	amt    int16
}

type sfInst struct {
	name string
	bag  uint16
}

type sfPHdr struct {
	name      string
	num, bank uint16
	bag       uint16
}

type sfSHdr struct {
	name               string
	start, end         uint32
	loopStart, loopEnd uint32
	rate               uint32
	pitch              uint8
	correction         int8
	link, typ          uint16
}

type SF2 struct {
	Path     string
	Name     string
	f        *os.File
	smplOff  int64
	smplSize int64
	sf3      bool // échantillons compressés (Ogg Vorbis)

	phdr       []sfPHdr
	pbag, ibag []sfBag
	pgen, igen []sfGen
	inst       []sfInst
	shdr       []sfSHdr

	mu    sync.Mutex
	cache map[int][]int16
}

type PresetInfo struct {
	Index int
	Name  string
	Bank  int
	Num   int
}

func cstr(b []byte) string {
	if i := strings.IndexByte(string(b), 0); i >= 0 {
		b = b[:i]
	}
	return strings.TrimSpace(string(b))
}

// OpenSF2 lit l'index du SoundFont (rapide, même pour un gros fichier).
func OpenSF2(path string) (*SF2, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	s := &SF2{Path: path, f: f, cache: map[int][]int16{}}
	if err := s.parse(); err != nil {
		f.Close()
		return nil, err
	}
	return s, nil
}

func (s *SF2) Close() { s.f.Close() }

func rd32(b []byte) uint32 { return binary.LittleEndian.Uint32(b) }
func rd16(b []byte) uint16 { return binary.LittleEndian.Uint16(b) }

func (s *SF2) parse() error {
	var hdr [12]byte
	if _, err := s.f.ReadAt(hdr[:], 0); err != nil {
		return errors.New("fichier SF2 illisible")
	}
	if string(hdr[0:4]) != "RIFF" || string(hdr[8:12]) != "sfbk" {
		return errors.New("ce fichier n'est pas un SoundFont SF2")
	}
	fi, err := s.f.Stat()
	if err != nil {
		return err
	}
	fileSize := fi.Size()
	pdta := map[string][]byte{}
	var haveSmpl bool
	pos := int64(12)
	for pos+8 <= fileSize {
		var ch [8]byte
		if _, err := s.f.ReadAt(ch[:], pos); err != nil {
			break
		}
		id, size := string(ch[0:4]), int64(rd32(ch[4:8]))
		body := pos + 8
		if id == "LIST" {
			var lt [4]byte
			if _, err := s.f.ReadAt(lt[:], body); err != nil {
				break
			}
			ltype := string(lt[:])
			p := body + 4
			stop := body + size
			if stop > fileSize {
				stop = fileSize
			}
			for p+8 <= stop {
				var sc [8]byte
				if _, err := s.f.ReadAt(sc[:], p); err != nil {
					break
				}
				sid, ssize := string(sc[0:4]), int64(rd32(sc[4:8]))
				sbody := p + 8
				switch {
				case ltype == "INFO" && sid == "ifil" && ssize >= 4:
					var v [4]byte
					s.f.ReadAt(v[:], sbody)
					if rd16(v[0:2]) >= 3 {
						s.sf3 = true
					}
				case ltype == "INFO" && sid == "INAM" && ssize < 4096:
					b := make([]byte, ssize)
					s.f.ReadAt(b, sbody)
					s.Name = cstr(b)
				case ltype == "sdta" && sid == "smpl":
					s.smplOff, s.smplSize = sbody, ssize
					if s.smplOff+s.smplSize > fileSize {
						s.smplSize = fileSize - s.smplOff
					}
					haveSmpl = true
				case ltype == "pdta":
					if ssize > 256<<20 {
						return errors.New("SoundFont corrompu")
					}
					b := make([]byte, ssize)
					if _, err := s.f.ReadAt(b, sbody); err != nil && err != io.EOF {
						return errors.New("SoundFont tronqué")
					}
					pdta[sid] = b
				}
				p = sbody + ssize + (ssize & 1)
			}
		}
		pos = body + size + (size & 1)
	}
	if !haveSmpl {
		return errors.New("ce SoundFont ne contient pas d'échantillons (SF2 sans données)")
	}
	need := []string{"phdr", "pbag", "pgen", "inst", "ibag", "igen", "shdr"}
	for _, n := range need {
		if len(pdta[n]) == 0 {
			return errors.New("SoundFont incomplet (" + n + " manquant)")
		}
	}
	b := pdta["phdr"]
	for i := 0; i+38 <= len(b); i += 38 {
		s.phdr = append(s.phdr, sfPHdr{name: cstr(b[i : i+20]), num: rd16(b[i+20:]), bank: rd16(b[i+22:]), bag: rd16(b[i+24:])})
	}
	rdBags := func(b []byte) (out []sfBag) {
		for i := 0; i+4 <= len(b); i += 4 {
			out = append(out, sfBag{rd16(b[i:]), rd16(b[i+2:])})
		}
		return
	}
	rdGens := func(b []byte) (out []sfGen) {
		for i := 0; i+4 <= len(b); i += 4 {
			out = append(out, sfGen{op: rd16(b[i:]), lo: b[i+2], hi: b[i+3], amt: int16(rd16(b[i+2:]))})
		}
		return
	}
	s.pbag, s.ibag = rdBags(pdta["pbag"]), rdBags(pdta["ibag"])
	s.pgen, s.igen = rdGens(pdta["pgen"]), rdGens(pdta["igen"])
	b = pdta["inst"]
	for i := 0; i+22 <= len(b); i += 22 {
		s.inst = append(s.inst, sfInst{name: cstr(b[i : i+20]), bag: rd16(b[i+20:])})
	}
	b = pdta["shdr"]
	for i := 0; i+46 <= len(b); i += 46 {
		s.shdr = append(s.shdr, sfSHdr{
			name: cstr(b[i : i+20]), start: rd32(b[i+20:]), end: rd32(b[i+24:]),
			loopStart: rd32(b[i+28:]), loopEnd: rd32(b[i+32:]), rate: rd32(b[i+36:]),
			pitch: b[i+40], correction: int8(b[i+41]), link: rd16(b[i+42:]), typ: rd16(b[i+44:]),
		})
	}
	if len(s.phdr) < 2 {
		return errors.New("ce SoundFont ne contient aucun son")
	}
	for _, h := range s.shdr {
		if h.typ&0x10 != 0 {
			s.sf3 = true
		}
	}
	return nil
}

// Liste des sons (presets) du fichier, triée par banque puis numéro.
func (s *SF2) Presets() []PresetInfo {
	var out []PresetInfo
	for i := 0; i < len(s.phdr)-1; i++ { // le dernier est « EOP »
		h := s.phdr[i]
		name := h.name
		if name == "" {
			name = fmt.Sprintf("Preset %d", h.num)
		}
		out = append(out, PresetInfo{Index: i, Name: name, Bank: int(h.bank), Num: int(h.num)})
	}
	sort.SliceStable(out, func(a, b int) bool {
		if out[a].Bank != out[b].Bank {
			return out[a].Bank < out[b].Bank
		}
		return out[a].Num < out[b].Num
	})
	return out
}

// ---------------------------------------------------------------- générateurs

type gset struct {
	v            [64]int
	keyLo, keyHi int
	velLo, velHi int
	hasInst      bool
	inst         int
	hasSample    bool
	sample       int
}

func newGset() *gset {
	return &gset{keyLo: 0, keyHi: 127, velLo: 0, velHi: 127}
}

func instDefaults() *gset {
	g := newGset()
	g.v[8] = 13500 // initialFilterFc
	for _, op := range []int{21, 23, 25, 26, 27, 28, 30, 33, 34, 35, 36, 38} {
		g.v[op] = -12000
	}
	g.v[56] = 100 // scaleTuning
	g.v[58] = -1  // overridingRootKey
	return g
}

func (g *gset) apply(gens []sfGen) {
	for _, x := range gens {
		switch x.op {
		case 43:
			g.keyLo, g.keyHi = int(x.lo), int(x.hi)
		case 44:
			g.velLo, g.velHi = int(x.lo), int(x.hi)
		case 41:
			g.hasInst, g.inst = true, int(uint16(x.amt))
		case 53:
			g.hasSample, g.sample = true, int(uint16(x.amt))
		default:
			if x.op < 64 {
				g.v[x.op] = int(x.amt)
				if x.op == 54 || x.op == 57 {
					g.v[x.op] = int(uint16(x.amt))
				}
			}
		}
	}
}

// générateurs réservés à l'instrument (non additifs au niveau du preset)
func instOnly(op int) bool {
	switch op {
	case 0, 1, 2, 3, 4, 12, 45, 46, 47, 50, 54, 57, 58:
		return true
	}
	return false
}

func tc2s(tc int) float64 {
	if tc <= -11900 {
		return 0
	}
	t := math.Pow(2, float64(tc)/1200)
	if t > 100 {
		t = 100
	}
	return t
}

func absCents2Hz(c int) float64 { return 8.176 * math.Pow(2, float64(c)/1200) }

func (s *SF2) zones(bags []sfBag, gens []sfGen, from, to int) ([][]sfGen, error) {
	if from < 0 || to < from || to+1 > len(bags) {
		return nil, errors.New("SoundFont corrompu (zones)")
	}
	var out [][]sfGen
	for b := from; b < to; b++ {
		g0, g1 := int(bags[b].gen), int(bags[b+1].gen)
		if g0 > g1 || g1 > len(gens) {
			return nil, errors.New("SoundFont corrompu (générateurs)")
		}
		out = append(out, gens[g0:g1])
	}
	return out, nil
}

func (s *SF2) sampleData(id int) ([]int16, error) {
	s.mu.Lock()
	if d, ok := s.cache[id]; ok {
		s.mu.Unlock()
		return d, nil
	}
	s.mu.Unlock()
	if id < 0 || id >= len(s.shdr) {
		return nil, errors.New("SoundFont corrompu (échantillon)")
	}
	h := s.shdr[id]
	if h.end <= h.start {
		return nil, errors.New("échantillon vide")
	}
	if h.typ&0x10 != 0 || s.sf3 {
		return s.sampleDataVorbis(id, h)
	}
	total := s.smplSize / 2
	start := int64(h.start)
	n := int64(h.end-h.start) + 46
	if start >= total {
		return nil, errors.New("échantillon hors du fichier")
	}
	if start+n > total {
		n = total - start
	}
	// lecture directe dans le tableau final : pas de copie ni de conversion (le SF2 et le PC sont en little-endian)
	d := make([]int16, n)
	raw := unsafe.Slice((*byte)(unsafe.Pointer(&d[0])), len(d)*2)
	if _, err := s.f.ReadAt(raw, s.smplOff+start*2); err != nil && err != io.EOF {
		return nil, err
	}
	s.mu.Lock()
	s.cache[id] = d
	s.mu.Unlock()
	return d, nil
}

// SF3 : chaque échantillon est un petit flux Ogg Vorbis ; start/end sont des octets.
func (s *SF2) sampleDataVorbis(id int, h sfSHdr) ([]int16, error) {
	if int64(h.end) > s.smplSize || h.end <= h.start {
		return nil, errors.New("échantillon SF3 hors du fichier")
	}
	raw := make([]byte, h.end-h.start)
	if _, err := s.f.ReadAt(raw, s.smplOff+int64(h.start)); err != nil && err != io.EOF {
		return nil, err
	}
	if h.typ&0x10 == 0 { // fichier SF3 mais échantillon non compressé : ne devrait pas arriver
		return nil, errors.New("échantillon SF3 non compressé")
	}
	rd, err := oggvorbis.NewReader(bytes.NewReader(raw))
	if err != nil {
		return nil, err
	}
	ch := rd.Channels()
	if ch < 1 {
		return nil, errors.New("flux Vorbis invalide")
	}
	d := make([]int16, 0, len(raw)*6) // estimation : un flux Vorbis est environ 6 fois plus petit que le son décodé
	buf := make([]float32, 4096*ch)
	for {
		n, err := rd.Read(buf)
		for i := 0; i+ch <= n; i += ch {
			var sum float32
			for c := 0; c < ch; c++ {
				sum += buf[i+c]
			}
			v := sum / float32(ch) * 32767
			if v > 32767 {
				v = 32767
			} else if v < -32768 {
				v = -32768
			}
			d = append(d, int16(v))
		}
		if err != nil || n == 0 {
			break
		}
	}
	if len(d) < 2 {
		return nil, errors.New("échantillon SF3 vide")
	}
	d = append(d, make([]int16, 46)...)
	s.mu.Lock()
	s.cache[id] = d
	s.mu.Unlock()
	return d, nil
}

// LoadPreset construit les régions jouables d'un son et charge leurs échantillons.
func (s *SF2) LoadPreset(index int) ([]*Region, error) {
	if index < 0 || index >= len(s.phdr)-1 {
		return nil, errors.New("son introuvable")
	}
	ph, next := s.phdr[index], s.phdr[index+1]
	pz, err := s.zones(s.pbag, s.pgen, int(ph.bag), int(next.bag))
	if err != nil {
		return nil, err
	}
	var gp *gset
	var presetZones []*gset
	for i, z := range pz {
		g := newGset()
		if gp != nil {
			*g = *gp
			g.keyLo, g.keyHi, g.velLo, g.velHi = gp.keyLo, gp.keyHi, gp.velLo, gp.velHi
		}
		g.hasInst = false
		g.apply(z)
		if !g.hasInst {
			if i == 0 {
				gp = g
			}
			continue
		}
		presetZones = append(presetZones, g)
	}
	type zonePair struct{ ig, pg *gset }
	var pairs []zonePair
	for _, pzg := range presetZones {
		if pzg.inst < 0 || pzg.inst >= len(s.inst)-1 {
			continue
		}
		in, inNext := s.inst[pzg.inst], s.inst[pzg.inst+1]
		iz, err := s.zones(s.ibag, s.igen, int(in.bag), int(inNext.bag))
		if err != nil {
			return nil, err
		}
		var gi *gset
		for i, z := range iz {
			g := instDefaults()
			if gi != nil {
				*g = *gi
			}
			g.hasSample = false
			g.apply(z)
			if !g.hasSample {
				if i == 0 {
					gi = g
				}
				continue
			}
			pairs = append(pairs, zonePair{g, pzg})
		}
	}
	// les échantillons sont lus (et décodés pour un SF3) en parallèle : chargement bien plus rapide des gros sons
	ids := make([]int, 0, len(pairs))
	for _, p := range pairs {
		if p.ig.sample >= 0 && p.ig.sample < len(s.shdr)-1 {
			ids = append(ids, p.ig.sample)
		}
	}
	s.preload(ids)
	var regions []*Region
	for _, p := range pairs {
		r, err := s.buildRegion(p.ig, p.pg)
		if err != nil {
			return nil, err
		}
		if r != nil {
			regions = append(regions, r)
		}
	}
	if len(regions) == 0 {
		return nil, errors.New("ce son ne contient aucun échantillon jouable")
	}
	return regions, nil
}

// preload charge plusieurs échantillons en même temps (disque rapide et processeurs multiples)
func (s *SF2) preload(ids []int) {
	seen := map[int]bool{}
	var todo []int
	s.mu.Lock()
	for _, id := range ids {
		if _, ok := s.cache[id]; !ok && !seen[id] {
			seen[id] = true
			todo = append(todo, id)
		}
	}
	s.mu.Unlock()
	if len(todo) < 2 {
		return
	}
	// on laisse des cœurs libres au son (et à l'hôte, ex. Ableton) pendant le chargement :
	// moitié des cœurs au plus, 4 maximum
	n := runtime.NumCPU() / 2
	if n > 4 {
		n = 4
	}
	if n < 1 {
		n = 1
	}
	if n > len(todo) {
		n = len(todo)
	}
	jobs := make(chan int)
	var wg sync.WaitGroup
	for w := 0; w < n; w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for id := range jobs {
				func() {
					defer func() { recover() }() // un échantillon abîmé ne bloque pas le chargement
					_, _ = s.sampleData(id)
				}()
				time.Sleep(300 * time.Microsecond) // courte pause : le son passe en priorité
			}
		}()
	}
	for _, id := range todo {
		jobs <- id
	}
	close(jobs)
	wg.Wait()
}

func maxi(a, b int) int {
	if a > b {
		return a
	}
	return b
}
func mini(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func (s *SF2) buildRegion(ig, pg *gset) (*Region, error) {
	keyLo, keyHi := maxi(ig.keyLo, pg.keyLo), mini(ig.keyHi, pg.keyHi)
	velLo, velHi := maxi(ig.velLo, pg.velLo), mini(ig.velHi, pg.velHi)
	if keyLo > keyHi || velLo > velHi {
		return nil, nil
	}
	if ig.sample < 0 || ig.sample >= len(s.shdr)-1 {
		return nil, nil
	}
	h := s.shdr[ig.sample]
	if h.typ&0x8000 != 0 { // échantillon en ROM : inutilisable
		return nil, nil
	}
	data, err := s.sampleData(ig.sample)
	if err != nil {
		return nil, nil // échantillon illisible : on ignore cette région
	}
	// valeur finale d'un générateur = instrument + preset (si additif)
	val := func(op int) int {
		v := ig.v[op]
		if !instOnly(op) {
			v += pg.v[op]
		}
		return v
	}
	r := &Region{KeyLo: keyLo, KeyHi: keyHi, VelLo: velLo, VelHi: velHi, Data: data}
	r.Rate = float64(h.rate)
	if r.Rate <= 0 {
		r.Rate = 44100
	}
	root := val(58)
	if root < 0 || root > 127 {
		root = int(h.pitch)
		if root > 127 {
			root = 60
		}
	}
	r.Root = root
	r.Tune = float64(val(51)*100+val(52)) + float64(h.correction)
	r.Scale = float64(val(56))
	// zone jouable et boucle (indices relatifs au début de l'échantillon)
	off := func(fine, coarse int) int { return fine + coarse*32768 }
	r.Start = maxi(0, off(val(0), val(4)))
	sampLen := int(h.end - h.start)
	if s.sf3 {
		sampLen = len(data) - 46
	}
	r.End = sampLen + off(val(1), val(12))
	if r.End > len(data) {
		r.End = len(data)
	}
	if r.End <= r.Start {
		return nil, nil
	}
	ls0, le0 := int(h.loopStart)-int(h.start), int(h.loopEnd)-int(h.start)
	if s.sf3 { // SF3 : les points de boucle sont déjà relatifs au début de l'échantillon
		ls0, le0 = int(h.loopStart), int(h.loopEnd)
	}
	r.LoopStart = ls0 + off(val(2), val(45))
	r.LoopEnd = le0 + off(val(3), val(50))
	mode := val(54) & 3
	if mode == 1 || mode == 3 {
		if r.LoopStart < 0 || r.LoopEnd > len(data) || r.LoopEnd-r.LoopStart < 2 {
			mode = 0
		}
	} else {
		mode = 0
	}
	r.LoopMode = mode
	r.AttenDB = float64(val(48)) / 10 * 0.4
	if r.AttenDB < 0 {
		r.AttenDB = 0
	}
	pan := float64(val(17)) / 500
	if val(17) == 0 {
		switch h.typ & 0xF {
		case 4: // gauche
			pan = -1
		case 2: // droite
			pan = 1
		}
	}
	r.Pan = math.Max(-1, math.Min(1, pan))
	r.Delay = tc2s(val(33))
	r.Attack = tc2s(val(34))
	r.Hold = tc2s(val(35))
	r.Decay = tc2s(val(36))
	sus := float64(val(37)) / 10
	if sus < 0 {
		sus = 0
	}
	if sus > 100 {
		sus = 100
	}
	r.SusDB = sus
	r.Release = tc2s(val(38))
	r.KeyHold = float64(val(39))
	r.KeyDecay = float64(val(40))
	r.FcHz = absCents2Hz(val(8))
	r.QdB = float64(val(9)) / 10
	if r.QdB < 0 {
		r.QdB = 0
	}
	r.VibCents = float64(val(6))
	r.VibRate = absCents2Hz(val(24))
	r.VibDelay = tc2s(val(23))
	r.Excl = ig.v[57]
	return r, nil
}
