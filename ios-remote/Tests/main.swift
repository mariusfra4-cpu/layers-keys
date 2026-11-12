import Foundation

// Tests sans iPhone (compilés avec MidiMap.swift et RemoteState.swift) : messages MIDI, boutons, faders, presets,
// enregistrement. Même vérifications que la version Android.
var fails = 0
func check(_ ok: Bool, _ what: String) {
    print((ok ? "OK    " : "ÉCHEC ") + what)
    if !ok { fails += 1 }
}

check(MidiMap.cc(0, MidiMap.VOL_BASE + 2, 100) == [0xB0, 22, 100], "volume piste 3 = CC 22")
check(MidiMap.cc(15, 30, 300) == [0xBF, 30, 127], "canal 16, valeur bornée")
check(MidiMap.program(0, 4) == [0xC0, 4], "preset 5 = Program Change 4")
let pn = MidiMap.panic()
check(pn.count == 144 && pn[0] == 0xB0 && pn[4] == 120 && pn[7] == 123 && pn[pn.count - 3] == 0xBF, "tout arrêter sur les 16 canaux")
check(MidiMap.faderValue(0) == 0 && MidiMap.faderValue(1) == 127 && MidiMap.faderValue(0.5) == 64, "faders 0..127")
let all = MidiMap.allControls()
check(Set(all).count == 35 && !all.contains { $0 == 0 || $0 == 1 || $0 == 7 || $0 == 11 || ($0 >= 32 && $0 <= 69) || $0 >= 120 },
      "35 CC libres et différents")

var sent: [[UInt8]] = []
let st = RemoteState()
st.out = { sent.append($0) }
st.setFader(0, 0.5)
st.setFader(0, 0.501)
check(sent == [[0xB0, 20, 64]], "fader 1 : un seul message quand la valeur ne change pas")
sent = []
st.pressTrackButton(4, 0, down: true)
st.pressTrackButton(4, 0, down: false)
check(st.mute[4] && sent == [[0xB0, 106, 127], [0xB0, 106, 0]], "M piste 5 momentané : 127 puis 0")
sent = []
st.momentary = false
st.pressTrackButton(2, 2, down: true)
st.pressTrackButton(2, 2, down: false)
check(st.hold[2] && sent == [[0xB0, 72, 127]], "H piste 3 en bascule")
st.momentary = true
sent = []
st.channel = 3
st.choosePreset(2)
check(sent == [[0xC3, 2]], "preset 3 canal 4")
sent = []
st.toggleLeslie()
check(st.leslieFast && sent.count == 2 && sent[0][1] == 30, "Leslie CC 30")
sent = []
st.sendAll()
check(sent.count == 10, "renvoyer tout : 10 messages")
st.names[1] = "Basse"
st.vol[6] = 0.25
let saved = st.save()
let st2 = RemoteState()
check(st2.load(saved) && st2.names[1] == "Basse" && abs(st2.vol[6] - 0.25) < 1e-9 && st2.leslieFast && st2.channel == 3,
      "réglages gardés")
check(!st2.load("abîmé") && !st2.load(nil) && !st2.load("v1|x"), "fichier abîmé ignoré")
// même format d'enregistrement que la version Android
check(saved.hasPrefix("v1|") && saved.components(separatedBy: "|").count == 1 + 3 * 8 + 7, "format identique à Android")

print(fails == 0 ? "\nTÉLÉCOMMANDE iPhone : TOUT VA BIEN" : "\nTÉLÉCOMMANDE iPhone : \(fails) ÉCHEC(S)")
exit(fails == 0 ? 0 : 1)
