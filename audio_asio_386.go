//go:build windows && 386

package main

import "errors"

// Version 32 bits : ASIO n'est disponible que dans la version 64 bits.
const asioSupported = false

var asioResetHook func()
var asioWindow uintptr

func asioDrivers() []string { return nil }

func startASIO(e *Engine, cfg AudioCfg) (audioBackend, error) {
	return nil, errors.New("ASIO demande la version 64 bits du logiciel")
}
