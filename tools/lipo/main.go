//go:build ignore

// Assemble un binaire Mac « universel » (Apple Silicon + Intel) à partir de deux binaires :
//
//	go run tools/lipo/main.go sortie arm64.bin amd64.bin
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
)

type slice struct {
	data     []byte
	cpu, sub uint32
	align    uint32 // puissance de 2
}

func main() {
	if len(os.Args) != 4 {
		fmt.Println("usage : lipo sortie arm64 amd64")
		os.Exit(2)
	}
	read := func(p string) []byte {
		b, err := os.ReadFile(p)
		if err != nil {
			fmt.Println(err)
			os.Exit(1)
		}
		return b
	}
	arm, amd := read(os.Args[2]), read(os.Args[3])
	check := func(b []byte, cpu uint32, name string) {
		if len(b) < 8 || binary.LittleEndian.Uint32(b) != 0xfeedfacf || binary.LittleEndian.Uint32(b[4:]) != cpu {
			fmt.Println(name, ": ce n'est pas un binaire Mac du bon type")
			os.Exit(1)
		}
	}
	check(arm, 0x0100000c, "arm64")
	check(amd, 0x01000007, "amd64")
	sl := []slice{{amd, 0x01000007, 3, 12}, {arm, 0x0100000c, 0, 14}}
	var out bytes.Buffer
	be := binary.BigEndian
	hdr := 8 + 20*len(sl)
	off := make([]uint32, len(sl))
	pos := uint32(hdr)
	for i, s := range sl {
		a := uint32(1) << s.align
		pos = (pos + a - 1) &^ (a - 1)
		off[i] = pos
		pos += uint32(len(s.data))
	}
	binary.Write(&out, be, uint32(0xCAFEBABE))
	binary.Write(&out, be, uint32(len(sl)))
	for i, s := range sl {
		binary.Write(&out, be, []uint32{s.cpu, s.sub, off[i], uint32(len(s.data)), s.align})
	}
	for i, s := range sl {
		for uint32(out.Len()) < off[i] {
			out.WriteByte(0)
		}
		out.Write(s.data)
	}
	if err := os.WriteFile(os.Args[1], out.Bytes(), 0o755); err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
}
