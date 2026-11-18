// Package africanloops est le moteur d'African Loops Pro : chargement des
// loops WAV / MP3, détection du tempo, reconnaissance des percussions,
// séparation en pistes, bibliothèque classée et lecture en boucle synchronisée.
package africanloops

import (
	"encoding/binary"
	"errors"
	"io"
	"math"
	"os"
	"path/filepath"
	"strings"

	"minipiano/mp3"
)

// MaxSeconds : durée maximale lue dans un fichier (une loop dépasse rarement 1 minute).
const MaxSeconds = 300

// Audio est un son stéréo en nombres flottants (-1..1).
type Audio struct {
	Rate int
	L, R []float32
	// Informations écrites dans le fichier par celui qui a fabriqué la loop
	// (bloc « acid » des WAV). 0 = absent.
	MetaBPM   float64
	MetaBeats int
}

// Frames renvoie le nombre d'échantillons par canal.
func (a *Audio) Frames() int { return len(a.L) }

// Seconds renvoie la durée en secondes.
func (a *Audio) Seconds() float64 { return float64(len(a.L)) / float64(a.Rate) }

// Mono renvoie la moyenne des deux canaux.
func (a *Audio) Mono() []float32 {
	m := make([]float32, len(a.L))
	for i := range m {
		m[i] = 0.5 * (a.L[i] + a.R[i])
	}
	return m
}

// IsAudioFile indique si le fichier est un WAV ou un MP3.
func IsAudioFile(path string) bool {
	switch strings.ToLower(filepath.Ext(path)) {
	case ".wav", ".wave", ".mp3":
		return true
	}
	return false
}

// Load charge un fichier WAV ou MP3.
func Load(path string) (*Audio, error) {
	switch strings.ToLower(filepath.Ext(path)) {
	case ".mp3":
		return loadMP3(path)
	case ".wav", ".wave":
		b, err := os.ReadFile(path)
		if err != nil {
			return nil, err
		}
		return ParseWAV(b)
	}
	return nil, errors.New("format non pris en charge (WAV ou MP3 uniquement)")
}

func loadMP3(path string) (a *Audio, err error) {
	defer func() { // un fichier abîmé ne doit jamais faire planter le logiciel
		if r := recover(); r != nil {
			a, err = nil, errors.New("ce fichier MP3 est illisible ou abîmé")
		}
	}()
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	d, err := mp3.NewDecoder(f)
	if err != nil {
		return nil, errors.New("ce fichier MP3 est illisible ou abîmé")
	}
	rate := d.SampleRate()
	if rate < 8000 || rate > 192000 {
		return nil, errors.New("MP3 invalide (fréquence d'échantillonnage)")
	}
	raw, rerr := io.ReadAll(io.LimitReader(d, int64(rate)*MaxSeconds*4))
	if rerr != nil && len(raw) < 4096 {
		return nil, errors.New("ce fichier MP3 est illisible ou abîmé")
	}
	n := len(raw) / 4
	a = &Audio{Rate: rate, L: make([]float32, n), R: make([]float32, n)}
	for i := 0; i < n; i++ {
		a.L[i] = float32(int16(binary.LittleEndian.Uint16(raw[4*i:]))) / 32768
		a.R[i] = float32(int16(binary.LittleEndian.Uint16(raw[4*i+2:]))) / 32768
	}
	return a, nil
}

