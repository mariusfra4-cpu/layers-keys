package main

// Diagnostic : pendant l'utilisation, on note dans le journal (jamais depuis le thread audio) les moments où le
// calcul du son a failli être en retard, avec la mémoire utilisée et les chargements en cours.
// Il n'y a que des chiffres techniques. Le journal est celui de lklog (voir lklog.go).

import (
	"runtime"
	"sync"
	"sync/atomic"
	"time"
)

var diagOnce sync.Once
var loadsNow int32

func startDiag(e *Engine) {
	diagOnce.Do(func() {
		go func() {
			var lastGC uint32
			for {
				time.Sleep(2 * time.Second)
				n := atomic.SwapInt64(&e.slowN, 0)
				mx := atomic.SwapUint64(&e.slowMax, 0)
				if n == 0 {
					continue
				}
				var m runtime.MemStats
				runtime.ReadMemStats(&m)
				lklog("diagnostic : %d bloc(s) audio lent(s) en 2 s, le pire à %d %% du temps disponible · chargements en cours : %d · mémoire du programme %d Mo · nettoyages mémoire +%d",
					n, mx/10, atomic.LoadInt32(&loadsNow), m.HeapAlloc>>20, m.NumGC-lastGC)
				lastGC = m.NumGC
			}
		}()
	})
}
