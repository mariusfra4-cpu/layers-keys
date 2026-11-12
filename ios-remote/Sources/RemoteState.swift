import Foundation

/// État de la télécommande et messages envoyés à chaque geste (même logique que RemoteState.java sur Android).
/// Sans UIKit : testé sur Mac par Tests/LogicTests.swift.
final class RemoteState {
    var vol = [Double](repeating: 0.8, count: MidiMap.tracks)
    var mute = [Bool](repeating: false, count: MidiMap.tracks)
    var solo = [Bool](repeating: false, count: MidiMap.tracks)
    var hold = [Bool](repeating: false, count: MidiMap.tracks)
    var names = (1...MidiMap.tracks).map { "Piste \($0)" }
    var master = 0.8
    var reverb = 0.15
    var leslieFast = false
    var sustain = false
    var panicFlash = false
    var preset = -1
    var channel = 0          // 0-15
    var momentary = true     // boutons : 127 à l'appui puis 0 (sinon 127 allumé / 0 éteint)
    var page = 0

    var out: (([UInt8]) -> Void)?

    private var lastVol = [Int](repeating: -1, count: MidiMap.tracks + 1)
    private var lastRev = -1

    private func send(_ m: [UInt8]) { out?(m) }
    private func cc(_ n: Int, _ v: Int) { send(MidiMap.cc(channel, n, v)) }

    /// fader de la piste i (0-7) ou du volume général (8) ; n'envoie que si la valeur MIDI change
    func setFader(_ i: Int, _ v0: Double) {
        let v = max(0, min(1, v0))
        if i == MidiMap.tracks { master = v } else { vol[i] = v }
        let mv = MidiMap.faderValue(v)
        if mv != lastVol[i] {
            lastVol[i] = mv
            cc(i == MidiMap.tracks ? MidiMap.MASTER : MidiMap.VOL_BASE + i, mv)
        }
    }

    func setReverb(_ v0: Double) {
        reverb = max(0, min(1, v0))
        let mv = MidiMap.faderValue(reverb)
        if mv != lastRev {
            lastRev = mv
            cc(MidiMap.REVERB, mv)
        }
    }

    /// bouton M (0), S (1) ou H (2) : appui ou relâchement
    func pressTrackButton(_ track: Int, _ b: Int, down: Bool) {
        guard track >= 0, track < MidiMap.tracks, b >= 0, b <= 2 else { return }
        let base = b == 0 ? MidiMap.MUTE_BASE : (b == 1 ? MidiMap.SOLO_BASE : MidiMap.HOLD_BASE)
        if down {
            let on: Bool
            switch b {
            case 0: mute[track].toggle(); on = mute[track]
            case 1: solo[track].toggle(); on = solo[track]
            default: hold[track].toggle(); on = hold[track]
            }
            cc(base + track, momentary ? 127 : (on ? 127 : 0))
        } else if momentary {
            cc(base + track, 0)
        }
    }

    func toggleLeslie() {
        leslieFast.toggle()
        if momentary {
            cc(MidiMap.LESLIE, 127)
            cc(MidiMap.LESLIE, 0)
        } else {
            cc(MidiMap.LESLIE, leslieFast ? 127 : 0)
        }
    }

    func setSustain(_ on: Bool) {
        sustain = on
        cc(MidiMap.SUSTAIN, on ? 127 : 0)
    }

    func choosePreset(_ k: Int) {
        guard k >= 0, k < MidiMap.presets else { return }
        preset = k
        send(MidiMap.program(channel, k))
    }

    func panic() {
        panicFlash = true
        sustain = false
        send(MidiMap.panic())
    }

    /// renvoie la position de tous les faders
    func sendAll() {
        for i in 0...MidiMap.tracks {
            lastVol[i] = -1
            setFader(i, i == MidiMap.tracks ? master : vol[i])
        }
        lastRev = -1
        setReverb(reverb)
    }

    // ---- enregistrement (même format que la version Android)

    func save() -> String {
        var s = "v1"
        for i in 0..<MidiMap.tracks {
            s += "|\(vol[i])|\(mute[i] ? 1 : 0)\(solo[i] ? 1 : 0)\(hold[i] ? 1 : 0)|" + names[i].replacingOccurrences(of: "|", with: "/")
        }
        s += "|\(master)|\(reverb)|\(channel)|\(momentary ? 1 : 0)|\(leslieFast ? 1 : 0)|\(preset)|\(page)"
        return s
    }

    @discardableResult
    func load(_ s: String?) -> Bool {
        guard let s = s, s.hasPrefix("v1|") else { return false }
        let f = s.components(separatedBy: "|")
        guard f.count >= 1 + 3 * MidiMap.tracks + 7 else { return false }
        var k = 1
        var nv = vol, nm = mute, ns = solo, nh = hold, nn = names
        for i in 0..<MidiMap.tracks {
            guard let v = Double(f[k]) else { return false }
            nv[i] = max(0, min(1, v.isNaN ? 0 : v)); k += 1
            let fl = Array(f[k]); k += 1
            nm[i] = fl.count > 0 && fl[0] == "1"
            ns[i] = fl.count > 1 && fl[1] == "1"
            nh[i] = fl.count > 2 && fl[2] == "1"
            let n = f[k].trimmingCharacters(in: .whitespaces); k += 1
            nn[i] = n.isEmpty ? "Piste \(i + 1)" : String(n.prefix(16))
        }
        guard let ma = Double(f[k]), let rv = Double(f[k + 1]), let chn = Int(f[k + 2]),
              let pr = Int(f[k + 5]), let pg = Int(f[k + 6]) else { return false }
        vol = nv; mute = nm; solo = ns; hold = nh; names = nn
        master = max(0, min(1, ma))
        reverb = max(0, min(1, rv))
        channel = max(0, min(15, chn))
        momentary = f[k + 3] != "0"
        leslieFast = f[k + 4] == "1"
        preset = max(-1, min(7, pr))
        page = max(0, min(2, pg))
        return true
    }
}
