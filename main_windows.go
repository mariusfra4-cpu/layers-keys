//go:build windows

package main

// Partie Windows de Layers Keys : aucune bibliothèque externe, uniquement les DLL
// déjà présentes dans Windows (user32, gdi32, winmm, comdlg32, ole32, kernel32).
// Toute la logique et tout le dessin sont dans ui.go / ui_draw.go / canvas.go.

import (
	"fmt"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"unicode/utf16"
	"unsafe"
)

// ------------------------------------------------------------------ DLL Windows

var (
	user32   = syscall.NewLazyDLL("user32.dll")
	gdi32    = syscall.NewLazyDLL("gdi32.dll")
	kernel32 = syscall.NewLazyDLL("kernel32.dll")
	winmm    = syscall.NewLazyDLL("winmm.dll")
	comdlg32 = syscall.NewLazyDLL("comdlg32.dll")
	ole32    = syscall.NewLazyDLL("ole32.dll")

	pRegisterClassEx   = user32.NewProc("RegisterClassExW")
	pCreateWindowEx    = user32.NewProc("CreateWindowExW")
	pDefWindowProc     = user32.NewProc("DefWindowProcW")
	pShowWindow        = user32.NewProc("ShowWindow")
	pUpdateWindow      = user32.NewProc("UpdateWindow")
	pGetMessage        = user32.NewProc("GetMessageW")
	pTranslateMessage  = user32.NewProc("TranslateMessage")
	pDispatchMessage   = user32.NewProc("DispatchMessageW")
	pPostQuitMessage   = user32.NewProc("PostQuitMessage")
	pPostMessage       = user32.NewProc("PostMessageW")
	pLoadCursor        = user32.NewProc("LoadCursorW")
	pLoadIcon          = user32.NewProc("LoadIconW")
	pBeginPaint        = user32.NewProc("BeginPaint")
	pEndPaint          = user32.NewProc("EndPaint")
	pInvalidateRect    = user32.NewProc("InvalidateRect")
	pSetTimer          = user32.NewProc("SetTimer")
	pKillTimer         = user32.NewProc("KillTimer")
	pSetFocus          = user32.NewProc("SetFocus")
	pSetCapture        = user32.NewProc("SetCapture")
	pReleaseCapture    = user32.NewProc("ReleaseCapture")
	pAdjustWindowRect  = user32.NewProc("AdjustWindowRectEx")
	pMessageBox        = user32.NewProc("MessageBoxW")
	pGetDC             = user32.NewProc("GetDC")
	pReleaseDC         = user32.NewProc("ReleaseDC")
	pDrawText          = user32.NewProc("DrawTextW")
	pMapVirtualKeyEx   = user32.NewProc("MapVirtualKeyExW")
	pToUnicodeEx       = user32.NewProc("ToUnicodeEx")
	pGetKeyboardLayout = user32.NewProc("GetKeyboardLayout")
	pSystemParamInfo   = user32.NewProc("SystemParametersInfoW")
	pGetKeyState       = user32.NewProc("GetKeyState")
	pSetWindowText     = user32.NewProc("SetWindowTextW")
	pDestroyWindow     = user32.NewProc("DestroyWindow")

	pCreateCompatibleDC = gdi32.NewProc("CreateCompatibleDC")
	pCreateDIBSection   = gdi32.NewProc("CreateDIBSection")
	pSelectObject       = gdi32.NewProc("SelectObject")
	pDeleteObject       = gdi32.NewProc("DeleteObject")
	pDeleteDC           = gdi32.NewProc("DeleteDC")
	pBitBlt             = gdi32.NewProc("BitBlt")
	pStretchBlt         = gdi32.NewProc("StretchBlt")
	pSetStretchBltMode  = gdi32.NewProc("SetStretchBltMode")
	pSetBkMode          = gdi32.NewProc("SetBkMode")
	pSetTextColor       = gdi32.NewProc("SetTextColor")
	pCreateFont         = gdi32.NewProc("CreateFontW")
	pGdiFlush           = gdi32.NewProc("GdiFlush")

	pGetModuleHandle     = kernel32.NewProc("GetModuleHandleW")
	pCreateEvent         = kernel32.NewProc("CreateEventW")
	pWaitForSingleObject = kernel32.NewProc("WaitForSingleObject")

	pCoInitializeEx = ole32.NewProc("CoInitializeEx")

	pGetOpenFileName = comdlg32.NewProc("GetOpenFileNameW")
	pGetSaveFileName = comdlg32.NewProc("GetSaveFileNameW")

	pWaveOutOpen          = winmm.NewProc("waveOutOpen")
	pWaveOutClose         = winmm.NewProc("waveOutClose")
	pWaveOutReset         = winmm.NewProc("waveOutReset")
	pWaveOutPrepareHeader = winmm.NewProc("waveOutPrepareHeader")
	pWaveOutUnprepare     = winmm.NewProc("waveOutUnprepareHeader")
	pWaveOutWrite         = winmm.NewProc("waveOutWrite")
	pMidiInGetNumDevs     = winmm.NewProc("midiInGetNumDevs")
	pMidiInGetDevCaps     = winmm.NewProc("midiInGetDevCapsW")
	pMidiInOpen           = winmm.NewProc("midiInOpen")
	pMidiInStart          = winmm.NewProc("midiInStart")
	pMidiInReset          = winmm.NewProc("midiInReset")
	pMidiInClose          = winmm.NewProc("midiInClose")
)

