//go:build darwin && vst3 && !au

package main

// (le diagnostic du son n'existe que dans l'Audio Unit)
func auDiagPeak(l, r []float32) {}
func auDiagNote()               {}
func auDiagBegin()              {}