// ParseWAV lit un WAV en mémoire (PCM 8/16/24/32 bits ou flottant 32/64 bits).
func ParseWAV(b []byte) (*Audio, error) {
	if len(b) < 12 || string(b[0:4]) != "RIFF" || string(b[8:12]) != "WAVE" {
		return nil, errors.New("ce fichier n'est pas un WAV valide")
	}
	var tag, ch, bits, rate int
	var data []byte
	haveFmt := false
	a := &Audio{}
	pos := 12
	for pos+8 <= len(b) {
		id := string(b[pos : pos+4])
		size := int(binary.LittleEndian.Uint32(b[pos+4:]))
		body := pos + 8
		end := body + size
		if size < 0 || end > len(b) || end < body {
			end = len(b) // fichier tronqué : on lit jusqu'au bout
		}
		chunk := b[body:end]
		switch id {
		case "fmt ":
			if len(chunk) < 16 {
				return nil, errors.New("WAV invalide (fmt)")
			}
			tag = int(binary.LittleEndian.Uint16(chunk[0:]))
			ch = int(binary.LittleEndian.Uint16(chunk[2:]))
			rate = int(binary.LittleEndian.Uint32(chunk[4:]))
			bits = int(binary.LittleEndian.Uint16(chunk[14:]))
			if tag == 0xFFFE && len(chunk) >= 26 {
				tag = int(binary.LittleEndian.Uint16(chunk[24:]))
			}
			haveFmt = true
		case "data":
			data = chunk
		case "acid":
			// bloc ACID : nombre de temps (offset 12) et tempo (offset 20)
			if len(chunk) >= 24 {
				beats := int(binary.LittleEndian.Uint32(chunk[12:]))
				t := float64(math.Float32frombits(binary.LittleEndian.Uint32(chunk[20:])))
				if t >= 40 && t <= 300 {
					a.MetaBPM = t
				}
				if beats > 0 && beats <= 1024 {
					a.MetaBeats = beats
				}
			}
		}
		pos = end + (size & 1)
	}
	if !haveFmt || data == nil {
		return nil, errors.New("WAV invalide (données manquantes)")
	}
	if ch < 1 || ch > 8 || rate < 1000 || rate > 384000 {
		return nil, errors.New("WAV invalide (format)")
	}
	bps := bits / 8
	if (tag != 1 && tag != 3) || bps < 1 || bps > 8 || (tag == 3 && bps != 4 && bps != 8) {
		return nil, errors.New("ce WAV est compressé : seuls les WAV PCM sont pris en charge")
	}
	frames := len(data) / (bps * ch)
	if maxF := rate * MaxSeconds; frames > maxF {
		frames = maxF
	}
	a.Rate = rate
	a.L = make([]float32, frames)
	a.R = make([]float32, frames)
	sample := func(s []byte) float32 {
		switch {
		case tag == 1 && bps == 1:
			return float32(int(s[0])-128) / 128
		case tag == 1 && bps == 2:
			return float32(int16(binary.LittleEndian.Uint16(s))) / 32768
		case tag == 1 && bps == 3:
			v := int32(uint32(s[0])<<8|uint32(s[1])<<16|uint32(s[2])<<24) >> 8
			return float32(v) / 8388608
		case tag == 1 && bps == 4:
			return float32(float64(int32(binary.LittleEndian.Uint32(s))) / 2147483648)
		case tag == 3 && bps == 4:
			return math.Float32frombits(binary.LittleEndian.Uint32(s))
		case tag == 3 && bps == 8:
			return float32(math.Float64frombits(binary.LittleEndian.Uint64(s)))
		}
		return 0
	}
	for i := 0; i < frames; i++ {
		base := i * bps * ch
		l := sample(data[base : base+bps])
		r := l
		if ch > 1 {
			r = sample(data[base+bps : base+2*bps])
		}
		if l != l { // NaN dans un fichier flottant abîmé
			l = 0
		}
		if r != r {
			r = 0
		}
		a.L[i], a.R[i] = l, r
	}
	return a, nil
}

// EncodeWAV fabrique un WAV stéréo 24 bits. Si bpm > 0, le tempo est écrit
// dans le fichier (bloc acid) pour que les autres logiciels le reconnaissent.
func EncodeWAV(a *Audio, bpm float64, beats int) []byte {
	n := len(a.L)
	dataSize := n * 6
	acidSize := 0
	if bpm > 0 {
		acidSize = 8 + 24
	}
	out := make([]byte, 0, 44+acidSize+dataSize)
	le32 := func(v uint32) { out = binary.LittleEndian.AppendUint32(out, v) }
	le16 := func(v uint16) { out = binary.LittleEndian.AppendUint16(out, v) }
	out = append(out, "RIFF"...)
	le32(uint32(4 + 24 + acidSize + 8 + dataSize))
	out = append(out, "WAVEfmt "...)
	le32(16)
	le16(1)
	le16(2)
	le32(uint32(a.Rate))
	le32(uint32(a.Rate * 6))
	le16(6)
	le16(24)
	if bpm > 0 {
		out = append(out, "acid"...)
		le32(24)
		le32(0)  // drapeaux : boucle
		le16(60) // note de base
		le16(0)
		le32(0)
		le32(uint32(beats))
		le16(4)
		le16(4)
		le32(math.Float32bits(float32(bpm)))
	}
	out = append(out, "data"...)
	le32(uint32(dataSize))
	put := func(f float32) {
		if f > 1 {
			f = 1
		} else if f < -1 {
			f = -1
		}
		v := int32(math.Round(float64(f) * 8388607))
		out = append(out, byte(v), byte(v>>8), byte(v>>16))
	}
	for i := 0; i < n; i++ {
		put(a.L[i])
		put(a.R[i])
	}
	return out
}
