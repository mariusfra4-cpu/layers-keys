package main

// Lecture des paquets MIDI de CoreMIDI (Mac) : MIDIPacketList → messages « statut | donnée1<<8 | donnée2<<16 ».
// Ce code ne dépend pas du Mac : il est testé sur n'importe quelle machine.

import "unsafe"

// midiBytesToMsgs découpe un flux d'octets MIDI en messages de canal (notes, CC, pitch bend, aftertouch…).
// Gère le « running status », ignore l'horloge temps réel et les messages système exclusifs.
func midiBytesToMsgs(b []byte, running *byte) []uint32 {
	var out []uint32
	for i := 0; i < len(b); {
		c := b[i]
		switch {
		case c >= 0xF8: // temps réel (horloge…) : ignoré
			i++
		case c == 0xF0: // système exclusif : on saute jusqu'à F7
			for i < len(b) && b[i] != 0xF7 {
				i++
			}
			i++
			*running = 0
		case c >= 0xF1 && c <= 0xF7: // autres messages système
			*running = 0
			i++
		case c >= 0x80: // octet de statut
			*running = c
			i++
			n := midiDataLen(c)
			if i+n > len(b) {
				return out
			}
			out = append(out, midiMsg(c, b[i:i+n]))
			i += n
		default: // donnée sans statut : running status
			if *running == 0 {
				i++
				continue
			}
			n := midiDataLen(*running)
			if i+n > len(b) {
				return out
			}
			out = append(out, midiMsg(*running, b[i:i+n]))
			i += n
		}
	}
	return out
}

func midiDataLen(status byte) int {
	switch status & 0xF0 {
	case 0xC0, 0xD0:
		return 1
	}
	return 2
}

func midiMsg(status byte, d []byte) uint32 {
	m := uint32(status)
	if len(d) > 0 {
		m |= uint32(d[0]&0x7F) << 8
	}
	if len(d) > 1 {
		m |= uint32(d[1]&0x7F) << 16
	}
	return m
}

// parseMIDIPacketList lit une MIDIPacketList de CoreMIDI :
//
//	UInt32 numPackets, puis des paquets { UInt64 timeStamp; UInt16 length; Byte data[length] } sans trous (pack 4).
//
// Sur les processeurs ARM, CoreMIDI aligne chaque paquet sur 4 octets (MIDIPacketNext).
func parseMIDIPacketList(base unsafe.Pointer, alignArm bool, running *byte) []uint32 {
	if base == nil {
		return nil
	}
	n := int(*(*uint32)(base))
	if n <= 0 || n > 4096 {
		return nil
	}
	var out []uint32
	off := uintptr(4)
	for k := 0; k < n; k++ {
		p := unsafe.Add(base, off)
		length := int(uint16(*(*byte)(unsafe.Add(p, 8))) | uint16(*(*byte)(unsafe.Add(p, 9)))<<8)
		if length > 256 {
			break
		}
		data := unsafe.Slice((*byte)(unsafe.Add(p, 10)), length)
		out = append(out, midiBytesToMsgs(data, running)...)
		next := uintptr(p) + 10 + uintptr(length)
		if alignArm {
			next = (next + 3) &^ 3
		}
		off += next - uintptr(p)
	}
	return out
}
