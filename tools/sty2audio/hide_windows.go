//go:build windows

package main

import (
	"os/exec"
	"syscall"
)

// hideWindow : pas de fenêtre noire supplémentaire pour ffmpeg.
func hideWindow(c *exec.Cmd) {
	c.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
}
