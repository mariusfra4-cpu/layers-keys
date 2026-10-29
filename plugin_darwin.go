//go:build darwin && (vst3 || au)

package main

/*
#include <stdint.h>
#include <stdlib.h>
*/
import "C"

// Plugin VST3 : la colle en C (vst3_glue.c) appelle ces fonctions. Un « instance » = un exemplaire du plugin dans le DAW.

import (
	"runtime/debug"
	"sync"
	"sync/atomic"
	"unsafe"

	"github.com/ebitengine/purego/objc"
)

type lkInst struct {
	refs int32
	eng  *Engine
	ui   *UI
	app  *macApp

	mu      sync.Mutex
	pending []byte // état reçu du DAW, à appliquer dans le thread de la fenêtre
}

var (
	instMu   sync.RWMutex
	insts            = map[uintptr]*lkInst{}
	nextInst uintptr = 1
	gcOnce   sync.Once
)

func getInst(h C.uintptr_t) *lkInst {
	instMu.RLock()
	i := insts[uintptr(h)]
	instMu.RUnlock()
	return i
}

//export lkNew
func lkNew() C.uintptr_t {
	defer recoverPlugin()
	gcOnce.Do(func() { debug.SetGCPercent(400) })
	eng := NewEngine()
	if cocoaInit() != nil {
		return 0
	}
	app := &macApp{eng: eng, scale: 1, plugin: true}
	ui := NewUI(eng, app, "")
	ui.Plugin = true
	app.ui = ui
	in := &lkInst{refs: 1, eng: eng, ui: ui, app: app}
	app.onUser = in.applyPending
	instMu.Lock()
	h := nextInst
	nextInst++
	insts[h] = in
	instMu.Unlock()
	return C.uintptr_t(h)
}

//export lkRetain
func lkRetain(h C.uintptr_t) {
	defer recoverPlugin()
	if in := getInst(h); in != nil {
		atomic.AddInt32(&in.refs, 1)
	}
}

//export lkRelease
func lkRelease(h C.uintptr_t) {
	defer recoverPlugin()
	in := getInst(h)
	if in == nil {
		return
	}
	if atomic.AddInt32(&in.refs, -1) > 0 {
		return
	}
	in.app.detachEditor()
	in.eng.Panic()
	in.ui.Shutdown()
	instMu.Lock()
	delete(insts, uintptr(h))
	instMu.Unlock()
}

//export lkSetup
func lkSetup(h C.uintptr_t, rate C.double, maxBlock C.int) {
	defer recoverPlugin()
	if in := getInst(h); in != nil {
		if int(rate) != in.eng.Rate() {
			in.eng.SetSampleRate(int(rate))
		}
	}
}

//export lkProcess
func lkProcess(h C.uintptr_t, l, r *C.float, n C.int) {
	defer recoverPlugin()
	in := getInst(h)
	if n <= 0 {
		return
	}
	ls := unsafe.Slice((*float32)(unsafe.Pointer(l)), int(n))
	rs := unsafe.Slice((*float32)(unsafe.Pointer(r)), int(n))
	if in == nil {
		for i := range ls {
			ls[i], rs[i] = 0, 0
		}
		return
	}
	in.eng.RenderF(ls, rs)
}

//export lkMidi
func lkMidi(h C.uintptr_t, status, d1, d2 C.int) {
	defer recoverPlugin()
	if in := getInst(h); in != nil {
		in.ui.MidiFast(uint32(status&0xFF) | uint32(d1&0x7F)<<8 | uint32(d2&0x7F)<<16)
	}
}

//export lkGetState
func lkGetState(h C.uintptr_t, n *C.int) unsafe.Pointer {
	defer recoverPlugin()
	in := getInst(h)
	if in == nil {
		*n = 0
		return nil
	}
	b := in.ui.StateBytes()
	*n = C.int(len(b))
	return C.CBytes(b)
}

func (in *lkInst) applyPending() {
	in.mu.Lock()
	b := in.pending
	in.pending = nil
	in.mu.Unlock()
	if b != nil {
		in.ui.SetStateBytes(b)
	}
}

//export lkSetState
func lkSetState(h C.uintptr_t, p unsafe.Pointer, n C.int) C.int {
	defer recoverPlugin()
	in := getInst(h)
	if in == nil || n <= 0 {
		return 0
	}
	b := C.GoBytes(p, n)
	in.mu.Lock()
	in.pending = b
	in.mu.Unlock()
	if in.app.view != 0 { // l'éditeur est ouvert : l'interface applique l'état à sa prochaine minuterie
		atomic.StoreInt32(&in.app.userFlag, 1)
	} else {
		in.applyPending()
	}
	return 1
}

//export lkAttach
func lkAttach(h C.uintptr_t, parent, hinst C.uintptr_t) C.int {
	defer recoverPlugin()
	in := getInst(h)
	if in == nil {
		return 0
	}
	if in.app.attachEditor(objc.ID(parent)) { // « parent » : la NSView du logiciel hôte
		return 1
	}
	return 0
}

//export lkDetach
func lkDetach(h C.uintptr_t) {
	defer recoverPlugin()
	if in := getInst(h); in != nil {
		in.app.detachEditor()
	}
}

//export lkSize
func lkSize(h C.uintptr_t, w, hh *C.int) {
	defer recoverPlugin()
	in := getInst(h)
	if in == nil {
		*w, *hh = WinW, WinH
		return
	}
	cw, ch := in.app.editorSize()
	*w, *hh = C.int(cw), C.int(ch)
}

// une erreur interne ne doit jamais faire tomber le DAW : on l'attrape et on continue
func recoverPlugin() {
	if r := recover(); r != nil {
		pluginErrors++
	}
}

var pluginErrors int

func main() {}
