//go:build darwin

package main

import (
	"strings"
	"syscall"
)

// version de macOS lue dans le système, sans lancer de programme externe
func macVersionSysctl() string {
	v, err := syscall.Sysctl("kern.osproductversion")
	if err != nil || strings.TrimSpace(v) == "" {
		return "?"
	}
	return strings.TrimSpace(v)
}
