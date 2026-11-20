//go:build darwin

package main

// Saisie d'un nom dans le plugin (DAW) : vraie case de texte de macOS (NSAlert + NSTextField).
// Logic, GarageBand… ne laissent les lettres qu'à une case de texte du système : la nôtre, dessinée, ne les reçoit pas.

import "github.com/ebitengine/purego/objc"

func (a *macApp) PromptText(title, initial string) (string, bool) {
	al := cls("NSAlert").Send(sel("alloc")).Send(sel("init"))
	al.Send(sel("setMessageText:"), nsString(title))
	al.Send(sel("addButtonWithTitle:"), nsString(T("OK")))
	al.Send(sel("addButtonWithTitle:"), nsString(T("Annuler")))
	tf := cls("NSTextField").Send(sel("alloc")).Send(sel("initWithFrame:"), nsRect{Size: nsSize{W: 280, H: 24}})
	tf.Send(sel("setStringValue:"), nsString(initial))
	al.Send(sel("setAccessoryView:"), tf)
	al.Send(sel("layout"))
	if w := al.Send(sel("window")); w != 0 {
		w.Send(sel("setInitialFirstResponder:"), tf)
	}
	r := objc.Send[int](al, sel("runModal")) // 1000 = premier bouton (OK)
	s := goStr(tf.Send(sel("stringValue")))
	a.ui.FocusLost()
	return s, r == 1000
}