func call(p *syscall.LazyProc, args ...uintptr) uintptr {
	r, _, _ := p.Call(args...)
	return r
}

func ptr[T any](p *T) uintptr { return uintptr(unsafe.Pointer(p)) }

// entier 32 bits signé -> argument d'appel Windows
func i32(v int) uintptr { return uintptr(uint32(int32(v))) }

func u16(s string) *uint16 {
	p, _ := syscall.UTF16PtrFromString(s)
	return p
}

// ------------------------------------------------------------------ structures

type rectT struct{ left, top, right, bottom int32 }
type pointT struct{ x, y int32 }

type wndClassEx struct {
	size       uint32
	style      uint32
	wndProc    uintptr
	clsExtra   int32
	wndExtra   int32
	instance   uintptr
	icon       uintptr
	cursor     uintptr
	background uintptr
	menu       *uint16
	class      *uint16
	iconSm     uintptr
}

type msgT struct {
	hwnd    uintptr
	message uint32
	wParam  uintptr
	lParam  uintptr
	time    uint32
	pt      pointT
}

type paintStruct struct {
	hdc       uintptr
	erase     int32
	rc        rectT
	restore   int32
	incUpdate int32
	reserved  [32]byte
}

type waveFormat struct {
	tag            uint16
	channels       uint16
	samplesPerSec  uint32
	avgBytesPerSec uint32
	blockAlign     uint16
	bitsPerSample  uint16
	cbSize         uint16
}

type waveHdr struct {
	data     uintptr
	length   uint32
	recorded uint32
	user     uintptr
	flags    uint32
	loops    uint32
	next     uintptr
	reserved uintptr
}

type midiInCaps struct {
	mid, pid uint16
	version  uint32
	name     [32]uint16
	support  uint32
}

type bitmapInfoHeader struct {
	size          uint32
	width, height int32
	planes        uint16
	bitCount      uint16
	compression   uint32
	sizeImage     uint32
	xPelsPerMeter int32
	yPelsPerMeter int32
	clrUsed       uint32
	clrImportant  uint32
}

type bitmapInfo struct {
	hdr   bitmapInfoHeader
	extra [4]byte
}

type openFileName struct {
	structSize    uint32
	owner         uintptr
	instance      uintptr
	filter        *uint16
	customFilter  *uint16
	maxCustFilter uint32
	filterIndex   uint32
	file          *uint16
	maxFile       uint32
	fileTitle     *uint16
	maxFileTitle  uint32
	initialDir    *uint16
	title         *uint16
	flags         uint32
	fileOffset    uint16
	fileExt       uint16
	defExt        *uint16
	custData      uintptr
	hook          uintptr
	template      *uint16
	reserved      uintptr
	reserved2     uint32
	flagsEx       uint32
}

const (
	wmDestroy     = 0x0002
	wmClose       = 0x0010
	wmKillFocus   = 0x0008
	wmPaint       = 0x000F
	wmEraseBkgnd  = 0x0014
	wmInputLang   = 0x0051
	wmKeyDown     = 0x0100
	wmKeyUp       = 0x0101
	wmChar        = 0x0102
	wmTimer       = 0x0113
	wmMouseMove   = 0x0200
	wmLButtonDown = 0x0201
	wmLButtonUp   = 0x0202
	wmMouseWheel  = 0x020A
	wmApp         = 0x8000
	mmMimData     = 0x03C3

	wsCaption     = 0x00C00000
	wsSysMenu     = 0x00080000
	wsMinimizeBox = 0x00020000
	cwUseDefault  = 0x80000000

	callbackWindow   = 0x00010000
	callbackFunction = 0x00030000
	callbackEvent    = 0x00050000
	whdrDone         = 0x00000001
	waveMapper       = 0xFFFFFFFF
	srcCopy          = 0x00CC0020
	mkLButton        = 0x0001

	dtCenter      = 0x0001
	dtRight       = 0x0002
	dtVCenter     = 0x0004
	dtSingle      = 0x0020
	dtNoPrefix    = 0x0800
	dtEndEllipsis = 0x8000
)

