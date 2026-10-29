//go:build darwin

package main

// Claviers MIDI du Mac (CoreMIDI) : tous les claviers branchés sont écoutés en même temps.

import (
	"fmt"
	"runtime"
	"strings"
	"unsafe"

	"github.com/ebitengine/purego"
)

var (
	midiClientCreate            func(name uintptr, notify, refcon uintptr, out *uint32) int32
	midiInputPortCreate         func(client uint32, name uintptr, proc, refcon uintptr, out *uint32) int32
	midiGetNumberOfSources      func() uintptr
	midiGetSource               func(i uintptr) uint32
	midiPortConnectSource       func(port, src uint32, ref uintptr) int32
	midiPortDispose             func(port uint32) int32
	midiObjectGetStringProperty func(obj uint32, prop uintptr, out *uintptr) int32
	kMIDIPropertyName           uintptr
)

type macMidi struct {
	ok      bool
	client  uint32
	port    uint32
	hasPort bool
	cb      uintptr
	names   []string
	running [64]byte // état « running status » de chaque source
}

func (m *macMidi) init(ui *UI) {
	defer func() { recover() }()
	purego.RegisterLibFunc(&midiClientCreate, libMIDI, "MIDIClientCreate")
	purego.RegisterLibFunc(&midiInputPortCreate, libMIDI, "MIDIInputPortCreate")
	purego.RegisterLibFunc(&midiGetNumberOfSources, libMIDI, "MIDIGetNumberOfSources")
	purego.RegisterLibFunc(&midiGetSource, libMIDI, "MIDIGetSource")
	purego.RegisterLibFunc(&midiPortConnectSource, libMIDI, "MIDIPortConnectSource")
	purego.RegisterLibFunc(&midiPortDispose, libMIDI, "MIDIPortDispose")
	purego.RegisterLibFunc(&midiObjectGetStringProperty, libMIDI, "MIDIObjectGetStringProperty")
	if p, err := purego.Dlsym(libMIDI, "kMIDIPropertyName"); err == nil && p != 0 {
		kMIDIPropertyName = peek(p)
	}
	arm := runtime.GOARCH == "arm64"
	m.cb = purego.NewCallback(func(pktlist uintptr, refcon uintptr, src uintptr) {
		defer func() { recover() }() // jamais de plantage dans le thread MIDI du système
		idx := int(src)
		if idx < 0 || idx >= len(m.running) {
			idx = 0
		}
		for _, msg := range parseMIDIPacketList(toPtr(pktlist), arm, &m.running[idx]) {
			ui.MidiFast(msg)
		}
	})
	if midiClientCreate(uintptr(nsString("Layers Keys")), 0, 0, &m.client) != 0 {
		return
	}
	m.ok = true
}

// compare la liste des claviers branchés ; reconnecte tout si elle a changé (ou si force)
func (m *macMidi) scan(ui *UI, force bool) {
	if !m.ok {
		ui.SetMidiStatus(false, "MIDI indisponible sur ce Mac")
		return
	}
	n := int(midiGetNumberOfSources())
	names := make([]string, n)
	for i := 0; i < n; i++ {
		names[i] = fmt.Sprintf("MIDI %d", i+1)
		var s uintptr
		if kMIDIPropertyName != 0 && midiObjectGetStringProperty(midiGetSource(uintptr(i)), kMIDIPropertyName, &s) == 0 && s != 0 {
			if t := cfStr(s); t != "" {
				names[i] = t
			}
			cfRelease(s)
		}
	}
	if !force && sameStrings(names, m.names) {
		return
	}
	m.names = names
	if m.hasPort {
		midiPortDispose(m.port)
		m.hasPort = false
	}
	var port uint32
	if midiInputPortCreate(m.client, uintptr(nsString("Entrée")), m.cb, 0, &port) != 0 {
		ui.SetMidiStatus(false, "MIDI indisponible sur ce Mac")
		return
	}
	m.port, m.hasPort = port, true
	var ok, busy []string
	for i := 0; i < n; i++ {
		if midiPortConnectSource(port, midiGetSource(uintptr(i)), uintptr(i)) == 0 {
			ok = append(ok, names[i])
		} else {
			busy = append(busy, names[i])
		}
	}
	switch {
	case n == 0:
		ui.SetMidiStatus(false, "Aucun clavier MIDI détecté — branche-le, puis « Actualiser MIDI »")
	case len(ok) > 0:
		s := "MIDI connecté : " + strings.Join(ok, ", ")
		if len(busy) > 0 {
			s += "  (occupé : " + strings.Join(busy, ", ") + ")"
		}
		ui.SetMidiStatus(true, s)
	default:
		ui.SetMidiStatus(false, "Clavier MIDI occupé par un autre logiciel — ferme-le puis « Actualiser MIDI »")
	}
}

func (m *macMidi) close() {
	if m.hasPort {
		midiPortDispose(m.port)
		m.hasPort = false
	}
}

var _ = unsafe.Pointer(nil)

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
