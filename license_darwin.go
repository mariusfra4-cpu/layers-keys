//go:build darwin

package main

import (
	"context"
	"os/exec"
	"regexp"
	"strings"
	"sync"
	"time"

	"github.com/ebitengine/purego"
)

// allowExec : lancer des programmes du système (ioreg…) n'est permis que dans l'application seule.
// Dans un plugin (GarageBand, Logic…), c'est interdit : cela peut bloquer le logiciel hôte.
var allowExec = false

var (
	ioOnce       sync.Once
	ioOK         bool
	ioMatching   func(name *byte) uintptr
	ioGetService func(port uint32, matching uintptr) uint32
	ioCreateProp func(entry uint32, key uintptr, alloc uintptr, opts uint32) uintptr
	ioRelease    func(obj uint32) int32
	cfNewString  func(alloc uintptr, c *byte, enc uint32) uintptr
	cfGetCStr    func(s uintptr, buf *byte, n int64, enc uint32) bool
	cfReleaseLic func(p uintptr)
)

// identifiant du Mac lu directement dans le système (IOKit), sans lancer aucun programme : même valeur que « ioreg »
func ioPlatformUUID() string {
	ioOnce.Do(func() {
		io, err := purego.Dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", purego.RTLD_LAZY|purego.RTLD_GLOBAL)
		if err != nil {
			return
		}
		cf, err := purego.Dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", purego.RTLD_LAZY|purego.RTLD_GLOBAL)
		if err != nil {
			return
		}
		purego.RegisterLibFunc(&ioMatching, io, "IOServiceMatching")
		purego.RegisterLibFunc(&ioGetService, io, "IOServiceGetMatchingService")
		purego.RegisterLibFunc(&ioCreateProp, io, "IORegistryEntryCreateCFProperty")
		purego.RegisterLibFunc(&ioRelease, io, "IOObjectRelease")
		purego.RegisterLibFunc(&cfNewString, cf, "CFStringCreateWithCString")
		purego.RegisterLibFunc(&cfGetCStr, cf, "CFStringGetCString")
		purego.RegisterLibFunc(&cfReleaseLic, cf, "CFRelease")
		ioOK = true
	})
	if !ioOK {
		return ""
	}
	name := append([]byte("IOPlatformExpertDevice"), 0)
	m := ioMatching(&name[0])
	if m == 0 {
		return ""
	}
	svc := ioGetService(0, m) // 0 = port principal par défaut ; « m » est consommé par l'appel
	if svc == 0 {
		return ""
	}
	defer ioRelease(svc)
	key := append([]byte("IOPlatformUUID"), 0)
	k := cfNewString(0, &key[0], 0x08000100)
	if k == 0 {
		return ""
	}
	defer cfReleaseLic(k)
	v := ioCreateProp(svc, k, 0, 0)
	if v == 0 {
		return ""
	}
	defer cfReleaseLic(v)
	buf := make([]byte, 128)
	if !cfGetCStr(v, &buf[0], int64(len(buf)), 0x08000100) {
		return ""
	}
	n := 0
	for n < len(buf) && buf[n] != 0 {
		n++
	}
	return strings.TrimSpace(string(buf[:n]))
}

var reIOUUID = regexp.MustCompile(`"IOPlatformUUID"\s*=\s*"([^"]+)"`)

// identifiant interne du Mac (IOPlatformUUID)
func machineIDRaw() string {
	if u := ioPlatformUUID(); u != "" {
		lklog("licence : identifiant lu par le système (sans programme externe)")
		return "mac|" + u
	}
	if !allowExec {
		lklog("licence : identifiant illisible (plugin : aucun programme externe lancé)")
		return "mac-inconnu"
	}
	// la commande a 3 secondes pour répondre : dans certains logiciels hôtes (GarageBand) elle pouvait ne jamais revenir et tout bloquer
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	out, err := exec.CommandContext(ctx, "/usr/sbin/ioreg", "-rd1", "-c", "IOPlatformExpertDevice").Output()
	lklog("licence : ioreg terminé (erreur=%v)", err)
	if err == nil {
		if m := reIOUUID.FindStringSubmatch(string(out)); len(m) == 2 {
			return "mac|" + strings.TrimSpace(m[1])
		}
	}
	ctx2, cancel2 := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel2()
	out, err = exec.CommandContext(ctx2, "/usr/sbin/system_profiler", "SPHardwareDataType").Output()
	if err == nil {
		for _, l := range strings.Split(string(out), "\n") {
			if strings.Contains(l, "Hardware UUID") {
				return "mac|" + strings.TrimSpace(l[strings.Index(l, ":")+1:])
			}
		}
	}
	return "mac-inconnu"
}

func licenseDirs() []string {
	return []string{"/Library/Application Support/JB Praise/Layers Keys", licenseUserDir()}
}
