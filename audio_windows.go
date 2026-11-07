//go:build windows

package main

// Sorties audio Windows : WASAPI (moderne), MME (compatibilité) ; ASIO est dans audio_asio_*.go.

import (
	"errors"
	"fmt"
	"runtime"
	"sort"
	"sync/atomic"
	"syscall"
	"time"
	"unicode/utf16"
	"unsafe"
)

// une sortie audio ouverte
type audioBackend interface {
	Close()
	Info() string // ex. « WASAPI · Haut-parleurs · 48000 Hz · 10,0 ms »
	Panel()       // panneau de réglages du pilote (ASIO), sinon rien
}

var (
	avrt               = syscall.NewLazyDLL("avrt.dll")
	pAvSetMmThreadChar = avrt.NewProc("AvSetMmThreadCharacteristicsW")
	pSetThreadPriority = kernel32.NewProc("SetThreadPriority")
	pGetCurrentThread  = kernel32.NewProc("GetCurrentThread")
	pCoCreateInstance  = ole32.NewProc("CoCreateInstance")
	pCoTaskMemFree     = ole32.NewProc("CoTaskMemFree")
	pPropVariantClear  = ole32.NewProc("PropVariantClear")
	pSetEvent          = kernel32.NewProc("SetEvent")
	pCloseHandle       = kernel32.NewProc("CloseHandle")
	pCLSIDFromString   = ole32.NewProc("CLSIDFromString")
)

// met le thread audio en priorité maximale (évite les craquements)
func boostThread() {
	runtime.LockOSThread()
	var idx uint32
	call(pAvSetMmThreadChar, uintptr(unsafe.Pointer(u16("Pro Audio"))), ptr(&idx))
	call(pSetThreadPriority, call(pGetCurrentThread), 15) // THREAD_PRIORITY_TIME_CRITICAL
}

// ------------------------------------------------------------------ COM minimal

type guid struct {
	d1 uint32
	d2 uint16
	d3 uint16
	d4 [8]byte
}

type propKey struct {
	fmtid guid
	pid   uint32
}

