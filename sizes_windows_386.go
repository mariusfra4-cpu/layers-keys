//go:build windows && 386

package main

import "unsafe"

// Vérifie à la compilation que nos structures ont exactement la taille attendue par Windows.
const (
	_ = uint(unsafe.Sizeof(wndClassEx{}) - 48)
	_ = uint(48 - unsafe.Sizeof(wndClassEx{}))
	_ = uint(unsafe.Sizeof(msgT{}) - 28)
	_ = uint(28 - unsafe.Sizeof(msgT{}))
	_ = uint(unsafe.Sizeof(paintStruct{}) - 64)
	_ = uint(64 - unsafe.Sizeof(paintStruct{}))
	_ = uint(unsafe.Sizeof(waveHdr{}) - 32)
	_ = uint(32 - unsafe.Sizeof(waveHdr{}))
	_ = uint(unsafe.Sizeof(openFileName{}) - 88)
	_ = uint(88 - unsafe.Sizeof(openFileName{}))
	_ = uint(unsafe.Sizeof(midiInCaps{}) - 76)
	_ = uint(76 - unsafe.Sizeof(midiInCaps{}))
	_ = uint(unsafe.Sizeof(bitmapInfoHeader{}) - 40)
	_ = uint(40 - unsafe.Sizeof(bitmapInfoHeader{}))
	_ = uint(unsafe.Sizeof(rectT{}) - 16)
	_ = uint(16 - unsafe.Sizeof(rectT{}))
)