// ------------------------------------------------------------------ audio

// ------------------------------------------------------------------ l'application Windows

type winApp struct {
	hwnd  uintptr
	eng   *Engine
	ui    *UI
	audio audioBackend

	memDC  uintptr
	bmp    uintptr
	oldBmp uintptr
	canvas *Canvas
	fonts  [nFonts]uintptr
	scale  float64
	cw, ch int

	pending int32
	tick    int

	midiH       []uintptr
	midiNames   []string
	audioErr    string
	midiCb      uintptr
	midiPending int32
	onUser      func() // appelé dans le thread de la fenêtre (message wmApp+4)
	plugin      bool   // vrai : la fenêtre est l'éditeur d'un plugin VST3 (pas de MIDI, pas de sortie audio propre)
}

var theApp *winApp

// ---- Host : ce que l'interface demande à Windows

func (a *winApp) Invalidate() {
	if a.hwnd == 0 {
		return
	}
	if atomic.CompareAndSwapInt32(&a.pending, 0, 1) {
		call(pPostMessage, a.hwnd, wmApp+1, 0, 0)
	}
}

func (a *winApp) KeyLabel(sc uint32, fallback string) string {
	hkl := call(pGetKeyboardLayout, 0)
	vk := call(pMapVirtualKeyEx, uintptr(sc), 1, hkl)
	if vk == 0 {
		return fallback
	}
	var state [256]byte
	var buf [8]uint16
	n := int32(call(pToUnicodeEx, vk, uintptr(sc), ptr(&state[0]), ptr(&buf[0]), 8, 4, hkl))
	if n <= 0 {
		return fallback
	}
	s := strings.TrimSpace(strings.ToUpper(string(utf16.Decode(buf[:n]))))
	if s == "" {
		return fallback
	}
	return s
}

func (a *winApp) fileDialog(save bool, filterStr, title, defExt, initial string) (string, bool) {
	file := make([]uint16, 32768)
	if initial != "" {
		copy(file, utf16.Encode([]rune(initial)))
	}
	filter := utf16.Encode([]rune(filterStr))
	t := u16(title)
	var ext *uint16
	if defExt != "" {
		ext = u16(defExt)
	}
	var ofn openFileName
	ofn.structSize = uint32(unsafe.Sizeof(ofn))
	ofn.owner = a.hwnd
	ofn.filter = &filter[0]
	ofn.filterIndex = 1
	ofn.file = &file[0]
	ofn.maxFile = uint32(len(file))
	ofn.title = t
	ofn.defExt = ext
	// OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_EXPLORER (+ FILEMUSTEXIST ou OVERWRITEPROMPT)
	ofn.flags = 0x8 | 0x800 | 0x80000
	proc := pGetOpenFileName
	if save {
		ofn.flags |= 0x2
		proc = pGetSaveFileName
	} else {
		ofn.flags |= 0x1000
	}
	call(pReleaseCapture)
	r := call(proc, ptr(&ofn))
	runtime.KeepAlive(&ofn)
	runtime.KeepAlive(filter)
	runtime.KeepAlive(file)
	runtime.KeepAlive(t)
	runtime.KeepAlive(ext)
	if r == 0 {
		return "", false
	}
	return syscall.UTF16ToString(file), true
}

func (a *winApp) OpenFile(kind string) (string, bool) {
	switch kind {
	case "folder":
		return a.folderDialog("Choisir le dossier de sons (Layers Keys l'analyse et range les sons)")
	case "padfolder":
		return a.folderDialog("Choisir le dossier des pads (un pad par tonalité, majeur et mineur)")
	}
	if kind == "project" {
		return a.fileDialog(false, "Projets Layers Keys (*.lkeys)\x00*.lkeys\x00Tous les fichiers\x00*.*\x00\x00",
			"Ouvrir un projet Layers Keys", "", "")
	}
	if kind == "audio" {
		return a.fileDialog(false, "Audio (WAV, MP3)\x00*.wav;*.wave;*.mp3\x00Tous les fichiers\x00*.*\x00\x00",
			"Choisir un fichier audio à lire", "", "")
	}
	return a.fileDialog(false, "Sons (SF2, SF3, WAV, MP3)\x00*.sf2;*.sf3;*.wav;*.wave;*.mp3\x00Tous les fichiers\x00*.*\x00\x00",
		"Choisir un fichier SF2, WAV ou MP3", "", "")
}

