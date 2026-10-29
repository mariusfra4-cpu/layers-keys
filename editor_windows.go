//go:build windows

package main

// Éditeur de plugin : la même fenêtre que le logiciel, mais comme fenêtre enfant d'un DAW.

import (
	"sync/atomic"
	"syscall"
	"unsafe"
)

var editorClassOnce int32

const (
	wsChild        = 0x40000000
	wsVisible      = 0x10000000
	wsClipSiblings = 0x04000000
	wsClipChildren = 0x02000000
)

// editorSize : taille de la fenêtre (réduite si l'écran est petit)
func (a *winApp) editorSize() (int, int) {
	a.scale = 1
	var work rectT
	if call(pSystemParamInfo, 0x0030, 0, ptr(&work), 0) != 0 {
		sw, sh := float64(work.right-work.left)-24, float64(work.bottom-work.top)-90
		if s := min(sw/WinW, sh/WinH); s < 1 && s > 0.3 {
			a.scale = s
		}
	}
	a.cw, a.ch = int(WinW*a.scale+0.5), int(WinH*a.scale+0.5)
	return a.cw, a.ch
}

// attachEditor crée la fenêtre dans la fenêtre parent donnée par le DAW.
func (a *winApp) attachEditor(parent, hinst uintptr) bool {
	a.plugin = true
	if a.hwnd != 0 { // déjà ouvert (le DAW n'a pas fermé l'ancienne vue) : on repart proprement
		a.detachEditor()
	}
	atomic.StoreInt32(&a.pending, 0)
	class := u16("LayersKeysPluginView")
	if atomic.CompareAndSwapInt32(&editorClassOnce, 0, 1) {
		wc := wndClassEx{
			cursor:   call(pLoadCursor, 0, 32512),
			instance: hinst,
			class:    class,
			wndProc:  syscall.NewCallback(wndProcThunk),
		}
		wc.size = uint32(unsafe.Sizeof(wc))
		call(pRegisterClassEx, ptr(&wc))
	}
	w, h := a.editorSize()
	hwnd := call(pCreateWindowEx, 0, uintptr(unsafe.Pointer(class)), 0, wsChild|wsVisible|wsClipSiblings|wsClipChildren,
		0, 0, uintptr(w), uintptr(h), parent, 0, hinst, 0)
	if hwnd == 0 {
		return false
	}
	appsMu.Lock()
	apps[hwnd] = a
	appsMu.Unlock()
	a.hwnd = hwnd
	if !a.setupGraphics() {
		a.detachEditor()
		return false
	}
	a.ui.mu.Lock()
	a.ui.toast = ""
	a.ui.mu.Unlock()
	a.ui.RefreshKeyLabels()
	call(pSetTimer, hwnd, 1, 40, 0)
	call(pShowWindow, hwnd, 5)
	call(pUpdateWindow, hwnd)
	return true
}

func (a *winApp) detachEditor() {
	hwnd := a.hwnd
	if hwnd == 0 {
		return
	}
	a.ui.FocusLost() // relâche les touches du clavier du PC encore enfoncées
	call(pKillTimer, hwnd, 1)
	a.hwnd = 0 // plus aucun redessin demandé
	call(pReleaseCapture)
	call(pDestroyWindow, hwnd)
	appsMu.Lock()
	delete(apps, hwnd)
	appsMu.Unlock()
	a.cleanupGraphics()
	// un redessin demandé avant la fermeture ne sera jamais traité : on remet le compteur à zéro,
	// sinon la fenêtre rouverte ne serait plus jamais redessinée
	atomic.StoreInt32(&a.pending, 0)
}
