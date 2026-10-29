package main

// Petit lecteur audio (WAV / MP3) : joue un fichier par-dessus les pistes, avec lecture, arrêt, position et volume.

import (
	"errors"
	"math"
	"path/filepath"
	"strings"
	"sync"
)

type Player struct {
	mu      sync.Mutex
	data    []float32 // stéréo entrelacé
	rate    float64
	pos     float64 // en images
	playing bool
	vol     float64
	Name    string
}

func newPlayer() *Player { return &Player{vol: 0.8} }

// Load décode le fichier (hors du thread audio) puis le met en place.
func (p *Player) Load(path string) error {
	ext := strings.ToLower(filepath.Ext(path))
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
			a = &pcmAudio{ch: ch, rate: rate, data: pcm}
		}
	default:
		return errors.New("le lecteur lit les fichiers WAV et MP3")
	}
	if err != nil {
		return err
	}
	if a.ch < 1 || a.rate < 1000 || len(a.data) < a.ch*8 {
		return errors.New("fichier audio vide ou invalide")
	}
	frames := len(a.data) / a.ch
	d := make([]float32, frames*2)
	for i := 0; i < frames; i++ {
		l := float32(a.data[i*a.ch]) / 32768
		r := l
		if a.ch > 1 {
			r = float32(a.data[i*a.ch+1]) / 32768
		}
		d[2*i], d[2*i+1] = l, r
	}
	p.mu.Lock()
	p.data, p.rate, p.pos, p.playing = d, float64(a.rate), 0, false
	p.Name = strings.TrimSuffix(filepath.Base(path), filepath.Ext(path))
	p.mu.Unlock()
	return nil
}

func (p *Player) Loaded() bool { p.mu.Lock(); defer p.mu.Unlock(); return len(p.data) > 0 }

func (p *Player) Play() {
	p.mu.Lock()
	if len(p.data) > 0 {
		if p.pos >= float64(len(p.data)/2-2) {
			p.pos = 0
		}
		p.playing = true
	}
	p.mu.Unlock()
}

func (p *Player) Stop() {
	p.mu.Lock()
	p.playing = false
	p.pos = 0
	p.mu.Unlock()
}

func (p *Player) Pause() { p.mu.Lock(); p.playing = false; p.mu.Unlock() }

func (p *Player) Playing() bool { p.mu.Lock(); defer p.mu.Unlock(); return p.playing }

func (p *Player) SetVol(v float64) { p.mu.Lock(); p.vol = clamp01(v); p.mu.Unlock() }
func (p *Player) Vol() float64     { p.mu.Lock(); defer p.mu.Unlock(); return p.vol }

// Position / durée en secondes
func (p *Player) Times() (cur, total float64) {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.rate <= 0 {
		return 0, 0
	}
	return p.pos / p.rate, float64(len(p.data)/2) / p.rate
}

func (p *Player) Seek(frac float64) {
	p.mu.Lock()
	if n := len(p.data) / 2; n > 0 {
		p.pos = clamp01(frac) * float64(n-2)
	}
	p.mu.Unlock()
}

// mix ajoute le son du fichier dans les deux tampons (appelé par le thread audio).
func (p *Player) mix(l, r []float64) {
	if !p.mu.TryLock() { // ne jamais bloquer le son : on saute ce bloc
		return
	}
	defer p.mu.Unlock()
	if !p.playing || len(p.data) == 0 {
		return
	}
	n := len(p.data) / 2
	inc := p.rate / SR
	g := float64(p.vol * p.vol)
	for i := range l {
		k := int(p.pos)
		if k >= n-1 {
			p.playing, p.pos = false, 0
			return
		}
		f := float32(p.pos - float64(k))
		a, b := p.data[2*k], p.data[2*k+2]
		c, d := p.data[2*k+1], p.data[2*k+3]
		l[i] += float64(a+(b-a)*f) * g
		r[i] += float64(c+(d-c)*f) * g
		p.pos += inc
	}
	_ = math.Pi
}