func (a *winApp) SaveFile(defaultName string) (string, bool) {
	return a.fileDialog(true, "Projets Layers Keys (*.lkeys)\x00*.lkeys\x00\x00",
		"Enregistrer le projet", "lkeys", defaultName)
}

func (a *winApp) Mods() (bool, bool) {
	ctrl := int16(call(pGetKeyState, 0x11)) < 0
	shift := int16(call(pGetKeyState, 0x10)) < 0
	return ctrl, shift
}

func (a *winApp) SetTitle(t string) {
	if a.plugin {
		return
	}
	if a.hwnd != 0 {
		call(pSetWindowText, a.hwnd, uintptr(unsafe.Pointer(u16(t))))
	}
}

func (a *winApp) AudioAPIs() []string              { return audioAPIs() }
func (a *winApp) AudioDevices(api string) []string { return audioDevices(api) }
func (a *winApp) AudioOpen(cfg AudioCfg) (string, error) {
	if a.audio != nil {
		a.audio.Close()
		a.audio = nil
	}
	b, info, err := openAudio(a.eng, cfg)
	if err != nil {
		a.audioErr = err.Error()
		return "", err
	}
	a.audio, a.audioErr = b, ""
	return info, nil
}
func (a *winApp) AudioPanel() {
	if a.audio != nil {
		a.audio.Panel()
	}
}

func (a *winApp) Quit() {
	if a.plugin {
		return
	}
	if a.hwnd != 0 {
		call(pDestroyWindow, a.hwnd)
	}
}

// ---- dessin

func colorRef(c uint32) uintptr { return uintptr((c&0xFF)<<16 | c&0xFF00 | (c>>16)&0xFF) }

func (a *winApp) flushText(texts []TextCmd) {
	for _, t := range texts {
		u := utf16.Encode([]rune(t.S))
		if len(u) == 0 {
			continue
		}
		call(pSelectObject, a.memDC, a.fonts[t.Font])
		call(pSetTextColor, a.memDC, colorRef(t.Col))
		rc := rectT{int32(t.X), int32(t.Y), int32(t.X + t.W), int32(t.Y + t.H)}
		flags := uintptr(dtSingle | dtVCenter | dtNoPrefix | dtEndEllipsis)
		switch t.Align {
		case 1:
			flags |= dtCenter
		case 2:
			flags |= dtRight
		}
		call(pDrawText, a.memDC, ptr(&u[0]), uintptr(len(u)), ptr(&rc), flags)
		runtime.KeepAlive(u)
	}
	call(pGdiFlush)
}

func (a *winApp) setupGraphics() bool {
	hdc := call(pGetDC, a.hwnd)
	a.memDC = call(pCreateCompatibleDC, hdc)
	var bmi bitmapInfo
	bmi.hdr = bitmapInfoHeader{size: uint32(unsafe.Sizeof(bmi.hdr)), width: WinW, height: -WinH, planes: 1, bitCount: 32}
	var bits unsafe.Pointer
	a.bmp = call(pCreateDIBSection, hdc, ptr(&bmi), 0, ptr(&bits), 0, 0)
	call(pReleaseDC, a.hwnd, hdc)
	runtime.KeepAlive(&bmi)
	if a.bmp == 0 || bits == nil || a.memDC == 0 {
		return false
	}
	a.oldBmp = call(pSelectObject, a.memDC, a.bmp)
	call(pSetBkMode, a.memDC, 1) // TRANSPARENT
	a.canvas = &Canvas{W: WinW, H: WinH, Pix: unsafe.Slice((*uint32)(bits), WinW*WinH)}
	a.canvas.FlushHook = a.flushText
	face := unsafe.Pointer(u16("Segoe UI"))
	mk := func(h, w int) uintptr {
		return call(pCreateFont, i32(-h), 0, 0, 0, uintptr(w), 0, 0, 0, 1, 0, 0, 5, 0, uintptr(face))
	}
	a.fonts[fontXs] = mk(10, 700)
	a.fonts[fontSm] = mk(11, 700)
	a.fonts[fontMd] = mk(13, 400)
	a.fonts[fontMdB] = mk(13, 700)
	a.fonts[fontLg] = mk(16, 700)
	a.fonts[fontXl] = mk(24, 700)
	a.fonts[fontHg] = mk(34, 700)
	runtime.KeepAlive(face)
	return true
}

