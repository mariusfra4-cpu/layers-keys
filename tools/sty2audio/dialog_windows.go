//go:build windows

package main

import (
	"os/exec"
	"path/filepath"
	"syscall"
	"unicode/utf16"
	"unsafe"
)

type openFileNameW struct {
	lStructSize       uint32
	hwndOwner         uintptr
	hInstance         uintptr
	lpstrFilter       *uint16
	lpstrCustomFilter *uint16
	nMaxCustFilter    uint32
	nFilterIndex      uint32
	lpstrFile         *uint16
	nMaxFile          uint32
	lpstrFileTitle    *uint16
	nMaxFileTitle     uint32
	lpstrInitialDir   *uint16
	lpstrTitle        *uint16
	Flags             uint32
	nFileOffset       uint16
	nFileExtension    uint16
	lpstrDefExt       *uint16
	lCustData         uintptr
	lpfnHook          uintptr
	lpTemplateName    *uint16
	pvReserved        uintptr
	dwReserved        uint32
	FlagsEx           uint32
}

func u16z(s string) []uint16 { return append(utf16.Encode([]rune(s)), 0) }

// pickStyles : fenêtre Windows « Ouvrir » pour choisir un ou plusieurs styles.
func pickStyles() []string {
	comdlg := syscall.NewLazyDLL("comdlg32.dll")
	getOpen := comdlg.NewProc("GetOpenFileNameW")
	console := syscall.NewLazyDLL("kernel32.dll").NewProc("GetConsoleWindow")
	owner, _, _ := console.Call()

	filter := utf16.Encode([]rune("Styles Yamaha (*.sty, *.prs, *.bcs, *.sst, *.fps…)\x00*.sty;*.prs;*.bcs;*.sst;*.fps;*.pcs;*.pst\x00Tous les fichiers\x00*.*\x00\x00"))
	buf := make([]uint16, 1<<16)
	title := u16z("Choisissez les styles à convertir en MP3 et WAV (Ctrl+clic pour en choisir plusieurs)")
	ofn := openFileNameW{
		hwndOwner:   owner,
		lpstrFilter: &filter[0],
		lpstrFile:   &buf[0],
		nMaxFile:    uint32(len(buf)),
		lpstrTitle:  &title[0],
		Flags:       0x200 | 0x80000 | 0x1000 | 0x800 | 0x8, // multisélection, explorateur, fichiers existants, pas de changement de dossier
	}
	ofn.lStructSize = uint32(unsafe.Sizeof(ofn))
	r, _, _ := getOpen.Call(uintptr(unsafe.Pointer(&ofn)))
	if r == 0 {
		return nil
	}
	var parts []string
	start := 0
	for i := 0; i < len(buf); i++ {
		if buf[i] == 0 {
			if i == start {
				break
			}
			parts = append(parts, string(utf16.Decode(buf[start:i])))
			start = i + 1
		}
	}
	if len(parts) <= 1 {
		return parts
	}
	var out []string
	for _, f := range parts[1:] {
		out = append(out, filepath.Join(parts[0], f))
	}
	return out
}

// openFolder : ouvre le dossier dans l'Explorateur.
func openFolder(dir string) { exec.Command("explorer.exe", dir).Start() }
