//go:build windows

package main

import (
	"os"
	"path/filepath"
	"syscall"
	"unsafe"
)

// identifiant interne de Windows (MachineGuid), lu dans la vue 64 bits pour que
// l'application 32 bits, l'application 64 bits et le plugin VST3 donnent le même code
func machineIDRaw() string {
	const keyWow64 = 0x100
	const keyRead = 0x20019
	sub, _ := syscall.UTF16PtrFromString(`SOFTWARE\Microsoft\Cryptography`)
	var h syscall.Handle
	if syscall.RegOpenKeyEx(syscall.HKEY_LOCAL_MACHINE, sub, 0, keyRead|keyWow64, &h) != nil {
		return "windows-inconnu|" + os.Getenv("COMPUTERNAME")
	}
	defer syscall.RegCloseKey(h)
	name, _ := syscall.UTF16PtrFromString("MachineGuid")
	buf := make([]uint16, 128)
	n := uint32(len(buf) * 2)
	var typ uint32
	if syscall.RegQueryValueEx(h, name, nil, &typ, (*byte)(unsafe.Pointer(&buf[0])), &n) != nil {
		return "windows-inconnu|" + os.Getenv("COMPUTERNAME")
	}
	return "windows|" + syscall.UTF16ToString(buf)
}

func licenseDirs() []string {
	pd := os.Getenv("ProgramData")
	if pd == "" {
		pd = `C:\ProgramData`
	}
	return []string{filepath.Join(pd, "JB Praise", "Layers Keys"), licenseUserDir()}
}