func (a *winApp) paint(hwnd uintptr) {
	var ps paintStruct
	hdc := call(pBeginPaint, hwnd, ptr(&ps))
	if a.canvas != nil {
		a.ui.Draw(a.canvas)
		if a.scale >= 0.999 {
			call(pBitBlt, hdc, 0, 0, WinW, WinH, a.memDC, 0, 0, srcCopy)
		} else {
			call(pSetStretchBltMode, hdc, 4) // HALFTONE : réduction de bonne qualité
			call(pStretchBlt, hdc, 0, 0, uintptr(a.cw), uintptr(a.ch), a.memDC, 0, 0, WinW, WinH, srcCopy)
		}
	}
	call(pEndPaint, hwnd, ptr(&ps))
}

// ---- MIDI

// fonction appelée directement par le pilote MIDI : la note part vers le moteur sans passer par l'écran
func (a *winApp) midiCallback() uintptr {
	if a.midiCb == 0 {
		a.midiCb = syscall.NewCallback(func(h, msg, inst, p1, p2 uintptr) uintptr {
			if uint32(msg) == mmMimData {
				a.ui.MidiFast(uint32(p1))
				if atomic.CompareAndSwapInt32(&a.midiPending, 0, 1) {
					call(pPostMessage, a.hwnd, wmApp+3, 0, 0)
				}
			}
			return 0
		})
	}
	return a.midiCb
}

func (a *winApp) closeMidi() {
	for _, h := range a.midiH {
		call(pMidiInReset, h)
		call(pMidiInClose, h)
	}
	a.midiH = nil
}

