import Foundation

/// Ce que chaque commande envoie : mêmes numéros que la version Android (android-remote/.../MidiMap.java)
/// et que Layers Keys (remote.go). Numéros libres de la norme MIDI (ni banque, ni molette, ni volume, ni pédales).
///
///   Volume piste 1-8 : CC 20-27      Muet 1-8 : CC 102-109    Solo 1-8 : CC 110-117    Hold 1-8 : CC 70-77
///   Volume général : CC 28           Réverbération : CC 29    Leslie lent / rapide : CC 30
///   Sustain : CC 64                  Presets 1-8 : Program Change 0-7
///   Tout arrêter : CC 64 = 0, CC 120, CC 123 sur les 16 canaux
enum MidiMap {
    static let tracks = 8
    static let VOL_BASE = 20
    static let MUTE_BASE = 102
    static let SOLO_BASE = 110
    static let HOLD_BASE = 70
    static let MASTER = 28
    static let REVERB = 29
    static let LESLIE = 30
    static let SUSTAIN = 64
    static let presets = 8

    static func clamp7(_ v: Int) -> UInt8 { UInt8(max(0, min(127, v))) }

    static func cc(_ channel: Int, _ cc: Int, _ value: Int) -> [UInt8] {
        [UInt8(0xB0 | (channel & 15)), UInt8(cc & 0x7F), clamp7(value)]
    }

    static func program(_ channel: Int, _ prog: Int) -> [UInt8] {
        [UInt8(0xC0 | (channel & 15)), UInt8(prog & 0x7F)]
    }

    static func panic() -> [UInt8] {
        var out: [UInt8] = []
        for ch in 0..<16 {
            let st = UInt8(0xB0 | ch)
            out += [st, 64, 0, st, 120, 0, st, 123, 0]
        }
        return out
    }

    static func faderValue(_ pos: Double) -> Int {
        if pos <= 0 { return 0 }
        if pos >= 1 { return 127 }
        return Int((pos * 127).rounded())
    }

    /// les 35 CC des pistes et réglages (sans le sustain)
    static func allControls() -> [Int] {
        var out: [Int] = []
        for i in 0..<tracks { out += [VOL_BASE + i, MUTE_BASE + i, SOLO_BASE + i, HOLD_BASE + i] }
        return out + [MASTER, REVERB, LESLIE]
    }
}
