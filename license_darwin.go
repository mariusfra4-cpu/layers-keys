//go:build darwin

package main

import (
	"os/exec"
	"regexp"
	"strings"
)

var reIOUUID = regexp.MustCompile(`"IOPlatformUUID"\s*=\s*"([^"]+)"`)

// identifiant interne du Mac (IOPlatformUUID)
func machineIDRaw() string {
	out, err := exec.Command("/usr/sbin/ioreg", "-rd1", "-c", "IOPlatformExpertDevice").Output()
	if err == nil {
		if m := reIOUUID.FindStringSubmatch(string(out)); len(m) == 2 {
			return "mac|" + strings.TrimSpace(m[1])
		}
	}
	out, err = exec.Command("/usr/sbin/system_profiler", "SPHardwareDataType").Output()
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
