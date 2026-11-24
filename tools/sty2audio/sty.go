package main

// Lecture des fichiers de style Yamaha (.sty, .prs, .bcs, .sst, .fps…).
// Un style est un fichier MIDI standard (MThd + MTrk) suivi de blocs propres
// à Yamaha (CASM, OTSc, MDB, MHhd…). Les sections (Intro A, Main A, Fill In AA,
// Ending A…) sont délimitées par des marqueurs dans la piste MIDI.

import (
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"sort"
	"strings"
)

// Event est un événement MIDI à une position absolue (en tics).
type Event struct {
	Tick   int64
	Status byte   // 0x80..0xEF pour les messages canal, 0xF0 SysEx, 0xFF méta
	Data1  byte   // note / contrôleur / programme
	Data2  byte   // vélocité / valeur
	Meta   byte   // type de méta-événement
	Bytes  []byte // contenu SysEx ou méta
	order  int    // ordre d'origine (tri stable)
}

func (e *Event) Channel() int { return int(e.Status & 0x0F) }
func (e *Event) Kind() byte   { return e.Status & 0xF0 }

// Section : une partie du style, de Start (inclus) à End (exclu).
type Section struct {
	Name       string
	Start, End int64
}

// CtabInfo : réglages d'une piste source dans le bloc CASM.
type CtabInfo struct {
	Src, Dest    int
	Name         string
	NTT          int // 0 = Bypass (jamais transposé)
	RootNote     int // tonalité source (0 = Do)
	LoLim, HiLim int // limites de notes
	Drum         bool
}

// CSeg : sections concernées + tables de routage.
type CSeg struct {
	Sections []string
	Tabs     []CtabInfo
}

// Style : contenu complet du fichier.
type Style struct {
	Path     string
	Name     string
	PPQN     int
	Events   []Event // tous les événements, triés par tic
	Sections []Section
	Tempos   []TempoPoint
	CASM     []CSeg
	TimeSig  [2]int
}

type TempoPoint struct {
	Tick  int64
	USPQN float64 // microsecondes par noire
}

func be32(b []byte) uint32 { return binary.BigEndian.Uint32(b) }
func be16(b []byte) uint16 { return binary.BigEndian.Uint16(b) }

// LoadStyle lit un fichier de style.
func LoadStyle(path string) (*Style, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return ParseStyle(b, path)
}

func ParseStyle(b []byte, path string) (*Style, error) {
	// Certains fichiers ont quelques octets avant MThd : on le cherche.
	off := strings.Index(string(b[:min(len(b), 4096)]), "MThd")
	if off < 0 {
		return nil, errors.New("ce fichier n'est pas un style Yamaha (en-tête MIDI « MThd » introuvable)")
	}
	b = b[off:]
	st := &Style{Path: path, PPQN: 480, TimeSig: [2]int{4, 4}}
	order := 0
	p := 0
	gotMIDI := false
	for p+8 <= len(b) {
		id := string(b[p : p+4])
		n := int(be32(b[p+4 : p+8]))
		p += 8
		if n < 0 || p+n > len(b) {
			n = len(b) - p // bloc tronqué : on lit ce qu'il reste
		}
		data := b[p : p+n]
		switch id {
		case "MThd":
			if n >= 6 {
				div := int(be16(data[4:6]))
				if div&0x8000 != 0 {
					return nil, errors.New("division temporelle SMPTE non prise en charge")
				}
				if div > 0 {
					st.PPQN = div
				}
			}
		case "MTrk":
			evs, err := parseTrack(data, &order)
			if err != nil && len(evs) == 0 {
				return nil, fmt.Errorf("piste MIDI illisible : %v", err)
			}
			st.Events = append(st.Events, evs...)
			gotMIDI = true
		case "CASM":
			st.CASM = parseCASM(data)
		}
		p += n
	}
	if !gotMIDI {
		return nil, errors.New("aucune piste MIDI dans ce fichier")
	}
	sort.SliceStable(st.Events, func(i, j int) bool {
		a, c := &st.Events[i], &st.Events[j]
		if a.Tick != c.Tick {
			return a.Tick < c.Tick
		}
		return a.order < c.order
	})
	st.analyse()
	return st, nil
}

func readVLQ(b []byte, p *int) (int64, error) {
	var v int64
	for i := 0; i < 4; i++ {
		if *p >= len(b) {
			return 0, errors.New("fin de piste inattendue")
		}
		c := b[*p]
		*p++
		v = v<<7 | int64(c&0x7F)
		if c&0x80 == 0 {
			return v, nil
		}
	}
	return v, errors.New("longueur variable invalide")
}

func parseTrack(b []byte, order *int) ([]Event, error) {
	var evs []Event
	var tick int64
	var running byte
	p := 0
	for p < len(b) {
		d, err := readVLQ(b, &p)
		if err != nil {
			return evs, err
		}
		tick += d
		if p >= len(b) {
			break
		}
		s := b[p]
		if s < 0x80 {
			if running == 0 {
				return evs, errors.New("statut MIDI manquant")
			}
			s = running
		} else {
			p++
		}
		ev := Event{Tick: tick, Status: s, order: *order}
		*order++
		switch {
		case s == 0xFF:
			if p >= len(b) {
				return evs, nil
			}
			ev.Meta = b[p]
			p++
			n, err := readVLQ(b, &p)
			if err != nil || p+int(n) > len(b) {
				return evs, errors.New("méta-événement tronqué")
			}
			ev.Bytes = b[p : p+int(n)]
			p += int(n)
			if ev.Meta == 0x2F {
				return append(evs, ev), nil
			}
		case s == 0xF0 || s == 0xF7:
			n, err := readVLQ(b, &p)
			if err != nil || p+int(n) > len(b) {
				return evs, errors.New("SysEx tronqué")
			}
			ev.Bytes = b[p : p+int(n)]
			p += int(n)
		case s >= 0xF0:
			// messages système temps réel / communs : ignorés
			continue
		default:
			running = s
			need := 2
			if s&0xF0 == 0xC0 || s&0xF0 == 0xD0 {
				need = 1
			}
			if p+need > len(b) {
				return evs, errors.New("message MIDI tronqué")
			}
			ev.Data1 = b[p] & 0x7F
			if need == 2 {
				ev.Data2 = b[p+1] & 0x7F
			}
			p += need
		}
		evs = append(evs, ev)
	}
	return evs, nil
}

