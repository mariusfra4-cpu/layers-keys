//go:build darwin && (vst3 || au)

// Logic Pro 13 surveille les signaux qui arrivent pendant qu'il appelle un plugin et prend chacun pour un plantage
// (« The Audio Unit plug-in reported a problem… »). Go en envoie normalement de lui-même (SIGURG) pour interrompre
// ses fils pendant le nettoyage de la mémoire : dans le plugin, on coupe ces signaux. Go s'interrompt alors seulement
// aux appels de fonction, sans aucun signal. Le réglage (asyncpreemptoff=1) est posé à la construction : voir
// build_mac_au.sh. Vérifié par le test « gardien » : tools/au_logic_test.m --gardien.

package main

/*
#include <stdint.h>
#include <stdlib.h>
*/
import "C"

// Plugin VST3 : la colle en C (vst3_glue.c) appelle ces fonctions. Un « instance » = un exemplaire du plugin dans le DAW.

import (
	"os"
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
	lklog("plugin %s : création d'une instance (macOS %s, GODEBUG %s)", pluginVersion, macVersionPlugin(), os.Getenv("GODEBUG"))
	gcOnce.Do(func() { debug.SetGCPercent(400) })
	eng := NewEngine()
	lklog("plugin : moteur créé")
	if err := cocoaInit(); err != nil {
		lklog("plugin : cocoaInit a échoué : %v", err)
		return 0
	}
	app := &macApp{eng: eng, scale: 1, plugin: true}
	lklog("plugin : cocoa prêt, création de l'interface")
	ui := NewUI(eng, app, "")
	lklog("plugin : interface créée")
	ui.Plugin = true
	auSetupUI(ui)
	app.ui = ui
	in := &lkInst{refs: 1, eng: eng, ui: ui, app: app}
	app.onUser = in.applyPending
	instMu.Lock()
	h := nextInst
	nextInst++
	insts[h] = in
	instMu.Unlock()
	lklog("plugin : instance %d créée", h)
	auDiagBegin()
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
	auDiagPeak(ls, rs)
}

//export lkMidi
func lkMidi(h C.uintptr_t, status, d1, d2 C.int) {
	defer recoverPlugin()
	if status&0xF0 == 0x90 && d2 > 0 {
		auDiagNote()
	}
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
	lklog("plugin : ouverture de la fenêtre dans l'hôte")
	if in.app.attachEditor(objc.ID(parent)) { // « parent » : la NSView du logiciel hôte
		lklog("plugin : fenêtre ouverte")
		return 1
	}
	lklog("plugin : la fenêtre n'a pas pu s'ouvrir")
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
		lklog("plugin : erreur interne attrapée : %v\n%s", r, debug.Stack())
	}
}

var pluginErrors int

const pluginVersion = "1.0.6"

func macVersionPlugin() string { return macVersionSysctl() }

func main() {}
