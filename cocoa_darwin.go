//go:build darwin

package main

// Petites briques pour parler à macOS (Cocoa, CoreGraphics, CoreMIDI) sans compilateur C :
// la bibliothèque purego appelle directement les fonctions du système.

import (
	"sync"
	"unsafe"

	"github.com/ebitengine/purego"
	"github.com/ebitengine/purego/objc"
)

type nsPoint struct{ X, Y float64 }
type nsSize struct{ W, H float64 }
type nsRect struct {
	Origin nsPoint
	Size   nsSize
}

var (
	libCocoa, libCG, libQuartz, libCF, libMIDI uintptr

	cgColorSpaceCreateDeviceRGB func() uintptr
	cgBitmapContextCreate       func(data, w, h, bpc, bpr, cs uintptr, info uint32) uintptr
	cgBitmapContextCreateImage  func(ctx uintptr) uintptr
	cgImageRelease              func(img uintptr)
	cgContextRelease            func(ctx uintptr)
	cgEventCreateKeyboardEvent  func(src uintptr, key uint16, down bool) uintptr
	cgEventKeyboardGetUnicode   func(ev uintptr, maxLen uintptr, actual *uintptr, buf *uint16)

	cfRelease          func(p uintptr)
	cfStringGetCString func(s uintptr, buf *byte, size int, enc uint32) bool
	cfDataGetBytePtr   func(d uintptr) uintptr

	tisCopyCurrentLayout func() uintptr
	tisGetProperty       func(src, key uintptr) uintptr
	ucKeyTranslate       func(layout uintptr, keyCode, action uint16, modState, kbdType, options uint32, dead *uint32, maxLen uintptr, actual *uintptr, buf *uint16) int32
	kTISUnicodeLayoutKey uintptr
)

var cocoaOnce sync.Once
var cocoaErr error

// charge les bibliothèques du système (une seule fois)
func cocoaInit() error {
	cocoaOnce.Do(func() {
		open := func(path string) uintptr {
			h, err := purego.Dlopen(path, purego.RTLD_GLOBAL|purego.RTLD_LAZY)
			if err != nil && cocoaErr == nil {
				cocoaErr = err
			}
			return h
		}
		fw := func(n string) string { return "/System/Library/Frameworks/" + n + ".framework/" + n }
		libCocoa = open(fw("Cocoa"))
		libCG = open(fw("CoreGraphics"))
		libQuartz = open(fw("QuartzCore"))
		libCF = open(fw("CoreFoundation"))
		libMIDI = open(fw("CoreMIDI"))
		if cocoaErr != nil {
			return
		}
		purego.RegisterLibFunc(&cgColorSpaceCreateDeviceRGB, libCG, "CGColorSpaceCreateDeviceRGB")
		purego.RegisterLibFunc(&cgBitmapContextCreate, libCG, "CGBitmapContextCreate")
		purego.RegisterLibFunc(&cgBitmapContextCreateImage, libCG, "CGBitmapContextCreateImage")
		purego.RegisterLibFunc(&cgImageRelease, libCG, "CGImageRelease")
		purego.RegisterLibFunc(&cgContextRelease, libCG, "CGContextRelease")
		// lettre écrite sur une touche, selon le clavier actif : facultatif (sinon lettres par défaut)
		func() {
			defer func() { recover() }()
			purego.RegisterLibFunc(&cgEventCreateKeyboardEvent, libCG, "CGEventCreateKeyboardEvent")
			purego.RegisterLibFunc(&cgEventKeyboardGetUnicode, libCG, "CGEventKeyboardGetUnicodeString")
		}()
		purego.RegisterLibFunc(&cfRelease, libCF, "CFRelease")
		purego.RegisterLibFunc(&cfStringGetCString, libCF, "CFStringGetCString")
	})
	return cocoaErr
}

// ---- Objective-C : raccourcis

var selCache sync.Map

func sel(name string) objc.SEL {
	if v, ok := selCache.Load(name); ok {
		return v.(objc.SEL)
	}
	s := objc.RegisterName(name)
	selCache.Store(name, s)
	return s
}

func cls(name string) objc.ID { return objc.ID(objc.GetClass(name)) }

func nsString(s string) objc.ID {
	return cls("NSString").Send(sel("stringWithUTF8String:"), s)
}

// texte d'un NSString
func goStr(id objc.ID) string {
	if id == 0 {
		return ""
	}
	p := objc.Send[uintptr](id, sel("UTF8String"))
	if p == 0 {
		return ""
	}
	var b []byte
	for i := uintptr(0); i < 4096; i++ {
		c := *(*byte)(toPtr(p + i))
		if c == 0 {
			break
		}
		b = append(b, c)
	}
	return string(b)
}

// texte d'une CFString (même chose qu'un NSString pour le système)
func cfStr(p uintptr) string {
	if p == 0 {
		return ""
	}
	buf := make([]byte, 512)
	if !cfStringGetCString(p, &buf[0], len(buf), 0x08000100) { // UTF-8
		return ""
	}
	for i, c := range buf {
		if c == 0 {
			return string(buf[:i])
		}
	}
	return string(buf)
}

func toPtr(a uintptr) unsafe.Pointer { return *(*unsafe.Pointer)(unsafe.Pointer(&a)) }
func peek(a uintptr) uintptr         { return *(*uintptr)(toPtr(a)) }

func nsArray(items ...string) objc.ID {
	arr := cls("NSMutableArray").Send(sel("array"))
	for _, s := range items {
		arr.Send(sel("addObject:"), nsString(s))
	}
	return arr
}

// adresse du premier pixel (la mémoire reste vivante tant que le Canvas existe)
func addrOf(p []uint32) uintptr { return uintptr(unsafe.Pointer(&p[0])) }
