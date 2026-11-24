package main

// Écriture des fichiers WAV (16/24 bits) et MP3 (LAME via ffmpeg si présent,
// sinon encodeur intégré « shine » à 320 kbit/s).

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"math"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"

	"sty2audio/internal/shine"
)

// Peak : crête absolue.
func (a *Audio) Peak() float64 {
	pk := 0.0
	for i := range a.L {
		pk = math.Max(pk, math.Max(math.Abs(float64(a.L[i])), math.Abs(float64(a.R[i]))))
	}
	return pk
}

// pcm : conversion en entiers avec dither triangulaire (TPDF).
func (a *Audio) pcm16(gain float64) []int16 {
	out := make([]int16, 2*len(a.L))
	rng := rand.New(rand.NewSource(1))
	conv := func(x float32) int16 {
		if x == 0 {
			return 0 // silence numérique : pas de bruit de dither
		}
		v := float64(x)*gain*32767 + (rng.Float64() - rng.Float64())
		v = math.Round(v)
		if v > 32767 {
			v = 32767
		} else if v < -32768 {
			v = -32768
		}
		return int16(v)
	}
	for i := range a.L {
		out[2*i] = conv(a.L[i])
		out[2*i+1] = conv(a.R[i])
	}
	return out
}

func wavHeader(w io.Writer, rate, bits, frames int) {
	ba := 2 * bits / 8
	size := frames * ba
	h := make([]byte, 44)
	copy(h[0:], "RIFF")
	binary.LittleEndian.PutUint32(h[4:], uint32(36+size))
	copy(h[8:], "WAVEfmt ")
	binary.LittleEndian.PutUint32(h[16:], 16)
	binary.LittleEndian.PutUint16(h[20:], 1)
	binary.LittleEndian.PutUint16(h[22:], 2)
	binary.LittleEndian.PutUint32(h[24:], uint32(rate))
	binary.LittleEndian.PutUint32(h[28:], uint32(rate*ba))
	binary.LittleEndian.PutUint16(h[32:], uint16(ba))
	binary.LittleEndian.PutUint16(h[34:], uint16(bits))
	copy(h[36:], "data")
	binary.LittleEndian.PutUint32(h[40:], uint32(size))
	w.Write(h)
}

func WriteWAV(path string, a *Audio, rate, bits int, gain float64) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	w := bufio.NewWriterSize(f, 1<<20)
	wavHeader(w, rate, bits, a.Len())
	if bits == 24 {
		rng := rand.New(rand.NewSource(1))
		var b [3]byte
		put := func(x float32) {
			if x == 0 {
				w.Write([]byte{0, 0, 0})
				return
			}
			v := math.Round(float64(x)*gain*8388607 + (rng.Float64()-rng.Float64())*0.5)
			v = math.Max(-8388608, math.Min(8388607, v))
			iv := int32(v)
			b[0], b[1], b[2] = byte(iv), byte(iv>>8), byte(iv>>16)
			w.Write(b[:])
		}
		for i := range a.L {
			put(a.L[i])
			put(a.R[i])
		}
	} else {
		pcm := a.pcm16(gain)
		buf := make([]byte, 2*len(pcm))
		for i, v := range pcm {
			binary.LittleEndian.PutUint16(buf[2*i:], uint16(v))
		}
		w.Write(buf)
	}
	if err := w.Flush(); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}

// findFFmpeg : ffmpeg à côté du programme, sinon dans le PATH.
func findFFmpeg() string {
	name := "ffmpeg"
	if runtime.GOOS == "windows" {
		name = "ffmpeg.exe"
	}
	if exe, err := os.Executable(); err == nil {
		p := filepath.Join(filepath.Dir(exe), name)
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	if p, err := exec.LookPath("ffmpeg"); err == nil {
		// vérifie la présence de LAME
		out, err := exec.Command(p, "-hide_banner", "-encoders").Output()
		if err == nil && bytes.Contains(out, []byte("libmp3lame")) {
			return p
		}
	}
	return ""
}

func WriteMP3(path string, a *Audio, rate, kbps int, gain float64, ffmpeg string, title, artist string) error {
	pcm := a.pcm16(gain)
	if ffmpeg != "" {
		var in bytes.Buffer
		wavHeader(&in, rate, 16, a.Len())
		raw := make([]byte, 2*len(pcm))
		for i, v := range pcm {
			binary.LittleEndian.PutUint16(raw[2*i:], uint16(v))
		}
		in.Write(raw)
		cmd := exec.Command(ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "wav", "-i", "-",
			"-c:a", "libmp3lame", "-b:a", fmt.Sprintf("%dk", kbps), "-q:a", "0",
			"-metadata", "title="+title, "-metadata", "artist="+artist, path)
		cmd.Stdin = &in
		var errb bytes.Buffer
		cmd.Stderr = &errb
		hideWindow(cmd)
		if err := cmd.Run(); err == nil {
			return nil
		}
		// échec : on retombe sur l'encodeur intégré
	}
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	w := bufio.NewWriterSize(f, 1<<20)
	writeID3(w, title, artist)
	enc := shine.NewEncoderBitrate(rate, 2, kbps)
	spp := 1152 * 2
	chunk := make([]int16, spp)
	for i := 0; i < len(pcm); i += spp {
		n := copy(chunk, pcm[i:])
		for k := n; k < spp; k++ {
			chunk[k] = 0
		}
		data, written := enc.EncodeBufferInterleaved(chunk)
		w.Write(data[:written])
	}
	if data, n := enc.Flush(); n > 0 {
		w.Write(data[:n])
	}
	if err := w.Flush(); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}

// writeID3 : balise ID3v2.4 minimale (titre, artiste).
func writeID3(w io.Writer, title, artist string) {
	var body bytes.Buffer
	frame := func(id, text string) {
		t := append([]byte{3}, []byte(text)...) // UTF-8
		var h [10]byte
		copy(h[:], id)
		m := len(t)
		h[4], h[5], h[6], h[7] = byte(m>>21&0x7F), byte(m>>14&0x7F), byte(m>>7&0x7F), byte(m&0x7F)
		body.Write(h[:])
		body.Write(t)
	}
	frame("TIT2", title)
	frame("TPE1", artist)
	frame("TSSE", "sty2audio")
	n := body.Len()
	hdr := []byte{'I', 'D', '3', 4, 0, 0,
		byte(n >> 21 & 0x7F), byte(n >> 14 & 0x7F), byte(n >> 7 & 0x7F), byte(n & 0x7F)}
	w.Write(hdr)
	w.Write(body.Bytes())
}