func sameStrings(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func (a *winApp) scanMidi(force bool) {
	n := int(call(pMidiInGetNumDevs))
	names := make([]string, n)
	for i := 0; i < n; i++ {
		var caps midiInCaps
		if call(pMidiInGetDevCaps, uintptr(i), ptr(&caps), unsafe.Sizeof(caps)) == 0 {
			names[i] = syscall.UTF16ToString(caps.name[:])
		} else {
			names[i] = fmt.Sprintf("MIDI %d", i+1)
		}
	}
	if !force && sameStrings(names, a.midiNames) {
		return
	}
	a.closeMidi()
	a.midiNames = names
	var ok, busy []string
	for i := range names {
		var h uintptr
		if call(pMidiInOpen, ptr(&h), uintptr(i), a.midiCallback(), 0, callbackFunction) == 0 {
			call(pMidiInStart, h)
			a.midiH = append(a.midiH, h)
			ok = append(ok, names[i])
		} else {
			busy = append(busy, names[i])
		}
	}
	switch {
	case n == 0:
		a.ui.SetMidiStatus(false, "Aucun clavier MIDI détecté — branche-le, puis « Actualiser MIDI »")
	case len(ok) > 0:
		s := "MIDI connecté : " + strings.Join(ok, ", ")
		if len(busy) > 0 {
			s += "  (occupé : " + strings.Join(busy, ", ") + ")"
		}
		a.ui.SetMidiStatus(true, s)
	default:
		a.ui.SetMidiStatus(false, "Clavier MIDI occupé par un autre logiciel — ferme-le puis « Actualiser MIDI »")
	}
}

// ---- messages de la fenêtre

func (a *winApp) pt(lp uintptr) (int, int) {
	x, y := float64(int16(lp&0xFFFF)), float64(int16((lp>>16)&0xFFFF))
	if a.scale < 0.999 {
		x, y = x/a.scale, y/a.scale
	}
	return int(x + 0.5), int(y + 0.5)
}

func (a *winApp) cleanup() {
	call(pKillTimer, a.hwnd, 1)
	a.closeMidi()
	if a.audio != nil {
		a.audio.Close()
	}
	a.ui.Shutdown()
	a.cleanupGraphics()
}

func (a *winApp) cleanupGraphics() {
	if a.memDC == 0 && a.bmp == 0 {
		return // déjà nettoyé
	}
	a.canvas = nil // le tampon d'image n'existe plus : plus aucun dessin possible
	if a.memDC != 0 {
		call(pSelectObject, a.memDC, a.oldBmp) // la bitmap ne doit plus être dans le DC avant d'être supprimée
	}
	for i, f := range a.fonts {
		call(pDeleteObject, f)
		a.fonts[i] = 0
	}
	call(pDeleteObject, a.bmp)
	call(pDeleteDC, a.memDC)
	a.bmp, a.memDC, a.oldBmp = 0, 0, 0
}

func (a *winApp) wndProc(hwnd, msg, wp, lp uintptr) uintptr {
	if a.hwnd == 0 && !a.plugin {
		a.hwnd = hwnd
	}
	switch uint32(msg) {
	case wmPaint:
		a.paint(hwnd)
		return 0
	case wmEraseBkgnd:
		return 1
	case wmApp + 2: // le pilote ASIO demande un redémarrage (ex. tampon modifié dans son panneau)
		a.ui.StartAudio()
		return 0
	case wmApp + 1:
		atomic.StoreInt32(&a.pending, 0)
		call(pInvalidateRect, hwnd, 0, 0)
		return 0
	case wmLButtonDown:
		if a.plugin {
			call(pSetFocus, hwnd)
		}
		call(pSetCapture, hwnd)
		x, y := a.pt(lp)
		a.ui.MouseDown(x, y)
		return 0
	case 0x0204: // clic droit : MIDI Learn
		if a.plugin {
			call(pSetFocus, hwnd)
		}
		x, y := a.pt(lp)
		a.ui.RightDown(x, y)
		return 0
	case wmMouseMove:
		x, y := a.pt(lp)
		a.ui.MouseMove(x, y, wp&mkLButton != 0)
		return 0
	case wmLButtonUp:
		call(pReleaseCapture)
		x, y := a.pt(lp)
		a.ui.MouseUp(x, y)
		return 0
	case wmMouseWheel:
		a.ui.Wheel(int(int16(wp >> 16)))
		return 0
	case wmKeyDown:
		a.ui.KeyDown(int(wp), uint32(lp>>16)&0xFF, lp&(1<<30) != 0, lp&(1<<24) != 0)
		return 0
	case wmKeyUp:
		a.ui.KeyUp(int(wp), uint32(lp>>16)&0xFF)
		return 0
	case wmChar:
		a.ui.Char(rune(wp))
		return 0
	case wmKillFocus:
		a.ui.FocusLost()
		return 0
	case wmInputLang:
		a.ui.RefreshKeyLabels()
		a.Invalidate()
	case wmTimer:
		if wp == 1 {
			if a.plugin {
				a.ui.DrainMidi()
			}
			a.ui.Tick()
			a.tick++
			if a.tick%50 == 0 && !a.plugin {
				a.scanMidi(false)
			}
		}
		return 0
	case wmApp + 4:
		if a.onUser != nil {
			a.onUser()
		}
		return 0
	case wmApp + 3: // des messages MIDI sont arrivés : on met l'écran à jour
		atomic.StoreInt32(&a.midiPending, 0)
		a.ui.DrainMidi()
		return 0
	case wmClose:
		if !a.ui.RequestClose() { // modifications non enregistrées : on demande d'abord
			return 0
		}
	case wmDestroy:
		if a.plugin {
			return 0 // le nettoyage est fait par detachEditor
		}
		a.cleanup()
		call(pPostQuitMessage, 0)
		return 0
	}
	return call(pDefWindowProc, hwnd, msg, wp, lp)
}

// fenêtres connues : l'application seule, ou une par éditeur de plugin ouvert
var (
	appsMu sync.Mutex
	apps   = map[uintptr]*winApp{}
)

func wndProcThunk(hwnd, msg, wp, lp uintptr) (ret uintptr) {
	defer func() {
		if r := recover(); r != nil && theApp == nil { // dans un DAW : jamais de plantage de la fenêtre hôte
			ret = call(pDefWindowProc, hwnd, msg, wp, lp)
		} else if r != nil {
			panic(r)
		}
	}()
	appsMu.Lock()
	a := apps[hwnd]
	appsMu.Unlock()
	if a == nil {
		a = theApp
	}
	if a == nil {
		return call(pDefWindowProc, hwnd, msg, wp, lp)
	}
	return a.wndProc(hwnd, msg, wp, lp)
}

func fatal(msg string) {
	call(pMessageBox, 0, uintptr(unsafe.Pointer(u16(msg))), uintptr(unsafe.Pointer(u16("Layers Keys"))), 0x10)
}
