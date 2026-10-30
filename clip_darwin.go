//go:build darwin

package main

import "github.com/ebitengine/purego/objc"

const nsPasteType = "public.utf8-plain-text"

// ClipGet : texte du presse-papiers.
func (a *macApp) ClipGet() string {
	pb := cls("NSPasteboard").Send(sel("generalPasteboard"))
	if pb == 0 {
		return ""
	}
	return goStr(pb.Send(sel("stringForType:"), nsString(nsPasteType)))
}

// ClipSet : met un texte dans le presse-papiers.
func (a *macApp) ClipSet(s string) {
	pb := cls("NSPasteboard").Send(sel("generalPasteboard"))
	if pb == 0 {
		return
	}
	pb.Send(sel("clearContents"))
	pb.Send(sel("setString:forType:"), nsString(s), nsString(nsPasteType))
}

var _ objc.ID
