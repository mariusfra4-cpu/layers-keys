//go:build darwin

package main

import (
	"context"
	"os/exec"
	"regexp"
	"strings"
	"time"
)

var reIOUUID = regexp.MustCompile(`"IOPlatformUUID"\s*=\s*"([^"]+)"`)

// identifiant interne du Mac (IOPlatformUUID)
func machineIDRaw() string {
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
