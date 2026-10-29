package main

// Chargement de fichiers audio (WAV, MP3) pour les jouer au clavier.

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

// durée maximale d'un fichier audio chargé en mémoire (au-delà, il est coupé)
const maxAudioSeconds = 600

// décodeur MP3 intégré au logiciel (aucune dépendance à Windows)
func decodeMP3(path string) (pcm []int16, channels, rate int, err error) {
	defer func() { // un fichier corrompu ne doit jamais faire planter le logiciel
		if r := recover(); r != nil {
			pcm, err = nil, errors.New("ce fichier MP3 est illisible ou corrompu")
		}
	}()
	f, err := os.Open(path)
	if err != nil {
		return nil, 0, 0, err
	}
	defer f.Close()
	d, err := mp3.NewDecoder(f)
	if err != nil {
		return nil, 0, 0, errors.New("ce fichier MP3 est illisible ou corrompu")
	}
	rate = d.SampleRate()
	if rate < 8000 || rate > 192000 {
		return nil, 0, 0, errors.New("MP3 invalide (fréquence d'échantillonnage)")
	}
	limit := int64(rate) * maxAudioSeconds * 4 // octets : stéréo, 16 bits
	raw, err := io.ReadAll(io.LimitReader(d, limit))
	if err != nil && len(raw) < 4096 {
		return nil, 0, 0, errors.New("ce fichier MP3 est illisible ou corrompu")
	}
	n := len(raw) / 2
	pcm = make([]int16, n)
	for i := range pcm {
		pcm[i] = int16(binary.LittleEndian.Uint16(raw[2*i:]))
	}
	return pcm, 2, rate, nil
}

type pcmAudio struct {
	ch, rate  int
	data      []int16 // entrelacé
	root      int
	loopStart int // en images (frames)
	loopEnd   int
	hasLoop   bool
}

// LoadAudioFile charge un fichier WAV ou MP3 et le transforme en son jouable.
func LoadAudioFile(path string) (*sampleSource, string, error) {
	ext := strings.ToLower(filepath.Ext(path))
	base := strings.TrimSuffix(filepath.Base(path), filepath.Ext(path))
	var a *pcmAudio
	var err error
	switch ext {
	case ".wav", ".wave":
		a, err = readWAV(path)
	case ".mp3":
		var pcm []int16
		var ch, rate int
		pcm, ch, rate, err = decodeMP3(path)
		if err == nil {
			a = &pcmAudio{ch: ch, rate: rate, data: pcm, root: 60}
		}
	default:
		return nil, "", errors.New("format non pris en charge (utilise un fichier SF2, WAV ou MP3)")
	}
	if err != nil {
		return nil, "", err
	}
	if a.ch < 1 || a.rate < 1000 || len(a.data) < a.ch*8 {
		return nil, "", errors.New("fichier audio vide ou invalide")
	}
	return &sampleSource{regions: a.regions()}, base, nil
}

func (a *pcmAudio) regions() []*Region {
	frames := len(a.data) / a.ch
	chans := a.ch
	if chans > 2 {
		chans = 2
	}
	var out []*Region
	for c := 0; c < chans; c++ {
		d := make([]int16, frames+8)
		for i := 0; i < frames; i++ {
			d[i] = a.data[i*a.ch+c]
		}
		r := newPlainRegion(d, float64(a.rate), a.root)
		r.End = frames
		r.Release = 0.12
		if chans == 2 {
			r.Pan = float64(2*c - 1)
		}
		if a.hasLoop && a.loopEnd > a.loopStart+1 && a.loopEnd <= frames {
			r.LoopMode, r.LoopStart, r.LoopEnd = 1, a.loopStart, a.loopEnd
		}
		out = append(out, r)
	}
	return out
}

func readWAV(path string) (*pcmAudio, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	if len(b) < 12 || string(b[0:4]) != "RIFF" || string(b[8:12]) != "WAVE" {
		return nil, errors.New("ce fichier n'est pas un WAV valide")
	}
	var tag, ch, bits int
	var rate int
	var data []byte
	haveFmt := false
	a := &pcmAudio{root: 60}
	pos := 12
	for pos+8 <= len(b) {
		id := string(b[pos : pos+4])
		size := int(binary.LittleEndian.Uint32(b[pos+4:]))
		body := pos + 8
		end := body + size
		if size < 0 || end > len(b) || end < body {
			end = len(b) // fichier tronqué ou taille inconnue : on lit jusqu'au bout
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
		case "smpl":
			if len(chunk) >= 36 {
				note := int(binary.LittleEndian.Uint32(chunk[12:]))
				if note >= 0 && note <= 127 {
					a.root = note
				}
				nl := int(binary.LittleEndian.Uint32(chunk[28:]))
				if nl > 0 && len(chunk) >= 36+24 {
					a.loopStart = int(binary.LittleEndian.Uint32(chunk[44:]))
					a.loopEnd = int(binary.LittleEndian.Uint32(chunk[48:])) + 1
					a.hasLoop = true
				}
			}
		}
		pos = end + (size & 1)
	}
	if !haveFmt || data == nil {
		return nil, errors.New("WAV invalide (données manquantes)")
	}
	if ch < 1 || ch > 8 || rate < 1000 {
		return nil, errors.New("WAV invalide (format)")
	}
	bps := bits / 8
	if (tag != 1 && tag != 3) || bps < 1 || bps > 8 {
		return nil, errors.New("ce WAV est compressé : seuls les WAV PCM sont pris en charge")
	}
	frames := len(data) / (bps * ch)
	if maxF := rate * maxAudioSeconds; frames > maxF {
		frames = maxF
	}
	out := make([]int16, frames*ch)
	clip := func(f float64) int16 {
		if f > 1 {
			f = 1
		} else if f < -1 {
			f = -1
		}
		return int16(f * 32767)
	}
	for i := 0; i < frames*ch; i++ {
		s := data[i*bps : (i+1)*bps]
		switch {
		case tag == 1 && bps == 1:
			out[i] = int16(int(s[0])-128) << 8
		case tag == 1 && bps == 2:
			out[i] = int16(binary.LittleEndian.Uint16(s))
		case tag == 1 && bps == 3:
			out[i] = int16(uint16(s[1]) | uint16(s[2])<<8)
		case tag == 1 && bps == 4:
			out[i] = int16(binary.LittleEndian.Uint16(s[2:]))
		case tag == 3 && bps == 4:
			out[i] = clip(float64(math.Float32frombits(binary.LittleEndian.Uint32(s))))
		case tag == 3 && bps == 8:
			out[i] = clip(math.Float64frombits(binary.LittleEndian.Uint64(s)))
		default:
			return nil, errors.New("format WAV non pris en charge")
		}
	}
	a.ch, a.rate, a.data = ch, rate, out
	return a, nil
}
