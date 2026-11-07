package main

import (
	"errors"
	"os"
	"syscall"
	"unsafe"
)

var (
	k32mm           = syscall.NewLazyDLL("kernel32.dll")
	pCreateFileMapW = k32mm.NewProc("CreateFileMappingW")
	pMapViewOfFile  = k32mm.NewProc("MapViewOfFile")
)

func mapFileOS(f *os.File, size int64) ([]byte, error) {
	if uint64(size) > uint64(^uintptr(0)>>1) {
		return nil, errors.New("fichier trop grand pour cette version")
	}
	h, _, _ := pCreateFileMapW.Call(f.Fd(), 0, 0x02 /* PAGE_READONLY */, 0, 0, 0)
	if h == 0 {
		return nil, errors.New("projection impossible")
	}
	defer syscall.CloseHandle(syscall.Handle(h)) // la vue garde le fichier ouvert pour le système
	addr, _, _ := pMapViewOfFile.Call(h, 0x04 /* FILE_MAP_READ */, 0, 0, 0)
	if addr == 0 {
		return nil, errors.New("projection impossible (adresses insuffisantes)")
	}
	return unsafe.Slice((*byte)(unsafe.Pointer(addr)), int(size)), nil
}
