//go:build !vst3

package main

import (
	"runtime"
	"runtime/debug"
	"syscall"
	"unsafe"
)

func init() { runtime.LockOSThread() }

func main() {
	call(pCoInitializeEx, 0, 2) // COINIT_APARTMENTTHREADED : nécessaire à la fenêtre « Ouvrir »
	eng := NewEngine()
	a := &winApp{eng: eng, scale: 1}
	theApp = a
	a.ui = NewUI(eng, a, configPath())
	a.ui.OnRescan = func() { a.scanMidi(true) }

	// si l'écran est petit (portable 1366x768), la fenêtre est réduite pour tenir entièrement
	var work rectT
	if call(pSystemParamInfo, 0x0030, 0, ptr(&work), 0) != 0 {
		sw, sh := float64(work.right-work.left)-24, float64(work.bottom-work.top)-56
		if s := min(sw/WinW, sh/WinH); s < 1 && s > 0.3 {
			a.scale = s
		}
	}
	a.cw, a.ch = int(WinW*a.scale+0.5), int(WinH*a.scale+0.5)

	hinst := call(pGetModuleHandle, 0)
	class := u16("LayersKeysWindow")
	icon := call(pLoadIcon, hinst, 1)
	wc := wndClassEx{
		cursor:   call(pLoadCursor, 0, 32512),
		instance: hinst,
		class:    class,
		icon:     icon,
		iconSm:   icon,
		wndProc:  syscall.NewCallback(wndProcThunk),
	}
	wc.size = uint32(unsafe.Sizeof(wc))
	call(pRegisterClassEx, ptr(&wc))

	style := uintptr(wsCaption | wsSysMenu | wsMinimizeBox)
	rc := rectT{0, 0, int32(a.cw), int32(a.ch)}
	call(pAdjustWindowRect, ptr(&rc), style, 0, 0)
	hwnd := call(pCreateWindowEx, 0, uintptr(unsafe.Pointer(class)), uintptr(unsafe.Pointer(u16("Layers Keys  ·  By JB Praise"))), style,
		uintptr(cwUseDefault), uintptr(cwUseDefault), uintptr(rc.right-rc.left), uintptr(rc.bottom-rc.top), 0, 0, hinst, 0)
	if hwnd == 0 {
		fatal("Impossible d'ouvrir la fenêtre.")
		return
	}
	a.hwnd = hwnd
	if !a.setupGraphics() {
		fatal("Impossible de préparer l'affichage.")
		return
	}

	asioWindow = hwnd
	asioResetHook = func() { call(pPostMessage, a.hwnd, wmApp+2, 0, 0) }
	debug.SetGCPercent(400) // moins de pauses du ramasse-miettes = moins de craquements
	a.ui.StartAudio()
	a.scanMidi(true)
	call(pSetTimer, hwnd, 1, 40, 0)

	call(pShowWindow, hwnd, 5)
	call(pUpdateWindow, hwnd)

	var m msgT
	for {
		r := int32(call(pGetMessage, ptr(&m), 0, 0, 0))
		if r <= 0 {
			break
		}
		call(pTranslateMessage, ptr(&m))
		call(pDispatchMessage, ptr(&m))
	}
}
