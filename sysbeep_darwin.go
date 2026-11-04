//go:build darwin && !vst3 && !au

package main

import (
	"context"
	"encoding/binary"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"time"
)

func init() { sysBeep = macSystemBeep }

// bip joué par le système lui-même (afplay), sans passer par le moteur ni par la sortie audio du logiciel :
// si on l'entend mais pas le bip du logiciel, le défaut est dans ma sortie audio ; si on ne l'entend pas, il est dans la configuration du Mac.
func macSystemBeep() {
	const rate = 48000
	n := rate
	b := make([]byte, 44+2*n)
	copy(b[0:], "RIFF")
	binary.LittleEndian.PutUint32(b[4:], uint32(36+2*n))
	copy(b[8:], "WAVEfmt ")
	binary.LittleEndian.PutUint32(b[16:], 16)
	binary.LittleEndian.PutUint16(b[20:], 1)
	binary.LittleEndian.PutUint16(b[22:], 1)
	binary.LittleEndian.PutUint32(b[24:], rate)
	binary.LittleEndian.PutUint32(b[28:], rate*2)
	binary.LittleEndian.PutUint16(b[32:], 2)
	binary.LittleEndian.PutUint16(b[34:], 16)
	copy(b[36:], "data")
	binary.LittleEndian.PutUint32(b[40:], uint32(2*n))
	for i := 0; i < n; i++ {
		v := int16(0.3 * 32767 * math.Sin(2*math.Pi*440*float64(i)/rate))
		binary.LittleEndian.PutUint16(b[44+2*i:], uint16(v))
	}
	p := filepath.Join(os.TempDir(), "layerskeys_bip.wav")
	if err := os.WriteFile(p, b, 0o644); err != nil {
		lklog("bip système : écriture impossible : %v", err)
		return
	}
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		err := exec.CommandContext(ctx, "/usr/bin/afplay", p).Run()
		lklog("bip système (afplay) terminé : erreur=%v", err)
	}()
}
