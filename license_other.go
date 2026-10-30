//go:build !windows && !darwin

package main

import (
	"os"
	"path/filepath"
	"strings"
)

// (pour les tests sous Linux)
func machineIDRaw() string {
	if b, err := os.ReadFile("/etc/machine-id"); err == nil {
		return "linux|" + strings.TrimSpace(string(b))
	}
	h, _ := os.Hostname()
	return "autre|" + h
}

func licenseDirs() []string {
	return []string{filepath.Join(os.TempDir(), "JB Praise", "Layers Keys"), licenseUserDir()}
}
