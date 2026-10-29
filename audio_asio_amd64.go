//go:build windows && amd64

package main

// Sortie ASIO : charge directement le pilote ASIO de la carte son (comme Ableton, Reaper…).
// Aucune bibliothèque externe : le pilote est un objet COM déclaré dans HKLM\SOFTWARE\ASIO.

import (
	"errors"
	"fmt"
	"math"
	"sync"
	"sync/atomic"
	"syscall"
	"unsafe"
)

const asioSupported = true

var (
	advapi32         = syscall.NewLazyDLL("advapi32.dll")
	pRegOpenKeyEx    = advapi32.NewProc("RegOpenKeyExW")
	pRegEnumKeyEx    = advapi32.NewProc("RegEnumKeyExW")
	pRegQueryValueEx = advapi32.NewProc("RegQueryValueExW")
	pRegCloseKey     = advapi32.NewProc("RegCloseKey")
	asioResetHook    func() // appelé quand le pilote demande de redémarrer (changement de tampon…)
	asioWindow       uintptr
)

const hklm = 0x80000002

func regOpen(parent uintptr, path string) uintptr {
	var h uintptr
	if call(pRegOpenKeyEx, parent, uintptr(unsafe.Pointer(u16(path))), 0, 0x20019, ptr(&h)) != 0 {
		return 0
	}
	return h
}

// liste des pilotes ASIO installés : nom -> CLSID
func asioList() map[string]string {
	out := map[string]string{}
	root := regOpen(hklm, `SOFTWARE\ASIO`)
	if root == 0 {
		return out
	}
	defer call(pRegCloseKey, root)
	for i := uintptr(0); i < 128; i++ {
		var buf [256]uint16
		n := uint32(len(buf))
		if call(pRegEnumKeyEx, root, i, ptr(&buf[0]), ptr(&n), 0, 0, 0, 0) != 0 {
			break
		}
		name := syscall.UTF16ToString(buf[:n])
		k := regOpen(root, name)
		if k == 0 {
			continue
		}
		var val [128]uint16
		sz := uint32(len(val) * 2)
		var typ uint32
		if call(pRegQueryValueEx, k, uintptr(unsafe.Pointer(u16("CLSID"))), 0, ptr(&typ), ptr(&val[0]), ptr(&sz)) == 0 {
			out[name] = syscall.UTF16ToString(val[:])
		}
		call(pRegCloseKey, k)
	}
	return out
}

func asioDrivers() []string {
	var l []string
	for n := range asioList() {
		l = append(l, n)
	}
	sortStrings(l)
	return l
}

type asioBufInfo struct {
	isInput int32
	channel int32
	buf     [2]uintptr
}

type asioChanInfo struct {
	channel, isInput, isActive, group, typ int32
	name                                   [32]byte
}

type asioCallbacks struct {
	bufferSwitch, rateChanged, message, bufferSwitchTI uintptr
}

type asioOut struct {
	drv     uintptr
	name    string
	bufSize int
	rate    int
	infos   [2]asioBufInfo
	types   [2]int32
	eng     *Engine
	tmp     []int16
	cbs     *asioCallbacks
	started bool
	stop    int32
	info    string
}

var (
	asioCur atomic.Pointer[asioOut]
	cbOnce  sync.Once
	cbTable asioCallbacks
)

func asioInitCallbacks() {
	cbOnce.Do(func() {
		cbTable.bufferSwitch = syscall.NewCallback(func(index, direct uintptr) uintptr {
			defer func() { recover() }()
			if a := asioCur.Load(); a != nil && atomic.LoadInt32(&a.stop) == 0 {
				a.fill(int(index))
			}
			return 0
		})
		cbTable.rateChanged = syscall.NewCallback(func(rate uintptr) uintptr { return 0 })
		cbTable.message = syscall.NewCallback(func(selector, value, msg, opt uintptr) uintptr {
			switch int32(selector) {
			case 1: // kAsioSelectorSupported
				switch int32(value) {
				case 2, 3, 5, 6:
					return 1
				}
				return 0
			case 2: // kAsioEngineVersion
				return 2
			case 3, 4, 5, 6: // reset, changement de tampon, resynchronisation, latences
				if int32(selector) == 3 || int32(selector) == 4 {
					if h := asioResetHook; h != nil {
						h()
					}
				}
				return 1
			}
			return 0
		})
		cbTable.bufferSwitchTI = syscall.NewCallback(func(t, index, direct uintptr) uintptr {
			defer func() { recover() }()
			if a := asioCur.Load(); a != nil && atomic.LoadInt32(&a.stop) == 0 {
				a.fill(int(index))
			}
			return 0
		})
	})
}

