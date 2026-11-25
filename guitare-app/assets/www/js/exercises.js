// Exercices : oreille (intervalles, accords, progressions) et théorie (manche, accords, degrés, modes).
'use strict';
const EX = (() => {
  const rnd = (n) => Math.floor(Math.random() * n);
  const pick = (a) => a[rnd(a.length)];
  const shuffle = (a) => { a = a.slice(); for (let i = a.length - 1; i > 0; i--) { const j = rnd(i + 1); [a[i], a[j]] = [a[j], a[i]]; } return a; };
  const mod = M.mod;
  function choicesWith(answer, pool, n = 4) {
    const others = shuffle(pool.filter((x) => x !== answer)).slice(0, n - 1);
    const all = shuffle([answer, ...others]);
    return { choices: all, answer: all.indexOf(answer) };
  }

  const LIST = [
    { id: 'iv-ear', group: 'Oreille', name: 'Intervalles à l’oreille', desc: 'Deux notes sont jouées : quel est l’intervalle ?', levels: ['Facile (3, 4, 5, 8)', 'Moyen (+ 2, 6, 7)', 'Tous'],
      gen(lvl) {
        const sets = [[3, 4, 5, 7, 12], [2, 3, 4, 5, 7, 9, 10, 11, 12], [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]];
        const set = sets[lvl];
        const st = pick(set);
        const base = 48 + rnd(12);
        const down = lvl === 2 && Math.random() < 0.3;
        const name = (s) => M.INTERVALS[s].name;
        const c = choicesWith(name(st), set.map(name), Math.min(6, set.length));
        const iv = M.INTERVALS[st];
        return { prompt: down ? 'Intervalle <b>descendant</b>' : 'Intervalle <b>ascendant</b>',
          play: () => { const a = down ? base + st : base, b = down ? base : base + st; Synth.note(a, 0, 0.7, 1.2); Synth.note(b, 0.7, 0.7, 1.5); Synth.note(a, 1.6, 0.5, 1.5); Synth.note(b, 1.6, 0.5, 1.5); },
          ...c, explain: `${iv.name} = ${st} demi-ton${st > 1 ? 's' : ''}. ${iv.feel} Repère : ${iv.song}.` };
      } },
    { id: 'chord-ear', group: 'Oreille', name: 'Qualité d’accord', desc: 'Majeur, mineur, 7, maj7, m9, 13… reconnais la couleur.', levels: ['Majeur / mineur', 'Triades (dim, aug, sus)', 'Accords de 7e', 'Jazz / gospel / R&B'],
      gen(lvl) {
        const sets = [['maj', 'min'], ['maj', 'min', 'dim', 'aug', 'sus4', 'sus2'], ['maj7', '7', 'm7', 'm7b5', 'dim7', 'm6'], ['maj9', '9', 'm9', '13', '7s9', '7b9', '9sus4', 'm11', '69']];
        const id = pick(sets[lvl]);
        const root = rnd(12);
        const nm = (x) => M.CHORD_BY_ID[x].name + (M.CHORD_BY_ID[x].sym ? ' (' + M.CHORD_BY_ID[x].sym + ')' : '');
        const c = choicesWith(nm(id), sets[lvl].map(nm), Math.min(6, sets[lvl].length));
        const midis = G.chordMidis(root, id);
        return { prompt: 'Quel type d’accord ?', play: () => { Synth.strum(midis, 0); Synth.arpeggio(midis, 1.6, 0.22, 1.2); }, ...c,
          explain: `C’était ${M.chordSymbol(root, id)} : ${M.chordNotes(root, id).join(' ')} (${M.CHORD_BY_ID[id].iv.map((i) => M.degLabel(i, M.CHORD_BY_ID[id].iv)).join(' ')}).` };
      } },
    { id: 'prog-ear', group: 'Oreille', name: 'Progressions à l’oreille', desc: 'Reconnais les enchaînements les plus courants.', levels: ['Pop', 'Avec ii, iii et 7e'],
      gen(lvl) {
        const sets = [
          { 'I – IV – V – I': [[0, 'maj'], [5, 'maj'], [7, 'maj'], [0, 'maj']], 'I – V – vi – IV': [[0, 'maj'], [7, 'maj'], [9, 'min'], [5, 'maj']], 'I – vi – IV – V': [[0, 'maj'], [9, 'min'], [5, 'maj'], [7, 'maj']], 'vi – IV – I – V': [[9, 'min'], [5, 'maj'], [0, 'maj'], [7, 'maj']], 'I – IV – I – V': [[0, 'maj'], [5, 'maj'], [0, 'maj'], [7, 'maj']] },
          { 'ii – V – I': [[2, 'm7'], [7, '7'], [0, 'maj7'], [0, 'maj7']], 'I – vi – ii – V': [[0, 'maj7'], [9, 'm7'], [2, 'm7'], [7, '7']], 'iii – vi – ii – V': [[4, 'm7'], [9, 'm7'], [2, 'm7'], [7, '7']], 'IV – V – iii – vi': [[5, 'maj7'], [7, '7'], [4, 'm7'], [9, 'm7']], 'I – IV – iv – I': [[0, 'maj7'], [5, 'maj7'], [5, 'm6'], [0, 'maj7']], 'ii – ♭II7 – I': [[2, 'm7'], [1, '7'], [0, 'maj7'], [0, 'maj7']] },
        ];
        const set = sets[lvl];
        const name = pick(Object.keys(set));
        const key = rnd(12);
        const c = choicesWith(name, Object.keys(set), Math.min(5, Object.keys(set).length));
        const seq = set[name];
        return { prompt: 'Quelle progression ? (la tonique est jouée d’abord)', ...c,
          play: () => { Synth.note(36 + key, 0, 0.6, 0.8); seq.forEach(([s, id], i) => Synth.strum(G.chordMidis(mod(key + s), id), 1 + i * 1.1, { dur: 1.1 })); },
          explain: `${name} en ${M.pcName(key)} : ${seq.map(([s, id]) => M.chordSymbol(mod(key + s), id)).join(' – ')}` };
      } },
    { id: 'fret-note', group: 'Manche', name: 'Notes du manche', desc: 'Une position est marquée : quelle note ?', levels: ['Frettes 0 à 5', 'Frettes 0 à 12', 'Tout le manche (15)'], grid: true,
      gen(lvl) {
        const maxF = [5, 12, 15][lvl];
        const s = rnd(6), f = rnd(maxF + 1);
        const midi = G.getTuning()[s] + f;
        const names = Array.from({ length: 12 }, (_, i) => M.pcName(i));
        return { prompt: `Corde ${6 - s}, frette ${f}`, visual: G.fretboard({ frets: Math.max(maxF, 5), mark: (ss, ff) => (ss === s && ff === f ? { cls: 'target', label: '?' } : null) }),
          choices: names, answer: mod(midi), play: () => Synth.note(midi, 0, 0.8, 1.5), playAfter: true,
          explain: `C’est ${M.pcName(midi)} (${M.noteName(midi)}).` };
      } },
    { id: 'chord-notes', group: 'Théorie', name: 'Notes d’un accord', desc: 'Retrouve les notes qui composent l’accord.', levels: ['Triades', 'Septièmes', 'Extensions'],
      gen(lvl) {
        const sets = [['maj', 'min', 'dim', 'aug', 'sus4'], ['maj7', '7', 'm7', 'm7b5', 'dim7', '6'], ['maj9', '9', 'm9', '13', '7s9', '7b9', 'm11']];
        const id = pick(sets[lvl]), root = rnd(12);
        const c = M.CHORD_BY_ID[id];
        const flats = [1, 3, 5, 8, 10].includes(root) || id.startsWith('m') || id.includes('b');
        const fmt = (pcs) => pcs.map((p) => M.pcName(p, flats)).join(' ');
        const right = fmt(c.iv.map((i) => root + i));
        const wrong = new Set();
        let guard = 0;
        while (wrong.size < 3 && guard++ < 50) {
          const iv = c.iv.slice();
          const k = 1 + rnd(iv.length - 1);
          iv[k] += pick([-1, 1]);
          const w = fmt(iv.map((i) => root + i));
          if (w !== right) wrong.add(w);
        }
        const r = choicesWith(right, [right, ...wrong]);
        return { prompt: `Les notes de <b class="sym">${M.chordSymbol(root, id)}</b> ?`, ...r, explain: `${c.name} = ${c.iv.map((i) => M.degLabel(i, c.iv)).join(' – ')} → ${right}`, play: () => Synth.strum(G.chordMidis(root, id)), playAfter: true };
      } },
    { id: 'chord-name', group: 'Théorie', name: 'Nommer l’accord', desc: 'Des notes ou un diagramme : quel accord ?', levels: ['Triades', 'Septièmes', 'Jazz'],
      gen(lvl) {
        const sets = [['maj', 'min', 'dim', 'aug', 'sus2', 'sus4'], ['maj7', '7', 'm7', 'm7b5', 'dim7', '6', 'm6'], ['maj9', '9', 'm9', '13', '7s9', '7b9', '9sus4', 'm11', '69', 'maj7s11']];
        const id = pick(sets[lvl]), root = rnd(12);
        const right = M.chordSymbol(root, id);
        const pool = sets[lvl].map((x) => M.chordSymbol(root, x)).concat([M.chordSymbol(mod(root + 5), id), M.chordSymbol(mod(root + 7), id)]);
        const r = choicesWith(right, pool);
        const v = G.voicings(root, id);
        const useDiag = Math.random() < 0.5 && v.length;
        const vo = useDiag ? v[rnd(Math.min(3, v.length))] : null;
        return { prompt: useDiag ? 'Quel accord sur ce diagramme ?' : `Quel accord contient <b>${M.chordNotes(root, id).join(' · ')}</b> (fondamentale en premier) ?`,
          visual: useDiag ? `<div class="diagwrap">${G.diagram(root, id, vo.frets, { labels: 'none' })}</div>` : '', ...r,
          play: () => Synth.strum(useDiag ? G.voicingMidis(vo.frets) : G.chordMidis(root, id)), playAfter: true, explain: `${right} : ${M.CHORD_BY_ID[id].name}.` };
      } },
    { id: 'interval-name', group: 'Théorie', name: 'Calcul d’intervalles', desc: 'Quel intervalle entre deux notes ?', levels: ['Naturelles', 'Avec altérations'],
      gen(lvl) {
        const nat = [0, 2, 4, 5, 7, 9, 11];
        const a = lvl ? rnd(12) : pick(nat);
        let st;
        do { st = 1 + rnd(11); } while (!lvl && !nat.includes(mod(a + st)));
        const b = mod(a + st);
        const right = M.INTERVALS[st].name;
        const r = choicesWith(right, M.INTERVALS.slice(1, 12).map((x) => x.name));
        return { prompt: `De <b>${M.pcName(a)}</b> à <b>${M.pcName(b)}</b> (en montant) ?`, ...r, play: () => { Synth.note(60 + a, 0); Synth.note(60 + a + st, 0.6); }, playAfter: true, explain: `${st} demi-tons = ${right}.` };
      } },
    { id: 'degrees', group: 'Théorie', name: 'Degrés et tonalités', desc: 'Trouve l’accord d’un degré dans une tonalité.', levels: ['Triades en majeur', 'Accords de 7e', 'Majeur et mineur'],
      gen(lvl) {
        const mode = lvl === 2 && Math.random() < 0.5 ? 'minor' : 'major';
        const tonic = rnd(12);
        const chords = M.diatonicChords(tonic, mode, lvl > 0);
        const d = rnd(7);
        const flats = M.keyPrefersFlats({ tonic, mode });
        M.setPreferFlats(flats);
        const syms = chords.map((c) => M.chordSymbol(c.root, c.id));
        const right = syms[d];
        const pool = syms.concat(chords.map((c) => M.chordSymbol(c.root, c.id === 'min' ? 'maj' : c.id === 'maj' ? 'min' : c.id === 'm7' ? '7' : 'm7')));
        const r = choicesWith(right, [...new Set(pool)]);
        const key = M.keyName({ tonic, mode });
        M.setPreferFlats(null);
        return { prompt: `En <b>${key}</b>, quel est l’accord du degré <b>${chords[d].roman}</b> ?`, ...r, explain: `${key} : ${syms.map((s, i) => chords[i].roman + ' = ' + s).join(', ')}.`,
          play: () => Synth.strum(G.chordMidis(chords[d].root, chords[d].id)), playAfter: true };
      } },
    { id: 'modes', group: 'Théorie', name: 'Les modes', desc: 'Formules, notes caractéristiques, degrés.', levels: ['Modes majeurs', 'Avancé'],
      gen(lvl) {
        const m = pick(M.MODES);
        const names = M.MODES.map((x) => x.name);
        const fmt = (iv) => iv.map((i) => ['1', '♭2', '2', '♭3', '3', '4', '♯4', '5', '♭6', '6', '♭7', '7'][i]).join(' ');
        const qs = [
          () => ({ prompt: `Quel mode a la formule <b>${fmt(m.iv)}</b> ?`, ...choicesWith(m.name, names, 5) }),
          () => ({ prompt: `Quel mode commence sur le degré <b>${m.deg}</b> de la gamme majeure ?`, ...choicesWith(m.name, names, 5) }),
          () => ({ prompt: `Quel mode a pour note caractéristique <b>${m.char}</b> ?`, ...choicesWith(m.name, names, 5) }),
        ];
        if (lvl) {
          const r = rnd(12);
          const degIdx = M.MODES.indexOf(m);
          const parent = mod(r - M.SCALE_BY_ID.major.iv[degIdx]);
          qs.push(() => ({ prompt: `<b>${M.pcName(r)} ${m.name.toLowerCase()}</b> utilise les notes de quelle gamme majeure ?`, ...choicesWith(M.pcName(parent) + ' majeur', Array.from({ length: 12 }, (_, i) => M.pcName(i) + ' majeur')) }));
          if (m.chord !== 'm7') qs.push(() => ({ prompt: `Sur un accord <b>${M.CHORD_BY_ID[m.chord].name}</b> (${M.CHORD_BY_ID[m.chord].sym}), quel mode correspond le mieux ?`, ...choicesWith(m.name, names.filter((n) => !['Dorien', 'Phrygien', 'Éolien'].includes(n) || n === m.name), 4) }));
        }
        const q = pick(qs)();
        return { ...q, explain: `${m.name} (${fmt(m.iv)}) : ${m.color} ${m.use}`, play: () => Synth.arpeggio(m.iv.concat([12]).map((i) => 60 + i), 0, 0.22, 0.6), playAfter: true };
      } },
  ];
  const BY_ID = Object.fromEntries(LIST.map((e) => [e.id, e]));

  // statistiques persistantes
  function loadStats() { try { return JSON.parse(localStorage.getItem('gx-stats') || '{}'); } catch (e) { return {}; } }
  function saveStats(s) { try { localStorage.setItem('gx-stats', JSON.stringify(s)); } catch (e) { /* stockage indisponible */ } }
  function record(id, ok) {
    const s = loadStats();
    const e = s[id] || { ok: 0, total: 0, best: 0, streak: 0 };
    e.total++; if (ok) { e.ok++; e.streak++; e.best = Math.max(e.best, e.streak); } else e.streak = 0;
    s[id] = e; saveStats(s);
    return e;
  }
  return { LIST, BY_ID, loadStats, record };
})();
