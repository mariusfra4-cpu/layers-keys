//go:build windows

package main

import (
	"syscall"
	"time"
	"unicode/utf16"
	"unsafe"
)

var (
	pOpenClipboard    = user32.NewProc("OpenClipboard")
	pCloseClipboard   = user32.NewProc("CloseClipboard")
	pEmptyClipboard   = user32.NewProc("EmptyClipboard")
	pGetClipboardData = user32.NewProc("GetClipboardData")
	pSetClipboardData = user32.NewProc("SetClipboardData")
	pGlobalAlloc      = kernel32.NewProc("GlobalAlloc")
	pGlobalLock       = kernel32.NewProc("GlobalLock")
	pGlobalUnlock     = kernel32.NewProc("GlobalUnlock")
	pGlobalFree       = kernel32.NewProc("GlobalFree")
)

func openClip(hwnd uintptr) bool {
	for i := 0; i < 10; i++ {
		if call(pOpenClipboard, hwnd) != 0 {
			return true
		}
		time.Sleep(10 * time.Millisecond)
	}
	return false
}

// ClipGet : texte du presse-papiers.
func (a *winApp) ClipGet() string {
	if !openClip(a.hwnd) {
		return ""
	}
	defer call(pCloseClipboard)
	h := call(pGetClipboardData, 13) // CF_UNICODETEXT
	if h == 0 {
		return ""
	}
	p := call(pGlobalLock, h)
	if p == 0 {
		return ""
	}
	defer call(pGlobalUnlock, h)
	var u []uint16
	for i := 0; i < 4096; i++ {
		c := *(*uint16)(unsafe.Pointer(p + uintptr(i)*2)) //nolint:govet
		if c == 0 {
			break
		}
		u = append(u, c)
	}
	return string(utf16.Decode(u))
}

// ClipSet : met un texte dans le presse-papiers.
func (a *winApp) ClipSet(s string) {
	u, err := syscall.UTF16FromString(s)
	if err != nil || !openClip(a.hwnd) {
		return
	}
	defer call(pCloseClipboard)
	call(pEmptyClipboard)
	h := call(pGlobalAlloc, 0x2, uintptr(len(u)*2)) // GMEM_MOVEABLE
	if h == 0 {
		return
	}
	p := call(pGlobalLock, h)
	if p == 0 {
		call(pGlobalFree, h)
		return
	}
	copy(unsafe.Slice((*uint16)(unsafe.Pointer(p)), len(u)), u) //nolint:govet
	call(pGlobalUnlock, h)
	if call(pSetClipboardData, 13, h) == 0 {
		call(pGlobalFree, h)
	}
}
