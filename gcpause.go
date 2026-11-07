package main

import (
	"runtime/debug"
	"sync"
)

// Pendant le chargement d'un son (gros fichier SF2 / audio), le ramasse-miettes de Go travaille beaucoup
// et fait concurrence au thread audio : craquements. On le suspend le temps du chargement
// (avec une limite de mémoire de sécurité), puis on le remet.
var (
	gcMu    sync.Mutex
	gcBusy  int
	gcSaved = 400
)

func gcPauseBegin() {
	gcMu.Lock()
	defer gcMu.Unlock()
	if gcBusy == 0 {
		debug.SetMemoryLimit(700 << 20)
		debug.SetGCPercent(-1)
	}
	gcBusy++
}

func gcPauseEnd() {
	gcMu.Lock()
	defer gcMu.Unlock()
	gcBusy--
	if gcBusy == 0 {
		debug.SetGCPercent(gcSaved)
		debug.SetMemoryLimit(1<<63 - 1)
	}
}
