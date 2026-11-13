//go:build darwin && au

package main

/*
#cgo darwin LDFLAGS: -framework CoreFoundation -framework AudioToolbox -framework AudioUnit -framework CoreMIDI -framework AppKit -framework Foundation
#include <stdint.h>
extern uintptr_t lkAUHandleFor(void *ci);
*/
import "C"

// Plugin Audio Unit : l'éditeur est une vue Cocoa fabriquée à la demande du logiciel hôte (Logic, GarageBand, FL Studio…).
// La classe Objective-C que l'hôte instancie (LKAUViewFactory) est écrite dans au_view_darwin.m, compilée dans le plugin :
// l'hôte la trouve ainsi dans le paquet. Elle appelle lkAUCreateView ci-dessous.

import "sync"

var auUIOnce sync.Once

//export lkAULog
func lkAULog(s *C.char) { lklog("%s", C.GoString(s)) }

// appelée quand l'hôte lit la propriété CocoaUI : prépare Cocoa avant que l'hôte crée la vue
//
//export lkAUPrepareUI
func lkAUPrepareUI(h C.uintptr_t) {
	defer recoverPlugin()
	auUIOnce.Do(func() {
		if err := cocoaInit(); err != nil {
			lklog("AU : Cocoa n'a pas pu être préparé : %v", err)
		}
	})
}

// appelée par -[LKAUViewFactory uiViewForAudioUnit:withSize:] ; renvoie une NSView « autorelease », ou 0
//
//export lkAUCreateView
func lkAUCreateView(au C.uintptr_t) C.uintptr_t {
	defer recoverPlugin()
	lklog("AU : le logiciel hôte demande l'interface")
	if cocoaInit() != nil {
		lklog("AU : Cocoa indisponible, pas d'interface")
		return 0
	}
	h := C.lkAUHandleFor(toPtr(uintptr(au)))
	in := getInst(h)
	if in == nil {
		lklog("AU : instance introuvable pour cette interface")
		return 0
	}
	in.app.autoDetach = true
	v := in.app.createEditorView()
	if v == 0 {
		lklog("AU : la vue n'a pas pu être créée")
		return 0
	}
	v.Send(sel("autorelease"))
	lklog("AU : interface fournie au logiciel hôte (%.0f x %.0f)", in.app.cw, in.app.ch)
	return C.uintptr_t(v)
}
