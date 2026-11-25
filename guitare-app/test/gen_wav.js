// Génère un WAV de test (progression jouée par un son de corde pincée) : node gen_wav.js sortie.wav
const M = require('../assets/www/js/music.js');
const sr = 44100;
const chords = (process.argv[3] || 'Dm9 G13 Cmaj9 A7b9 Dm7 G7 Cmaj7 C6').split(' ');
const bpm = 96, beat = 60 / bpm;
const x = new Float32Array(Math.ceil(chords.length * 4 * beat * sr) + sr);
function pluck(start, midi, dur, amp) {
  const f0 = 440 * Math.pow(2, (midi - 69) / 12), n0 = Math.floor(start * sr), n = Math.floor(dur * sr);
  for (let i = 0; i < n && n0 + i < x.length; i++) { const t = i / sr; let v = 0; for (let h = 1; h <= 10; h++) v += Math.sin(2 * Math.PI * f0 * h * t) * Math.exp(-t * (1.2 + h * 0.7)) / h; x[n0 + i] += v * amp * Math.min(1, i / 40); }
}
chords.forEach((s, i) => { const p = M.parseChord(s); const iv = M.CHORD_BY_ID[p.id].iv;
  for (let b = 0; b < 4; b++) { const t = (i * 4 + b) * beat; pluck(t, 36 + (p.bass ?? p.root) % 12, beat, 0.4); iv.forEach((v, k) => pluck(t + k * 0.01, 52 + ((p.root - 4 + 12) % 12) + v, beat, 0.22)); } });
const buf = Buffer.alloc(44 + x.length * 2);
buf.write('RIFF', 0); buf.writeUInt32LE(36 + x.length * 2, 4); buf.write('WAVEfmt ', 8); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(1, 22);
buf.writeUInt32LE(sr, 24); buf.writeUInt32LE(sr * 2, 28); buf.writeUInt16LE(2, 32); buf.writeUInt16LE(16, 34); buf.write('data', 36); buf.writeUInt32LE(x.length * 2, 40);
for (let i = 0; i < x.length; i++) buf.writeInt16LE(Math.max(-32767, Math.min(32767, Math.round(x[i] * 12000))), 44 + i * 2);
require('fs').writeFileSync(process.argv[2], buf);
