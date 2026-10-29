package main

import (
	"encoding/binary"
	"testing"
	"unsafe"
)

// fabrique une MIDIPacketList comme CoreMIDI le ferait
func fakePacketList(packets [][]byte, alignArm bool) []uint32 {
	buf := make([]uint32, 1024) // aligné sur 4 octets
	b := unsafe.Slice((*byte)(unsafe.Pointer(&buf[0])), len(buf)*4)
	binary.LittleEndian.PutUint32(b[0:], uint32(len(packets)))
	off := 4
	for _, p := range packets {
		binary.LittleEndian.PutUint64(b[off:], 12345)
		binary.LittleEndian.PutUint16(b[off+8:], uint16(len(p)))
		copy(b[off+10:], p)
		next := off + 10 + len(p)
		if alignArm {
			next = (next + 3) &^ 3
		}
		off = next
	}
	return buf
}

func TestMIDIPacketList(t *testing.T) {
	for _, arm := range []bool{false, true} {
		buf := fakePacketList([][]byte{
			{0x90, 60, 100},
			{0xB0, 74, 20, 0xB0, 74, 21},        // deux messages dans un paquet
			{0xE0, 0x00, 0x40, 0xF8},            // pitch bend + horloge ignorée
			{0xD0, 50},                          // aftertouch de canal (1 octet de donnée)
			{0x80, 60, 0, 61, 0},                // running status
			{0xF0, 1, 2, 3, 0xF7, 0x90, 62, 90}, // sysex ignoré puis une note
		}, arm)
		var running byte
		got := parseMIDIPacketList(unsafe.Pointer(&buf[0]), arm, &running)
		want := []uint32{
			0x90 | 60<<8 | 100<<16,
			0xB0 | 74<<8 | 20<<16,
			0xB0 | 74<<8 | 21<<16,
			0xE0 | 0<<8 | 0x40<<16,
			0xD0 | 50<<8,
			0x80 | 60<<8,
			0x80 | 61<<8,
			0x90 | 62<<8 | 90<<16,
		}
		if len(got) != len(want) {
			t.Fatalf("arm=%v : %d messages (attendu %d) : %x", arm, len(got), len(want), got)
		}
		for i := range want {
			if got[i] != want[i] {
				t.Fatalf("arm=%v message %d : %#x (attendu %#x)", arm, i, got[i], want[i])
			}
		}
	}
	var r byte
	if parseMIDIPacketList(nil, false, &r) != nil {
		t.Fatal("liste vide")
	}
}