// analyse : nom, tempos, mesure et sections.
func (st *Style) analyse() {
	type mark struct {
		name string
		tick int64
	}
	var marks []mark
	var last int64
	for i := range st.Events {
		e := &st.Events[i]
		if e.Tick > last {
			last = e.Tick
		}
		if e.Status != 0xFF {
			continue
		}
		switch e.Meta {
		case 0x06: // marqueur
			marks = append(marks, mark{strings.TrimSpace(string(e.Bytes)), e.Tick})
		case 0x03: // nom de séquence
			if st.Name == "" {
				st.Name = strings.TrimSpace(strings.TrimRight(string(e.Bytes), "\x00"))
			}
		case 0x51:
			if len(e.Bytes) >= 3 {
				us := float64(int(e.Bytes[0])<<16 | int(e.Bytes[1])<<8 | int(e.Bytes[2]))
				if us > 0 {
					st.Tempos = append(st.Tempos, TempoPoint{e.Tick, us})
				}
			}
		case 0x58:
			if len(e.Bytes) >= 2 && st.TimeSig == [2]int{4, 4} {
				st.TimeSig = [2]int{int(e.Bytes[0]), 1 << e.Bytes[1]}
			}
		}
	}
	if len(st.Tempos) == 0 || st.Tempos[0].Tick > 0 {
		st.Tempos = append([]TempoPoint{{0, 500000}}, st.Tempos...)
	}
	// Sections : chaque marqueur (hors SFF1/SFF2/SInt) court jusqu'au suivant.
	for i, m := range marks {
		end := last
		for j := i + 1; j < len(marks); j++ {
			if marks[j].tick > m.tick {
				end = marks[j].tick
				break
			}
		}
		up := strings.ToUpper(m.name)
		if up == "SFF1" || up == "SFF2" || up == "SINT" || m.name == "" || end <= m.tick {
			continue
		}
		st.Sections = append(st.Sections, Section{m.name, m.tick, end})
	}
	if len(st.Sections) == 0 {
		// Fichier MIDI sans marqueurs : un seul morceau.
		st.Sections = []Section{{"Morceau", 0, last}}
	}
}

// BPM : tempo initial du style.
func (st *Style) BPM() float64 { return 60e6 / st.Tempos[0].USPQN }

// ---------------------------------------------------------------- CASM

func parseCASM(b []byte) []CSeg {
	var out []CSeg
	walkChunks(b, func(id string, d []byte) {
		if id != "CSEG" {
			return
		}
		var seg CSeg
		walkChunks(d, func(id string, x []byte) {
			switch id {
			case "Sdec":
				for _, s := range strings.Split(string(x), ",") {
					if s = strings.TrimSpace(s); s != "" {
						seg.Sections = append(seg.Sections, s)
					}
				}
			case "Ctab", "Ctb2":
				if len(x) < 26 {
					return
				}
				c := CtabInfo{
					Src:      int(x[0]) & 0x0F,
					Name:     strings.TrimSpace(strings.TrimRight(string(x[1:9]), "\x00")),
					Dest:     int(x[9]) & 0x0F,
					RootNote: int(x[18]) & 0x0F,
				}
				if id == "Ctab" {
					c.NTT = int(x[21])
					c.LoLim, c.HiLim = int(x[23]), int(x[24])
				} else if len(x) >= 34 {
					c.NTT = int(x[29])
					c.LoLim, c.HiLim = int(x[31]), int(x[32])
				}
				if c.HiLim == 0 || c.HiLim < c.LoLim {
					c.LoLim, c.HiLim = 0, 127
				}
				c.Drum = c.Dest == 8 || c.Dest == 9
				if c.RootNote > 11 {
					c.RootNote = 0
				}
				seg.Tabs = append(seg.Tabs, c)
			}
		})
		out = append(out, seg)
	})
	return out
}

func walkChunks(b []byte, f func(id string, d []byte)) {
	p := 0
	for p+8 <= len(b) {
		id := string(b[p : p+4])
		n := int(be32(b[p+4 : p+8]))
		p += 8
		if n < 0 || p+n > len(b) {
			n = len(b) - p
		}
		f(id, b[p:p+n])
		p += n
	}
}

// Routing : table source → réglages pour une section donnée.
func (st *Style) Routing(section string) map[int]CtabInfo {
	m := map[int]CtabInfo{}
	for _, seg := range st.CASM {
		ok := false
		for _, s := range seg.Sections {
			if strings.EqualFold(s, section) {
				ok = true
				break
			}
		}
		if !ok {
			continue
		}
		for _, t := range seg.Tabs {
			if _, dup := m[t.Src]; !dup {
				m[t.Src] = t
			}
		}
	}
	return m
}