var (
	clsidMMDeviceEnumerator = guid{0xBCDE0395, 0xE52F, 0x467C, [8]byte{0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}}
	iidMMDeviceEnumerator   = guid{0xA95664D2, 0x9614, 0x4F35, [8]byte{0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}}
	iidAudioClient          = guid{0x1CB9AD4C, 0xDBFA, 0x4C32, [8]byte{0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}}
	iidAudioRenderClient    = guid{0xF294ACFC, 0x3146, 0x4483, [8]byte{0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}}
	pkeyFriendlyName        = propKey{guid{0xA45C254E, 0xDF1C, 0x4EFD, [8]byte{0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14}
)

const ptrSize = unsafe.Sizeof(uintptr(0))

// lit un mot (pointeur) à l'adresse a
func toPtr(a uintptr) unsafe.Pointer { return *(*unsafe.Pointer)(unsafe.Pointer(&a)) }

func peek(a uintptr) uintptr { return *(*uintptr)(toPtr(a)) }

func goBytes(a uintptr, n int) []byte {
	return unsafe.Slice((*byte)(toPtr(a)), n)
}

func goInt16s(a uintptr, n int) []int16 {
	return unsafe.Slice((*int16)(toPtr(a)), n)
}

// appelle la méthode n°idx d'un objet COM
func com(obj uintptr, idx int, args ...uintptr) uint32 {
	fn := peek(peek(obj) + uintptr(idx)*ptrSize)
	r, _, _ := syscall.SyscallN(fn, append([]uintptr{obj}, args...)...)
	return uint32(r)
}

func comRelease(obj uintptr) {
	if obj != 0 {
		com(obj, 2)
	}
}

// entier 64 bits en arguments d'appel (1 mot en 64 bits, 2 mots en 32 bits)
func i64args(v int64) []uintptr {
	if ptrSize == 8 {
		return []uintptr{uintptr(v)}
	}
	return []uintptr{uintptr(uint32(v)), uintptr(uint32(uint64(v) >> 32))}
}

func hrErr(what string, hr uint32) error {
	return fmt.Errorf("%s (0x%08X)", what, hr)
}

func readWide(p uintptr) string {
	if p == 0 {
		return ""
	}
	var w []uint16
	for i := uintptr(0); i < 1024; i++ {
		c := *(*uint16)(toPtr(p + i*2))
		if c == 0 {
			break
		}
		w = append(w, c)
	}
	return string(utf16.Decode(w))
}

// ------------------------------------------------------------------ WASAPI : liste des cartes

func newEnumerator() (uintptr, error) {
	var en uintptr
	hr := uint32(call(pCoCreateInstance, ptr(&clsidMMDeviceEnumerator), 0, 23, ptr(&iidMMDeviceEnumerator), ptr(&en)))
	if hr != 0 || en == 0 {
		return 0, hrErr("énumérateur audio", hr)
	}
	return en, nil
}

func deviceName(dev uintptr) string {
	var store uintptr
	if com(dev, 4, 0, ptr(&store)) != 0 || store == 0 { // OpenPropertyStore(STGM_READ)
		return ""
	}
	defer comRelease(store)
	var pv [4]uint64 // PROPVARIANT (24 octets en 64 bits)
	if com(store, 5, ptr(&pkeyFriendlyName), ptr(&pv[0])) != 0 {
		return ""
	}
	defer call(pPropVariantClear, ptr(&pv[0]))
	if uint16(pv[0]) != 31 { // VT_LPWSTR
		return ""
	}
	return readWide(uintptr(pv[1]))
}

// noms des cartes de sortie actives
func wasapiDevices() []string {
	en, err := newEnumerator()
	if err != nil {
		return nil
	}
	defer comRelease(en)
	var coll uintptr
	if com(en, 3, 0, 1, ptr(&coll)) != 0 || coll == 0 { // eRender, DEVICE_STATE_ACTIVE
		return nil
	}
	defer comRelease(coll)
	var n uint32
	com(coll, 3, ptr(&n))
	var out []string
	for i := uint32(0); i < n && i < 64; i++ {
		var dev uintptr
		if com(coll, 4, uintptr(i), ptr(&dev)) == 0 && dev != 0 {
			if nm := deviceName(dev); nm != "" {
				out = append(out, nm)
			}
			comRelease(dev)
		}
	}
	return out
}

// retrouve une carte par son nom (ou la carte par défaut de Windows)
func findDevice(en uintptr, name string) (uintptr, string, error) {
	if name != "" {
		var coll uintptr
		if com(en, 3, 0, 1, ptr(&coll)) == 0 && coll != 0 {
			defer comRelease(coll)
			var n uint32
			com(coll, 3, ptr(&n))
			for i := uint32(0); i < n && i < 64; i++ {
				var dev uintptr
				if com(coll, 4, uintptr(i), ptr(&dev)) == 0 && dev != 0 {
					if deviceName(dev) == name {
						return dev, name, nil
					}
					comRelease(dev)
				}
			}
		}
	}
	var dev uintptr
	if hr := com(en, 4, 0, 0, ptr(&dev)); hr != 0 || dev == 0 { // GetDefaultAudioEndpoint(eRender, eConsole)
		return 0, "", hrErr("aucune sortie audio trouvée", hr)
	}
	return dev, deviceName(dev), nil
}

// ------------------------------------------------------------------ WASAPI : lecture

type wasapiOut struct {
	client, render, event uintptr
	frames                uint32
	stop                  int32
	done                  chan struct{}
	info                  string
}

func (w *wasapiOut) Info() string { return w.info }
func (w *wasapiOut) Panel()       {}

func (w *wasapiOut) Close() {
	atomic.StoreInt32(&w.stop, 1)
	call(pSetEvent, w.event)
	select {
	case <-w.done:
	case <-time_after(1500):
	}
	if w.client != 0 {
		com(w.client, 11) // Stop
		comRelease(w.render)
		comRelease(w.client)
	}
	call(pCloseHandle, w.event)
}

type waveFormatEx struct {
	tag            uint16
	channels       uint16
	samplesPerSec  uint32
	avgBytesPerSec uint32
	blockAlign     uint16
	bitsPerSample  uint16
	cbSize         uint16
}

func startWASAPI(e *Engine, devName string, rate, bufFrames int, exclusive bool) (*wasapiOut, error) {
	en, err := newEnumerator()
	if err != nil {
		return nil, err
	}
	defer comRelease(en)
	dev, name, err := findDevice(en, devName)
	if err != nil {
		return nil, err
	}
	defer comRelease(dev)

	wf := waveFormatEx{tag: 1, channels: 2, samplesPerSec: uint32(rate), avgBytesPerSec: uint32(rate) * 4, blockAlign: 4, bitsPerSample: 16}
	activate := func() (uintptr, error) {
		var c uintptr
		if hr := com(dev, 3, ptr(&iidAudioClient), 23, 0, ptr(&c)); hr != 0 || c == 0 {
			return 0, hrErr("ouverture de la carte son", hr)
		}
		return c, nil
	}
	initialize := func(c uintptr, dur int64) uint32 {
		if exclusive {
			args := []uintptr{1, 0x00040000} // EXCLUSIVE, EVENTCALLBACK
			args = append(args, i64args(dur)...)
			args = append(args, i64args(dur)...)
			args = append(args, ptr(&wf), 0)
			return com(c, 3, args...)
		}
		// partagé : Windows convertit si besoin la fréquence de la carte
		// tampon confortable de 40 ms : une petite pause de Windows ou du programme ne coupe plus le son
		args := []uintptr{0, 0x00040000 | 0x80000000 | 0x08000000}
		args = append(args, i64args(400000)...)
		args = append(args, i64args(0)...)
		args = append(args, ptr(&wf), 0)
		return com(c, 3, args...)
	}
	client, err := activate()
	if err != nil {
		return nil, err
	}
	dur := int64(0)
	if exclusive {
		if bufFrames < 32 {
			bufFrames = 256 // 5 ms : marge sûre avec 8 pistes actives
		}
		dur = int64(bufFrames) * 10000000 / int64(rate)
	}
	hr := initialize(client, dur)
	if hr == 0x88890019 && exclusive { // taille de tampon non alignée : on demande la taille exacte
		var fr uint32
		com(client, 4, ptr(&fr))
		comRelease(client)
		if client, err = activate(); err != nil {
			return nil, err
		}
		dur = int64(float64(10000000)/float64(rate)*float64(fr) + 0.5)
		hr = initialize(client, dur)
	}
	if hr != 0 {
		comRelease(client)
		switch hr {
		case 0x88890008:
			return nil, errors.New("cette carte n'accepte pas cette fréquence ou ce format")
		case 0x8889000A:
			return nil, errors.New("la carte est utilisée par un autre logiciel")
		}
		return nil, hrErr("initialisation de la carte son", hr)
	}
	w := &wasapiOut{client: client, done: make(chan struct{})}
	w.event = call(pCreateEvent, 0, 0, 0, 0)
	if hr = com(client, 13, w.event); hr != 0 {
		comRelease(client)
		return nil, hrErr("événement audio", hr)
	}
	com(client, 4, ptr(&w.frames))
	if hr = com(client, 14, ptr(&iidAudioRenderClient), ptr(&w.render)); hr != 0 || w.render == 0 {
		comRelease(client)
		return nil, hrErr("service de lecture", hr)
	}
	// silence initial puis démarrage
	var p uintptr
	if com(w.render, 3, uintptr(w.frames), ptr(&p)) == 0 {
		com(w.render, 4, uintptr(w.frames), 2) // AUDCLNT_BUFFERFLAGS_SILENT
	}
	if hr = com(client, 10); hr != 0 {
		comRelease(w.render)
		comRelease(client)
		return nil, hrErr("démarrage de la carte son", hr)
	}
	mode := "WASAPI"
	if exclusive {
		mode = "WASAPI exclusif"
	}
	lat := float64(w.frames) / float64(rate) * 1000
	if !exclusive {
		var l int64
		com(client, 5, ptr(&l))
		lat += float64(l) / 10000
	}
	w.info = fmt.Sprintf("%s · %s · %d Hz · %.1f ms", mode, name, rate, lat)
	go w.loop(e, exclusive)
	return w, nil
}

func (w *wasapiOut) loop(e *Engine, exclusive bool) {
	defer close(w.done)
	boostThread()
	call(pCoInitializeEx, 0, 0)
	warm := make([]int16, 512) // préchauffage : alloue les tampons avant le premier vrai bloc
	e.Render(warm)
	for atomic.LoadInt32(&w.stop) == 0 {
		if r := call(pWaitForSingleObject, w.event, 1000); r != 0 {
			continue
		}
		if atomic.LoadInt32(&w.stop) != 0 {
			return
		}
		n := w.frames
		if !exclusive {
			var pad uint32
			if com(w.client, 6, ptr(&pad)) != 0 {
				continue
			}
			n = w.frames - pad
		}
		if n == 0 {
			continue
		}
		var p uintptr
		if com(w.render, 3, uintptr(n), ptr(&p)) != 0 || p == 0 {
			continue
		}
		buf := goInt16s(p, int(n)*2)
		for off := 0; off < len(buf); off += 512 { // morceaux de 256 images : durée de calcul régulière
			end := off + 512
			if end > len(buf) {
				end = len(buf)
			}
			e.Render(buf[off:end])
		}
		com(w.render, 4, uintptr(n), 0)
	}
}

// ------------------------------------------------------------------ MME (ancien système, secours)

type mmeOut struct {
	hwo   uintptr
	event uintptr
	hdrs  [audioBufs]waveHdr
	bufs  [audioBufs][]int16
	stop  int32
	done  chan struct{}
	info  string
}

const (
	audioFrames = 256
	audioBufs   = 4
)

func (a *mmeOut) Info() string { return a.info }
func (a *mmeOut) Panel()       {}

func startMME(e *Engine, rate, frames int) (*mmeOut, error) {
	if frames < 64 {
		frames = audioFrames
	}
	a := &mmeOut{done: make(chan struct{})}
	a.event = call(pCreateEvent, 0, 0, 0, 0)
	f := waveFormat{tag: 1, channels: 2, samplesPerSec: uint32(rate), avgBytesPerSec: uint32(rate) * 4, blockAlign: 4, bitsPerSample: 16}
	r := call(pWaveOutOpen, ptr(&a.hwo), uintptr(waveMapper), ptr(&f), a.event, 0, callbackEvent)
	runtime.KeepAlive(&f)
	if r != 0 {
		return nil, fmt.Errorf("code %d", r)
	}
	for i := range a.hdrs {
		a.bufs[i] = make([]int16, frames*2)
		h := &a.hdrs[i]
		h.data = ptr(&a.bufs[i][0])
		h.length = uint32(len(a.bufs[i]) * 2)
		call(pWaveOutPrepareHeader, a.hwo, ptr(h), unsafe.Sizeof(*h))
		call(pWaveOutWrite, a.hwo, ptr(h), unsafe.Sizeof(*h))
	}
	a.info = fmt.Sprintf("MME · %d Hz · %.0f ms", rate, float64(frames*audioBufs)/float64(rate)*1000)
	go func() {
		defer close(a.done)
		boostThread()
		for atomic.LoadInt32(&a.stop) == 0 {
			call(pWaitForSingleObject, a.event, 100)
			for i := range a.hdrs {
				h := &a.hdrs[i]
				if atomic.LoadUint32(&h.flags)&whdrDone != 0 && atomic.LoadInt32(&a.stop) == 0 {
					e.Render(a.bufs[i])
					call(pWaveOutWrite, a.hwo, ptr(h), unsafe.Sizeof(*h))
				}
			}
		}
	}()
	return a, nil
}

func (a *mmeOut) Close() {
	atomic.StoreInt32(&a.stop, 1)
	call(pSetEvent, a.event)
	select {
	case <-a.done:
	case <-time_after(1500):
	}
	call(pWaveOutReset, a.hwo)
	for i := range a.hdrs {
		call(pWaveOutUnprepare, a.hwo, ptr(&a.hdrs[i]), unsafe.Sizeof(a.hdrs[i]))
	}
	call(pWaveOutClose, a.hwo)
	call(pCloseHandle, a.event)
}

// ------------------------------------------------------------------ choix automatique + secours

// ouvre la sortie demandée ; en cas d'échec essaie WASAPI puis MME. Renvoie le texte d'état.
func openAudio(e *Engine, cfg AudioCfg) (audioBackend, string, error) {
	cfg.sanitize()
	e.SetSampleRate(cfg.Rate)
	var tried []string
	try := func(api string) (audioBackend, error) {
		switch api {
		case apiASIO:
			return startASIO(e, cfg)
		case apiWASAPX:
			return startWASAPI(e, cfg.Device, cfg.Rate, cfg.Buffer, true)
		case apiMME:
			return startMME(e, cfg.Rate, cfg.Buffer)
		}
		return startWASAPI(e, cfg.Device, cfg.Rate, cfg.Buffer, false)
	}
	order := []string{cfg.API}
	for _, f := range []string{apiWASAPI, apiMME} {
		if f != cfg.API && !(cfg.API == apiAuto && f == apiWASAPI) {
			order = append(order, f)
		}
	}
	for i, api := range order {
		b, err := safeOpen(func() (audioBackend, error) { return try(api) })
		if err == nil {
			s := b.Info()
			if i > 0 && len(tried) > 0 {
				s += "  (secours : " + tried[0] + ")"
			}
			return b, s, nil
		}
		tried = append(tried, apiLabel(api)+" : "+err.Error())
	}
	return nil, "", errors.New(tried[len(tried)-1])
}

// un pilote qui plante (panique Go) ne doit pas fermer le logiciel
func safeOpen(f func() (audioBackend, error)) (b audioBackend, err error) {
	defer func() {
		if r := recover(); r != nil {
			b, err = nil, fmt.Errorf("erreur du pilote (%v)", r)
		}
	}()
	return f()
}

func audioAPIs() []string {
	l := []string{apiWASAPI, apiWASAPX}
	if asioSupported {
		l = append([]string{apiASIO}, l...)
	}
	return append(l, apiMME)
}

func audioDevices(api string) []string {
	switch api {
	case apiASIO:
		return asioDrivers()
	case apiMME:
		return nil
	}
	d := wasapiDevices()
	sort.Strings(d)
	return d
}

func time_after(ms int) <-chan time.Time { return time.After(time.Duration(ms) * time.Millisecond) }
