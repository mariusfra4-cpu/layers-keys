// Test de la reconnaissance d'accords sur des accords synthétisés (sons de corde pincée avec harmoniques)
const M = require('../assets/www/js/music.js');
global.M = M;
const DSP = require('../assets/www/js/dsp.js');
const sr = 11025;
function pluck(out, start, midi, dur, amp, detune = 0) {
  const f0 = 440 * Math.pow(2, (midi - 69 + detune) / 12);
  const n0 = Math.floor(start * sr), n = Math.floor(dur * sr);
  for (let i = 0; i < n && n0 + i < out.length; i++) {
    const t = i / sr; let v = 0;
    for (let h = 1; h <= 8; h++) { if (f0 * h > sr / 2) break; v += Math.sin(2 * Math.PI * f0 * h * t) * Math.exp(-t * (1.5 + h * 0.8)) / h; }
    out[n0 + i] += v * amp * Math.min(1, i / 30);
  }
}
let fail = 0, total = 0;
function prog(chords, bpm = 100, detune = 0, noise = 0.01) {
  const beat = 60 / bpm, barDur = beat * 4;
  const x = new Float32Array(Math.ceil(chords.length * barDur * sr) + sr);
  chords.forEach((sym, i) => {
    const p = M.parseChord(sym);
    const iv = M.CHORD_BY_ID[p.id].iv;
    const bassPc = p.bass !== undefined ? p.bass : p.root;
    for (let b = 0; b < 4; b++) {
      const t = i * barDur + b * beat;
      pluck(x, t, 36 + ((bassPc - 0 + 12) % 12) + 0, beat, 0.5, detune);
      iv.forEach((v, k) => pluck(x, t + k * 0.012, 52 + ((p.root - 4 + 12) % 12) + v, beat, 0.3, detune));
    }
  });
  for (let i = 0; i < x.length; i++) x[i] += (Math.random() - 0.5) * noise;
  const t0 = Date.now();
  const r = DSP.analyzeSignal(x, sr, { level: 3 });
  const ms = Date.now() - t0;
  const got = chords.map((_, i) => {
    const mid = i * barDur + barDur / 2;
    const s = r.segments.find((sg) => sg.start <= mid && sg.end > mid);
    M.setPreferFlats(M.keyPrefersFlats(r.key)); return s && s.root >= 0 ? M.chordSymbol(s.root, s.id, s.bass) : 'N';
  });
  chords.forEach((c, i) => { total++; const p = M.parseChord(c); const q = M.parseChord(got[i].replace('♯','#').replace('♭','b')) ; if (!q || q.root !== p.root || q.id !== p.id) fail++; });
  console.log(`clé ${M.keyName(r.key)} bpm ${r.bpm.toFixed(0)} accordage ${r.tuning.toFixed(2)} (${ms} ms, ${(x.length/sr).toFixed(0)} s)`);
  console.log('  attendu :', chords.join(' '));
  console.log('  trouvé  :', got.join(' '));
}
prog(['C', 'Am', 'F', 'G', 'C', 'Em', 'Dm', 'G']);
prog(['Dm7', 'G7', 'Cmaj7', 'A7', 'Dm9', 'G13', 'Cmaj9', 'C6']);
prog(['Ebmaj9', 'D7#9', 'Gm9', 'Fm9', 'Bb13', 'Ebmaj7', 'Abmaj7', 'Bb9sus4'], 90, -0.3);
prog(['Am', 'E7', 'Am', 'Dm', 'G', 'C', 'Bm7b5', 'E7'], 120);
prog(['Fmaj7', 'E7', 'Am7', 'Gm7', 'C7', 'Fmaj7', 'Bbmaj7', 'Edim7'], 80);
console.log(`échecs ${fail}/${total}`);
