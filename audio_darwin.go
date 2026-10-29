//go:build darwin && !vst3 && !au

package main

// Sortie audio du Mac : CoreAudio (AudioQueue) via la bibliothèque oto, sans compilateur C.

import (
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/ebitengine/oto/v3"
)

func init() {
	defaultAPI = apiCoreAudio
	defaultDeviceName = "celle du Mac"
}

func audioAPIs() []string              { return []string{apiCoreAudio} }
func audioDevices(api string) []string { return nil } // le Mac utilise la sortie choisie dans Réglages Système > Son

// Le système audio ne permet de créer qu'un seul contexte par exécution : on le garde pour toute la session.
var (
	macAudioMu  sync.Mutex
	macCtx      *oto.Context
	macCtxRate  int
	macCtxFrame int
)

// lit le moteur directement : il calcule le son au moment où la carte son en a besoin
type engReader struct {
	e   *Engine
	buf []int16
}

func (r *engReader) Read(p []byte) (int, error) {
	frames := len(p) / 4
	if frames > 512 {
		frames = 512
	}
	if frames == 0 {
		return 0, nil
	}
	if cap(r.buf) < frames*2 {
		r.buf = make([]int16, 1024)
	}
	b := r.buf[:frames*2]
	r.e.Render(b)
	for i, v := range b {
		p[2*i] = byte(v)
		p[2*i+1] = byte(uint16(v) >> 8)
	}
	return frames * 4, nil
}

type macAudio struct {
	player *oto.Player
	info   string
}

func (m *macAudio) Close() {
	if m.player != nil {
		m.player.Pause()
		m.player.Close()
		m.player = nil
	}
}
func (m *macAudio) Info() string { return m.info }
func (m *macAudio) Panel()       {}

func openAudio(e *Engine, cfg AudioCfg) (audioBackend, string, error) {
	macAudioMu.Lock()
	defer macAudioMu.Unlock()
	rate := cfg.Rate
	if rate == 0 {
		rate = 48000
	}
	frames := cfg.Buffer
	if frames == 0 {
		frames = 512
	}
	frames = clamp(frames, 256, 8192)
	note := ""
	if macCtx == nil {
		ctx, ready, err := oto.NewContext(&oto.NewContextOptions{
			SampleRate:   rate,
			ChannelCount: 2,
			Format:       oto.FormatSignedInt16LE,
			BufferSize:   time.Duration(frames) * time.Second / time.Duration(rate),
		})
		if err != nil {
			return nil, "", err
		}
		select {
		case <-ready:
		case <-time.After(8 * time.Second):
			return nil, "", errors.New("la sortie audio du Mac ne répond pas")
		}
		if err := ctx.Err(); err != nil {
			return nil, "", err
		}
		macCtx, macCtxRate, macCtxFrame = ctx, rate, frames
	} else if rate != macCtxRate || frames != macCtxFrame {
		note = " · fréquence/tampon : relance le logiciel pour les changer"
	}
	e.SetSampleRate(macCtxRate)
	p := macCtx.NewPlayer(&engReader{e: e})
	p.SetBufferSize(macCtxFrame * 4)
	p.Play()
	info := fmt.Sprintf("CoreAudio · sortie du Mac · %d Hz · ≈ %.0f ms%s", macCtxRate,
		2*float64(macCtxFrame)/float64(macCtxRate)*1000, note)
	return &macAudio{player: p, info: info}, info, nil
}
