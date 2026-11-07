//go:build !windows

package main

import (
	"os"
	"syscall"
)

func mapFileOS(f *os.File, size int64) ([]byte, error) {
	return syscall.Mmap(int(f.Fd()), 0, int(size), syscall.PROT_READ, syscall.MAP_SHARED)
}
