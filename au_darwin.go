//go:build darwin && au

package main

/*
#cgo darwin LDFLAGS: -framework CoreFoundation -framework AudioToolbox
#include <stdint.h>
extern uintptr_t lkAUHandleFor(void *ci);
*/
import "C"

// Plugin Audio Unit : l'éditeur est une vue Cocoa fabriquée à la demande du logiciel hôte (Logic, GarageBand…).

import (
	"sync"

	"github.com/ebitengine/purego"
	"github.com/ebitengine/purego/objc"
)

var auClassOnce sync.Once

//export lkAUPrepareUI
func lkAUPrepareUI(h C.uintptr_t) {
	defer recoverPlugin()
	auClassOnce.Do(registerAUViewFactory)
}

func registerAUViewFactory() {
	if cocoaInit() != nil {
		return
	}
	// le protocole AUCocoaUIBase est dans CoreAudioKit
	purego_open("/System/Library/Frameworks/CoreAudioKit.framework/CoreAudioKit")
	cls, err := objc.RegisterClass("LKAUViewFactory", objc.GetClass("NSObject"), nil, nil, []objc.MethodDef{
		{Cmd: sel("interfaceVersion"), Fn: func(self objc.ID, _cmd objc.SEL) uint { return 0 }},
		// (NSView *)uiViewForAudioUnit:(AudioUnit)au withSize:(NSSize)size — la taille demandée est ignorée
		{Cmd: sel("uiViewForAudioUnit:withSize:"), Fn: func(self objc.ID, _cmd objc.SEL, au uintptr) objc.ID {
			defer recoverPlugin()
			lklog("AU : le logiciel hôte demande l'interface")
			h := C.lkAUHandleFor(toPtr(au))
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
			return v
		}},
		{Cmd: sel("description"), Fn: func(self objc.ID, _cmd objc.SEL) objc.ID { return nsString("Layers Keys") }},
	})
	if err != nil {
		return
	}
	// déclare que la classe respecte AUCocoaUIBase (si le système connaît ce protocole)
	var getProto func(name string) uintptr
	var addProto func(c objc.Class, p uintptr) bool
	lib, e := purego.Dlopen("/usr/lib/libobjc.A.dylib", purego.RTLD_GLOBAL)
	if e != nil {
		return
	}
	purego.RegisterLibFunc(&getProto, lib, "objc_getProtocol")
	purego.RegisterLibFunc(&addProto, lib, "class_addProtocol")
	if p := getProto("AUCocoaUIBase"); p != 0 {
		addProto(cls, p)
	}
}

func purego_open(path string) {
	_, _ = purego.Dlopen(path, purego.RTLD_GLOBAL|purego.RTLD_LAZY)
}
