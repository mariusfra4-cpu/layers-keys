// Test de l'extraction de mélodie : une mélodie (guitare lead) jouée sur des accords.
const M = require('../assets/www/js/music.js');
global.M = M;
const DSP = require('../assets/www/js/dsp.js');
const sr = 11025;
function pluck(x, start, midi, dur, amp) {
  const f0 = 440 * Math.pow(2, (midi - 69) / 12), n0 = Math.floor(start * sr), n = Math.floor(dur * sr);
  for (let i = 0; i < n && n0 + i < x.length; i++) {
    const t = i / sr; let v = 0;
    for (let h = 1; h <= 8; h++) { if (f0 * h > sr / 2) break; v += Math.sin(2 * Math.PI * f0 * h * t) * Math.exp(-t * (1.0 + h * 0.6)) / h; }
    x[n0 + i] += v * amp * Math.min(1, i / 30) * (i > n - 200 ? (n - i) / 200 : 1);
  }
}
const bpm = 100, beat = 60 / bpm;
const chords = ['C', 'Am', 'F', 'G'];
const mel = 'E4 G4 A4 G4 E4 D4 C4 D4 | E4 E4 C5 B4 A4 G4 E4 C4 | F4 A4 C5 A4 F4 E4 D4 C4 | B3 D4 G4 F4 D4 B3 D4 G4'.replace(/\|/g, '').split(/\s+/).filter(Boolean).map(M.midiFromName);
const x = new Float32Array(Math.ceil((chords.length * 4 + 1) * beat * sr));
chords.forEach((s, i) => { const p = M.parseChord(s); for (let b = 0; b < 4; b++) { const t = (i * 4 + b) * beat; pluck(x, t, 36 + p.root, beat, 0.3); M.CHORD_BY_ID[p.id].iv.forEach((v, k) => pluck(x, t + k * 0.01, 48 + p.root + v, beat, 0.1)); } });
mel.forEach((m, i) => pluck(x, i * beat / 2, m, beat / 2, 0.45));
const r = DSP.analyzeSignal(x, sr, { level: 2 });
let ok = 0;
const got = mel.map((m, i) => {
  const mid = i * beat / 2 + beat / 4;
  const n = r.notes.find((q) => q.t <= mid && q.t + q.d > mid);
  if (n && n.midi === m) ok++;
  return n ? M.noteName(n.midi) : '·';
});
console.log('attendu :', mel.map((m) => M.noteName(m)).join(' '));
console.log('trouvé  :', got.join(' '));
console.log(`mélodie : ${ok}/${mel.length} notes justes, ${r.notes.length} notes détectées`);
console.log('accords :', r.segments.map((s) => (s.root < 0 ? 'N' : M.chordSymbol(s.root, s.id))).join(' '));
