//go:build darwin && !vst3 && !au

package main

// Liste des cartes son du Mac (CoreAudio) : on les voit et on choisit celle qu'on veut, comme sur Windows.

import (
	"sort"
	"sync"
	"unsafe"

	"github.com/ebitengine/purego"
)

type aoAddr struct{ selector, scope, element uint32 }

const (
	aoSystem    = 1
	aoGlobal    = 0x676c6f62 // 'glob'
	aoOutput    = 0x6f757470 // 'outp'
	aoDevices   = 0x64657623 // 'dev#'
	aoStreams   = 0x73746d23 // 'stm#'
	aoName      = 0x6c6e616d // 'lnam'
	aoUID       = 0x75696420 // 'uid '
	aoDefOutput = 0x644f7574 // 'dOut'
)

var (
	caOnce       sync.Once
	caOK         bool
	caGetSize    func(obj uint32, a *aoAddr, qs uint32, q unsafe.Pointer, size *uint32) int32
	caGetData    func(obj uint32, a *aoAddr, qs uint32, q unsafe.Pointer, size *uint32, data unsafe.Pointer) int32
	cfGetCString func(s uintptr, buf *byte, n int64, enc uint32) bool
	cfRelease2   func(s uintptr)
)

func caInit() bool {
	caOnce.Do(func() {
		ca, err := purego.Dlopen("/System/Library/Frameworks/CoreAudio.framework/CoreAudio", purego.RTLD_LAZY|purego.RTLD_GLOBAL)
		if err != nil {
			lklog("cartes son : CoreAudio indisponible : %v", err)
			return
		}
		cf, err := purego.Dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", purego.RTLD_LAZY|purego.RTLD_GLOBAL)
		if err != nil {
			lklog("cartes son : CoreFoundation indisponible : %v", err)
			return
		}
		purego.RegisterLibFunc(&caGetSize, ca, "AudioObjectGetPropertyDataSize")
		purego.RegisterLibFunc(&caGetData, ca, "AudioObjectGetPropertyData")
		purego.RegisterLibFunc(&cfGetCString, cf, "CFStringGetCString")
		purego.RegisterLibFunc(&cfRelease2, cf, "CFRelease")
		caOK = true
	})
	return caOK
}

func caString(obj, selector, scope uint32) string {
	a := aoAddr{selector, scope, 0}
	var s uintptr
	size := uint32(unsafe.Sizeof(s))
	if caGetData(obj, &a, 0, nil, &size, unsafe.Pointer(&s)) != 0 || s == 0 {
		return ""
	}
	defer cfRelease2(s)
	buf := make([]byte, 512)
	if !cfGetCString(s, &buf[0], int64(len(buf)), 0x08000100) {
		return ""
	}
	n := 0
	for n < len(buf) && buf[n] != 0 {
		n++
	}
	return string(buf[:n])
}

type macDev struct{ name, uid string }

// toutes les cartes son qui ont une sortie
func macOutputDevices() []macDev {
	if !caInit() {
		return nil
	}
	a := aoAddr{aoDevices, aoGlobal, 0}
	var size uint32
	if caGetSize(aoSystem, &a, 0, nil, &size) != 0 || size == 0 {
		return nil
	}
	ids := make([]uint32, size/4)
	if caGetData(aoSystem, &a, 0, nil, &size, unsafe.Pointer(&ids[0])) != 0 {
		return nil
	}
	var out []macDev
	for _, id := range ids {
		sa := aoAddr{aoStreams, aoOutput, 0}
		var ss uint32
		if caGetSize(id, &sa, 0, nil, &ss) != 0 || ss == 0 {
			continue // pas de sortie (micro seul)
		}
		name, uid := caString(id, aoName, aoGlobal), caString(id, aoUID, aoGlobal)
		if name == "" || uid == "" {
			continue
		}
		out = append(out, macDev{name, uid})
	}
	sort.SliceStable(out, func(i, j int) bool { return out[i].name < out[j].name })
	return out
}

func macDeviceUID(name string) string {
	for _, d := range macOutputDevices() {
		if d.name == name {
			return d.uid
		}
	}
	return ""
}

// carte son de sortie choisie dans Réglages Système > Son
func macDefaultUID() string {
	if !caInit() {
		return ""
	}
	a := aoAddr{aoDefOutput, aoGlobal, 0}
	var id uint32
	size := uint32(4)
	if caGetData(aoSystem, &a, 0, nil, &size, unsafe.Pointer(&id)) != 0 || id == 0 {
		return ""
	}
	return caString(id, aoUID, aoGlobal)
}
