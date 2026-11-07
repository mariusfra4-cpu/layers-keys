package main

import (
	"runtime"
	"testing"
)

func bigBank(frames int) []byte {
	samples := []tSample{{name: "BIG", data: sine(220, 44000, frames, 0.7), rate: 44000, pitch: 57, loopS: 1000, loopE: uint32(frames - 1000)}}
	insts := []tInst{{"I", []tZone{{gAmt(54, 1)}, {gAmt(53, 0)}}}}
	presets := []tPreset{{"Big", 0, 0, []tZone{{gAmt(41, 0)}}}}
	return buildSF2(2, samples, insts, presets)
}

// Le chargement par projection mémoire donne exactement les mêmes échantillons que la lecture classique,
// sans réserver de mémoire pour eux.
func TestSF2MapMatchesCopy(t *testing.T) {
	path := writeTmp(t, bigBank(20_000_000)) // ≈ 40 Mo
	load := func(forceCopy bool) ([]*Region, uint64) {
		sf, err := OpenSF2(path)
		if err != nil {
			t.Fatal(err)
		}
		if forceCopy {
			sf.mapOnce.Do(func() {})
		}
		var m0, m1 runtime.MemStats
		runtime.GC()
		runtime.ReadMemStats(&m0)
		regs, err := sf.LoadPreset(0)
		if err != nil {
			t.Fatal(err)
		}
		runtime.ReadMemStats(&m1)
		if forceCopy == (sf.mm != nil) {
			t.Fatalf("mode inattendu (copie forcée=%v, projeté=%v)", forceCopy, sf.mm != nil)
		}
		return regs, m1.TotalAlloc - m0.TotalAlloc
	}
	a, allocMap := load(false)
	b, allocCopy := load(true)
	if len(a) != len(b) || len(a[0].Data) != len(b[0].Data) {
		t.Fatalf("tailles différentes : %d / %d", len(a[0].Data), len(b[0].Data))
	}
	for i := range a[0].Data {
		if a[0].Data[i] != b[0].Data[i] {
			t.Fatalf("échantillon %d différent", i)
		}
	}
	t.Logf("mémoire réservée : projeté %d Ko, copie %d Ko", allocMap/1024, allocCopy/1024)
	if allocMap > 2<<20 {
		t.Errorf("la projection ne devrait presque rien réserver, a réservé %d Ko", allocMap/1024)
	}
}
