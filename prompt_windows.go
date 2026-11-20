package main

// Saisie d'un nom dans le plugin VST3 (DAW) : petite fenêtre de dialogue de Windows avec une vraie case de texte.
// Les logiciels hôtes gardent les touches pour leurs raccourcis ; une boîte de dialogue modale les reçoit toujours.

import (
	"sync"
	"syscall"
	"unsafe"
)

var (
	pDialogBoxIndirect = user32.NewProc("DialogBoxIndirectParamW")
	pEndDialog         = user32.NewProc("EndDialog")
	pGetDlgItem        = user32.NewProc("GetDlgItem")
	pSetDlgItemText    = user32.NewProc("SetDlgItemTextW")
	pGetDlgItemText    = user32.NewProc("GetDlgItemTextW")
	pSendMessage       = user32.NewProc("SendMessageW")
	pGetAncestor       = user32.NewProc("GetAncestor")
)

const (
	idEdit        = 100
	wmInitDialog  = 0x0110
	wmCommand     = 0x0111
	emSetSel      = 0x00B1
	dlgIDOK       = 1
	dlgIDCancel   = 2
	dlgMaxChars   = 64
	dsSetFont     = 0x40
	dsModalFrame  = 0x80
	dsCenter      = 0x0800
	wsPopup       = 0x80000000
	wsChildDlg    = 0x40000000
	wsVisibleDlg  = 0x10000000
	wsTabStop     = 0x00010000
	wsBorder      = 0x00800000
	esAutoHScroll = 0x0080
	bsDefPush     = 0x0001
)

var (
	promptMu   sync.Mutex
	promptCb   uintptr
	promptInit string
	promptOut  string
	promptOK   bool
)

func promptProc(hdlg, msg, wp, lp uintptr) uintptr {
	switch msg {
	case wmInitDialog:
		call(pSetDlgItemText, hdlg, idEdit, uintptr(unsafe.Pointer(u16(promptInit))))
		ed := call(pGetDlgItem, hdlg, idEdit)
		call(pSendMessage, ed, emSetSel, 0, ^uintptr(0))
		call(pSetFocus, ed)
		return 0 // la case de texte a déjà le clavier
	case wmCommand:
		switch wp & 0xFFFF {
		case dlgIDOK:
			buf := make([]uint16, dlgMaxChars+1)
			call(pGetDlgItemText, hdlg, idEdit, uintptr(unsafe.Pointer(&buf[0])), uintptr(len(buf)))
			promptOut, promptOK = syscall.UTF16ToString(buf), true
			call(pEndDialog, hdlg, 1)
			return 1
		case dlgIDCancel:
			call(pEndDialog, hdlg, 0)
			return 1
		}
	}
	return 0
}

// modèle de boîte de dialogue en mémoire (DLGTEMPLATE + 4 éléments)
type dlgBuf struct{ w []uint16 }

func (b *dlgBuf) u16(v uint16) { b.w = append(b.w, v) }
func (b *dlgBuf) u32(v uint32) { b.w = append(b.w, uint16(v), uint16(v>>16)) }
func (b *dlgBuf) str(s string) {
	u, _ := syscall.UTF16FromString(s)
	b.w = append(b.w, u...)
}
func (b *dlgBuf) align() {
	if len(b.w)%2 != 0 {
		b.w = append(b.w, 0)
	}
}
func (b *dlgBuf) item(style uint32, x, y, cx, cy int16, id uint16, class uint16, text string) {
	b.align()
	b.u32(style | wsChildDlg | wsVisibleDlg)
	b.u32(0)
	b.u16(uint16(x))
	b.u16(uint16(y))
	b.u16(uint16(cx))
	b.u16(uint16(cy))
	b.u16(id)
	b.u16(0xFFFF)
	b.u16(class)
	b.str(text)
	b.u16(0) // pas de données de création
}

func (a *winApp) PromptText(title, initial string) (string, bool) {
	promptMu.Lock()
	defer promptMu.Unlock()
	if promptCb == 0 {
		promptCb = syscall.NewCallback(promptProc)
	}
	promptInit, promptOut, promptOK = initial, "", false
	b := &dlgBuf{}
	b.u32(wsPopup | wsCaption | wsSysMenu | dsModalFrame | dsSetFont | dsCenter)
	b.u32(0)
	b.u16(4) // 4 éléments
	b.u16(0)
	b.u16(0)
	b.u16(230)
	b.u16(64)
	b.u16(0) // pas de menu
	b.u16(0) // classe standard
	b.str("Layers Keys")
	b.u16(9)
	b.str("Segoe UI")
	b.item(0, 8, 7, 214, 10, 0xFFFF, 0x0082, title)                              // texte
	b.item(wsBorder|wsTabStop|esAutoHScroll, 8, 20, 214, 14, idEdit, 0x0081, "") // case de texte
	b.item(wsTabStop|bsDefPush, 118, 42, 50, 15, dlgIDOK, 0x0080, T("OK"))       // OK
	b.item(wsTabStop, 172, 42, 50, 15, dlgIDCancel, 0x0080, T("Annuler"))        // Annuler
	b.align()
	owner := a.hwnd
	if owner != 0 {
		if root := call(pGetAncestor, owner, 2); root != 0 { // GA_ROOT : la fenêtre du logiciel hôte
			owner = root
		}
	}
	inst := call(pGetModuleHandle, 0)
	call(pDialogBoxIndirect, inst, uintptr(unsafe.Pointer(&b.w[0])), owner, promptCb, 0)
	a.ui.FocusLost()
	return promptOut, promptOK
}