func (a *asioOut) fill(idx int) {
	if idx < 0 || idx > 1 {
		return
	}
	n := a.bufSize
	a.eng.Render(a.tmp[:2*n])
	for c := 0; c < 2; c++ {
		asioConvert(a.infos[c].buf[idx], a.types[c], a.tmp, c, n)
	}
	com(a.drv, 23) // ASIOOutputReady
}

func asioConvert(dst uintptr, typ int32, src []int16, c, n int) {
	switch typ {
	case 16: // Int16 LSB
		b := goBytes(dst, n*2)
		for i := 0; i < n; i++ {
			v := uint16(src[2*i+c])
			b[2*i], b[2*i+1] = byte(v), byte(v>>8)
		}
	case 17: // Int24 LSB
		b := goBytes(dst, n*3)
		for i := 0; i < n; i++ {
			v := int32(src[2*i+c]) << 8
			b[3*i], b[3*i+1], b[3*i+2] = byte(v), byte(v>>8), byte(v>>16)
		}
	case 18, 24, 25, 26, 27: // Int32 LSB (pleine échelle ou 16/18/20/24 bits)
		sh := map[int32]uint{18: 16, 24: 0, 25: 2, 26: 4, 27: 8}[typ]
		b := goBytes(dst, n*4)
		for i := 0; i < n; i++ {
			v := uint32(int32(src[2*i+c]) << sh)
			b[4*i], b[4*i+1], b[4*i+2], b[4*i+3] = byte(v), byte(v>>8), byte(v>>16), byte(v>>24)
		}
	case 19: // Float32 LSB
		b := goBytes(dst, n*4)
		for i := 0; i < n; i++ {
			v := math.Float32bits(float32(src[2*i+c]) / 32768)
			b[4*i], b[4*i+1], b[4*i+2], b[4*i+3] = byte(v), byte(v>>8), byte(v>>16), byte(v>>24)
		}
	case 20: // Float64 LSB
		b := goBytes(dst, n*8)
		for i := 0; i < n; i++ {
			v := math.Float64bits(float64(src[2*i+c]) / 32768)
			for k := 0; k < 8; k++ {
				b[8*i+k] = byte(v >> (8 * uint(k)))
			}
		}
	}
}

func asioTypeOK(t int32) bool {
	switch t {
	case 16, 17, 18, 19, 20, 24, 25, 26, 27:
		return true
	}
	return false
}

func (a *asioOut) Info() string { return a.info }

func (a *asioOut) Panel() {
	if a.drv != 0 {
		com(a.drv, 21, asioWindow)
	}
}

func (a *asioOut) Close() {
	atomic.StoreInt32(&a.stop, 1)
	if a.started {
		com(a.drv, 8) // stop
	}
	asioCur.Store(nil)
	com(a.drv, 20) // disposeBuffers
	comRelease(a.drv)
}

func pickBuffer(want int, min, max, pref, gran int32) int32 {
	if want <= 0 {
		want = 128
	}
	w := int32(want)
	if min <= 0 || max < min {
		return pref
	}
	if w < min {
		w = min
	}
	if w > max {
		w = max
	}
	switch {
	case gran == 0:
		return pref
	case gran > 0:
		w = min + ((w-min+gran/2)/gran)*gran
	default: // -1 : puissances de deux
		p := min
		for p*2 <= w {
			p *= 2
		}
		w = p
	}
	if w > max {
		w = max
	}
	return w
}

