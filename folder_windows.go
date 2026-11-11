//go:build windows

package main

// Choix d'un dossier sous Windows (boîte « Rechercher un dossier » du système, shell32), sans bibliothèque externe.

import (
	"runtime"
	"syscall"
	"unsafe"
)

var (
	shell32              = syscall.NewLazyDLL("shell32.dll")
	pSHBrowseForFolder   = shell32.NewProc("SHBrowseForFolderW")
	pSHGetPathFromIDList = shell32.NewProc("SHGetPathFromIDListW")
)

type browseInfo struct {
	owner       uintptr
	root        uintptr
	displayName *uint16
	title       *uint16
	flags       uint32
	callback    uintptr
	lParam      uintptr
	image       int32
}

// folderDialog : renvoie le dossier choisi, ou "" si on annule.
func (a *winApp) folderDialog(title string) (string, bool) {
	name := make([]uint16, 260)
	path := make([]uint16, 1024)
	t := u16(title)
	bi := browseInfo{owner: a.hwnd, displayName: &name[0], title: t,
		// BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON
		flags: 0x1 | 0x40 | 0x200}
	call(pReleaseCapture)
	pidl := call(pSHBrowseForFolder, ptr(&bi))
	runtime.KeepAlive(&bi)
	runtime.KeepAlive(name)
	runtime.KeepAlive(t)
	if pidl == 0 {
		return "", false
	}
	ok := call(pSHGetPathFromIDList, pidl, uintptr(unsafe.Pointer(&path[0])))
	call(pCoTaskMemFree, pidl)
	if ok == 0 {
		return "", false
	}
	p := syscall.UTF16ToString(path)
	return p, p != ""
}
