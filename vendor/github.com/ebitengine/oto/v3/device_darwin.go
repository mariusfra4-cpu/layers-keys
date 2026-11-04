// Ajout Layers Keys : choix de la carte son de sortie (AudioQueue : propriété CurrentDevice).

package oto

import (
	"errors"
	"sync"
	"unsafe"

	"github.com/ebitengine/purego"
)

const kAudioQueueProperty_CurrentDevice = 0x61716364 // 'aqcd'

var (
	devMu          sync.Mutex
	devUID         string
	devCFNew       func(alloc uintptr, cstr *byte, enc uint32) uintptr
	devCFRelease   func(cf uintptr)
	devSetProperty func(q _AudioQueueRef, id uint32, data unsafe.Pointer, size uint32) uintptr
)

func devInit() error {
	if devSetProperty != nil {
		return nil
	}
	cf, err := purego.Dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", purego.RTLD_LAZY|purego.RTLD_GLOBAL)
	if err != nil {
		return err
	}
	tb, err := purego.Dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", purego.RTLD_LAZY|purego.RTLD_GLOBAL)
	if err != nil {
		return err
	}
	purego.RegisterLibFunc(&devCFNew, cf, "CFStringCreateWithCString")
	purego.RegisterLibFunc(&devCFRelease, cf, "CFRelease")
	purego.RegisterLibFunc(&devSetProperty, tb, "AudioQueueSetProperty")
	return nil
}

// applique la carte son à la file audio (appelée avant le démarrage, ou à chaud)
func applyDevice(q _AudioQueueRef, uid string) error {
	if uid == "" || q == 0 {
		return nil
	}
	if err := devInit(); err != nil {
		return err
	}
	b := append([]byte(uid), 0)
	s := devCFNew(0, &b[0], 0x08000100) // UTF-8
	if s == 0 {
		return errors.New("oto: identifiant de carte son illisible")
	}
	defer devCFRelease(s)
	if st := devSetProperty(q, kAudioQueueProperty_CurrentDevice, unsafe.Pointer(&s), uint32(unsafe.Sizeof(s))); st != 0 {
		return errors.New("oto: AudioQueueSetProperty(CurrentDevice) a échoué")
	}
	return nil
}

// SetOutputDeviceUID choisit la carte son de sortie ("" = ne rien changer). Peut être appelée avant ou après NewContext.
func SetOutputDeviceUID(uid string) error {
	devMu.Lock()
	devUID = uid
	devMu.Unlock()
	if theContext != nil && theContext.audioQueue != 0 {
		return applyDevice(theContext.audioQueue, uid)
	}
	return nil
}

func pendingDeviceUID() string {
	devMu.Lock()
	defer devMu.Unlock()
	return devUID
}