func startASIO(e *Engine, cfg AudioCfg) (audioBackend, error) {
	list := asioList()
	if len(list) == 0 {
		return nil, errors.New("aucun pilote ASIO n'est installé")
	}
	name := cfg.Device
	clsidStr, ok := list[name]
	if !ok {
		names := asioDrivers()
		name = names[0]
		clsidStr = list[name]
	}
	var g guid
	if hr := uint32(call(pCLSIDFromString, uintptr(unsafe.Pointer(u16(clsidStr))), ptr(&g))); hr != 0 {
		return nil, hrErr("pilote ASIO invalide", hr)
	}
	var drv uintptr
	if hr := uint32(call(pCoCreateInstance, ptr(&g), 0, 1, ptr(&g), ptr(&drv))); hr != 0 || drv == 0 {
		return nil, hrErr("impossible de charger le pilote "+name, hr)
	}
	fail := func(msg string) (audioBackend, error) {
		var em [128]byte
		com(drv, 6, ptr(&em[0]))
		if i := indexByte(em[:], 0); i > 0 {
			msg += " : " + string(em[:i])
		}
		com(drv, 20)
		comRelease(drv)
		return nil, errors.New(msg)
	}
	if com(drv, 3, asioWindow) == 0 {
		return fail("le pilote " + name + " refuse de démarrer (carte débranchée ou utilisée par un autre logiciel ?)")
	}
	var nin, nout int32
	com(drv, 9, ptr(&nin), ptr(&nout))
	pair := cfg.Pair
	if int32(pair*2+2) > nout {
		pair = 0
	}
	if nout < 2 {
		return fail("le pilote n'a pas deux sorties")
	}
	rate := float64(cfg.Rate)
	if com(drv, 12, uintptr(math.Float64bits(rate))) != 0 {
		return fail(fmt.Sprintf("le pilote n'accepte pas %d Hz : change la fréquence dans le panneau du pilote", cfg.Rate))
	}
	if com(drv, 14, uintptr(math.Float64bits(rate))) != 0 {
		return fail("impossible de régler la fréquence du pilote")
	}
	var min, max, pref, gran int32
	com(drv, 11, ptr(&min), ptr(&max), ptr(&pref), ptr(&gran))
	size := pickBuffer(cfg.Buffer, min, max, pref, gran)
	a := &asioOut{drv: drv, name: name, bufSize: int(size), rate: cfg.Rate, eng: e}
	a.tmp = make([]int16, 2*int(size))
	for c := 0; c < 2; c++ {
		a.infos[c] = asioBufInfo{isInput: 0, channel: int32(pair*2 + c)}
	}
	asioInitCallbacks()
	a.cbs = &cbTable
	if r := int32(com(drv, 19, ptr(&a.infos[0]), 2, uintptr(size), ptr(&cbTable))); r != 0 {
		return fail("impossible de créer les tampons du pilote")
	}
	for c := 0; c < 2; c++ {
		ci := asioChanInfo{channel: int32(pair*2 + c)}
		com(drv, 18, ptr(&ci))
		a.types[c] = ci.typ
		if !asioTypeOK(ci.typ) {
			com(drv, 20)
			comRelease(drv)
			return nil, fmt.Errorf("format de sortie du pilote non pris en charge (type %d)", ci.typ)
		}
	}
	var lin, lout int32
	com(drv, 10, ptr(&lin), ptr(&lout))
	for i := range a.tmp {
		a.tmp[i] = 0
	}
	// tampons initialement silencieux
	for idx := 0; idx < 2; idx++ {
		for c := 0; c < 2; c++ {
			asioConvert(a.infos[c].buf[idx], a.types[c], a.tmp, c, int(size))
		}
	}
	asioCur.Store(a)
	if r := int32(com(drv, 7)); r != 0 {
		asioCur.Store(nil)
		com(drv, 20)
		comRelease(drv)
		return nil, errors.New("le pilote n'a pas pu démarrer")
	}
	a.started = true
	lat := float64(lout) / rate * 1000
	if lat <= 0 {
		lat = float64(size) / rate * 1000
	}
	a.info = fmt.Sprintf("ASIO · %s · %d Hz · %d éch. · %.1f ms", name, cfg.Rate, size, lat)
	return a, nil
}

func indexByte(b []byte, c byte) int {
	for i, v := range b {
		if v == c {
			return i
		}
	}
	return -1
}
