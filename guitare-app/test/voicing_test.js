global.M = require('../assets/www/js/music.js');
const fs = require('fs');
eval(fs.readFileSync(__dirname + '/../assets/www/js/guitar.js', 'utf8') + ';global.G=G;');
for (const sym of ['C', 'G', 'Am', 'F', 'E7', 'Cmaj7', 'G13', 'Dm9', 'Bb9sus4', 'F#m7b5', 'Ebmaj9', 'C/E', 'Bdim7', 'A7#9']) {
  const p = M.parseChord(sym);
  const t0 = Date.now();
  const v = G.voicings(p.root, p.id, p.bass);
  console.log(sym.padEnd(8), (Date.now() - t0 + 'ms').padEnd(6), v.slice(0, 6).map((x) => x.frets.map((f) => (f < 0 ? 'x' : f)).join('')).join('  '));
}
