//go:build darwin && au

package main

/*
#cgo darwin LDFLAGS: -framework CoreFoundation -framework AudioToolbox -framework AudioUnit -framework CoreMIDI -framework AppKit -framework Foundation
#include <stdint.h>
extern uintptr_t lkAUHandleFor(void *ci);
extern uint64_t lk_st(int i);
*/
import "C"

// Plugin Audio Unit : l'éditeur est une vue Cocoa fabriquée à la demande du logiciel hôte (Logic, GarageBand, FL Studio…).
// La classe Objective-C que l'hôte instancie (LKAUViewFactory) est écrite dans au_view_darwin.m, compilée dans le plugin :
// l'hôte la trouve ainsi dans le paquet. Elle appelle lkAUCreateView ci-dessous.

import (
	"math"
	"sync"
	"sync/atomic"
	"time"
)

// ---- diagnostic du son, écrit dans LayersKeys.log toutes les 3 s (seulement quand quelque chose change)

var (
	auPeakBits uint64 // crête de sortie depuis le dernier rapport (float64 en bits)
	auDiagOnce sync.Once
)

// auNotePeak : appelé par lkProcess à chaque bloc (thread audio, sans verrou)
func auNotePeak(l, r []float32) {
	p := 0.0
	for i := range l {
		if v := math.Abs(float64(l[i])); v > p {
			p = v
		}
		if v := math.Abs(float64(r[i])); v > p {
			p = v
		}
	}
	for {
		old := atomic.LoadUint64(&auPeakBits)
		if p <= math.Float64frombits(old) || atomic.CompareAndSwapUint64(&auPeakBits, old, math.Float64bits(p)) {
			return
		}
	}
}

func auDiagStart() {
	auDiagOnce.Do(func() {
		go func() {
			var last [8]uint64
			notes, reports := 0, 0
			for {
				time.Sleep(3 * time.Second)
				var cur [8]uint64
				for i := range cur {
					cur[i] = uint64(C.lk_st(C.int(i)))
				}
				pk := math.Float64frombits(atomic.SwapUint64(&auPeakBits, 0))
				if cur == last && pk == 0 {
					continue
				}
				// les 40 premiers rapports, puis seulement si un refus apparaît (le journal ne doit pas grossir)
				if reports++; reports > 40 && cur[1] == last[1] {
					last = cur
					continue
				}
				n := int(atomic.LoadInt64(&auNotesOn))
				lklog("AU son : %d blocs demandés (+%d, dernier %d images, %d tampons%s), %d refusés (code %d), notes reçues : classique %d / liste %d, notes jouées %d (+%d), crête de sortie %.4f",
					cur[0], cur[0]-last[0], cur[3], cur[4], map[bool]string{true: ", entrelacé", false: ""}[cur[7] != 0],
					cur[1], int32(cur[2]), cur[5], cur[6], n, n-notes, pk)
				last, notes = cur, n
			}
		}()
	})
}

var auNotesOn int64 // notes reçues par le moteur (compteur du diagnostic)

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

func auDiagPeak(l, r []float32) { auNotePeak(l, r) }
func auDiagNote()               { atomic.AddInt64(&auNotesOn, 1) }
func auDiagBegin()              { auDiagStart() }
