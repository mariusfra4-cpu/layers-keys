//go:build android

package main

/*
#cgo LDFLAGS: -laaudio -ljnigraphics -llog
#include <stdint.h>
#include <stdlib.h>
*/
import "C"

// Fonctions appelées par la colle en C (droid_glue.c), elle-même appelée par Java.

import (
	"runtime/debug"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"unsafe"
)

func gs(p *C.char) string { return C.GoString(p) }

func recoverDroid() { _ = recover() }

//export lkaInit
func lkaInit(filesDir, id *C.char, rate C.int) {
	defer recoverDroid()
	if theDroid != nil {
		return
	}
	debug.SetGCPercent(400)
	theDroid = newDroid(gs(filesDir), gs(id), int(rate))
}

//export lkaSetRate
func lkaSetRate(rate C.int) {
	defer recoverDroid()
	if d := theDroid; d != nil && int(rate) >= 8000 && int(rate) != d.eng.Rate() {
		d.eng.SetSampleRate(int(rate))
	}
}

//export lkaFrame
func lkaFrame(dst *C.uint32_t, w, h, stride C.int) C.int {
	defer recoverDroid()
	d := theDroid
	if d == nil || w <= 0 || h <= 0 || stride < w {
		return 0
	}
	buf := unsafe.Slice((*uint32)(unsafe.Pointer(dst)), int(stride)*int(h))
	return C.int(d.Frame(buf, int(w), int(h), int(stride)))
}

//export lkaSize
func lkaSize() C.int {
	defer recoverDroid()
	if theDroid == nil {
		return C.int(WinW<<16 | WinH)
	}
	w, h := theDroid.Size()
	return C.int(w<<16 | h)
}

//export lkaLayout
func lkaLayout(out *C.float) {
	defer recoverDroid()
	if theDroid == nil {
		return
	}
	l, t, s := theDroid.Layout()
	o := unsafe.Slice((*float32)(unsafe.Pointer(out)), 3)
	o[0], o[1], o[2] = float32(l), float32(t), float32(s)
}

//export lkaSetView
func lkaSetView(w, h C.int) {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.SetView(int(w), int(h))
	}
}

//export lkaTouch
func lkaTouch(action, id C.int, x, y C.float) {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.Touch(int(action), int(id), float64(x), float64(y))
	}
}

//export lkaKey
func lkaKey(vk, down C.int) {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.Key(int(vk), down != 0)
	}
}

//export lkaChar
func lkaChar(cp C.int) {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.Char(rune(cp))
	}
}

//export lkaBack
func lkaBack() {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.Back()
	}
}

var (
	midiMu      sync.Mutex
	midiRunning byte
)

//export lkaMidi
func lkaMidi(p *C.uint8_t, n C.int) {
	defer recoverDroid()
	if theDroid == nil || n <= 0 {
		return
	}
	b := C.GoBytes(unsafe.Pointer(p), n)
	midiMu.Lock()
	theDroid.Midi(b, &midiRunning)
	midiMu.Unlock()
}

//export lkaMidiStatus
func lkaMidiStatus(ok C.int, s *C.char) {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.MidiStatus(ok != 0, gs(s))
	}
}

// demande suivante pour Java : chaîne à libérer avec free(), ou NULL
//
//export lkaNextReq
func lkaNextReq() *C.char {
	defer recoverDroid()
	if theDroid == nil {
		return nil
	}
	r := theDroid.NextReq()
	if r == "" {
		return nil
	}
	// Java n'accepte que les caractères du plan de base
	r = strings.Map(func(c rune) rune {
		if c > 0xFFFF {
			return '?'
		}
		return c
	}, r)
	return C.CString(r)
}

//export lkaFileResult
func lkaFileResult(p *C.char) {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.FileResult(gs(p))
	}
}

//export lkaSetClip
func lkaSetClip(p *C.char) {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.SetClip(gs(p))
	}
}

//export lkaRender
func lkaRender(out *C.float, frames C.int) {
	defer recoverDroid()
	d := theDroid
	n := int(frames)
	if d == nil || n <= 0 {
		if n > 0 {
			z := unsafe.Slice((*float32)(unsafe.Pointer(out)), 2*n)
			for i := range z {
				z[i] = 0
			}
		}
		return
	}
	d.Render(unsafe.Slice((*float32)(unsafe.Pointer(out)), 2*n), n)
}

//export lkaResume
func lkaResume() {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.Resume()
	}
}

//export lkaStop
func lkaStop() {
	defer recoverDroid()
	if theDroid != nil {
		theDroid.Stop()
	}
}

// état pour les journaux : appels audio, images sonores, messages MIDI, notes jouées
//
//export lkaStatus
func lkaStatus() *C.char {
	defer recoverDroid()
	d := theDroid
	if d == nil {
		return C.CString("pas démarré")
	}
	s := "audio=" + strconv.FormatInt(atomic.LoadInt64(&d.renderCalls), 10) + " frames=" + strconv.FormatInt(atomic.LoadInt64(&d.renderFrames), 10) +
		" midi=" + strconv.FormatInt(atomic.LoadInt64(&d.midiMsgs), 10) + " notes=" + strconv.FormatInt(atomic.LoadInt64(&d.noteOns), 10)
	return C.CString(s)
}

func main() {}
