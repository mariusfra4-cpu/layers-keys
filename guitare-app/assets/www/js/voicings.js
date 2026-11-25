// Familles de voicings construites comme le font les guitaristes jazz / gospel / R&B :
// triades par jeu de cordes, drop 2, drop 3, shells, sans fondamentale, quartes, structures supérieures, triades « couleur ».
'use strict';
const Voicings = (() => {
  const mod = M.mod;
  const SETS3 = [[3, 4, 5], [2, 3, 4], [1, 2, 3], [0, 1, 2]];
  const SETS4 = [[2, 3, 4, 5], [1, 2, 3, 4], [0, 1, 2, 3]];
  const SETS_DROP3 = [[0, 2, 3, 4], [1, 3, 4, 5]];
  const STR = ['6', '5', '4', '3', '2', '1'];
  const setName = (s) => 'cordes ' + s.map((x) => STR[x]).join('-');

  // Place une suite de classes de hauteur (grave → aigu) sur des cordes données, hauteurs strictement croissantes.
  function place(pcs, strings, maxSpan = 4, maxFret = 15) {
    const tu = G.getTuning();
    const res = [];
    const cur = [];
    (function dfs(i, prevPitch) {
      if (i === pcs.length) {
        const fretted = cur.filter((f) => f > 0);
        const mn = fretted.length ? Math.min(...fretted) : 0, mx = fretted.length ? Math.max(...fretted) : 0;
        if (mx - mn > maxSpan) return;
        if (cur.includes(0) && mx > 4) return; // corde à vide + position haute : injouable
        const fr = [-1, -1, -1, -1, -1, -1];
        strings.forEach((s, k) => { fr[s] = cur[k]; });
        res.push({ frets: fr, minFret: mn, maxFret: mx });
        return;
      }
      const s = strings[i];
      for (let f = 0; f <= maxFret; f++) {
        const p = tu[s] + f;
        if (mod(p) !== mod(pcs[i]) || p <= prevPitch) continue;
        cur[i] = f;
        // élagage : écart déjà trop grand
        const fr = cur.slice(0, i + 1).filter((x) => x > 0);
        if (fr.length && Math.max(...fr) - Math.min(...fr) > maxSpan) continue;
        dfs(i + 1, p);
      }
    })(0, -1);
    // dédoublonne et trie par position
    const seen = new Set();
    return res.filter((v) => { const k = v.frets.join(); if (seen.has(k)) return false; seen.add(k); return true; })
      .sort((a, b) => a.minFret - b.minFret);
  }
  const inv = (arr, k) => arr.slice(k).concat(arr.slice(0, k));
  const INV_NAMES = ['position fondamentale', '1er renversement', '2e renversement', '3e renversement'];

  function triadOf(id) {
    const iv = M.CHORD_BY_ID[id].iv;
    const third = iv.includes(4) ? 4 : iv.includes(3) ? 3 : iv.includes(5) ? 5 : iv.includes(2) ? 2 : 4;
    const fifth = iv.includes(7) ? 7 : iv.includes(6) ? 6 : iv.includes(8) ? 8 : 7;
    return [0, third, fifth];
  }
  function triadId(t) {
    const k = t.join(',');
    return { '0,4,7': 'maj', '0,3,7': 'min', '0,3,6': 'dim', '0,4,8': 'aug', '0,5,7': 'sus4', '0,2,7': 'sus2', '0,4,6': 'maj', '0,3,8': 'min' }[k] || 'maj';
  }
  // 4 notes représentatives d'un accord (1 3 5 7, ou 1 3 7 + extension)
  function fourOf(id) {
    const c = M.CHORD_BY_ID[id];
    const iv = c.iv.map((i) => mod(i));
    if (iv.length <= 4) return iv.length === 4 ? iv.slice().sort((a, b) => a - b) : null;
    const ess = c.ess.map((i) => mod(i));
    const four = [...new Set(ess)].slice(0, 4);
    if (four.length < 4) for (const i of iv) if (!four.includes(i) && four.length < 4) four.push(i);
    return four.sort((a, b) => a - b);
  }

  // Chaque famille renvoie [{ group, label, frets, pcs (pour l'étiquette) }]
  const FAMILIES = [
    { id: 'triads', name: 'Triades', desc: 'Les 3 notes de base (1-3-5) sur chaque groupe de 3 cordes voisines, dans les 3 renversements. La base du jeu rythmique funk, gospel, R&B et du chord-melody.',
      gen(root, id) {
        const t = triadOf(id);
        const out = [];
        for (const set of SETS3) for (let k = 0; k < 3; k++) {
          const v = place(inv(t, k).map((i) => root + i), set, 4, 14)[0];
          if (v) out.push({ group: setName(set), label: INV_NAMES[k], frets: v.frets, root, id: triadId(t) });
        }
        return out;
      } },
    { id: 'color', name: 'Triades « couleur »', desc: 'Les triades formées par les notes de l’accord SANS sa fondamentale : jouées sur la basse, elles donnent l’accord enrichi (Em sur C = Cmaj7, G sur C = Cmaj9). La technique n°1 du neo-soul et du gospel.',
      gen(root, id) {
        const pcs = new Set(M.CHORD_BY_ID[id].iv.map((i) => mod(root + i)));
        const out = [];
        for (let r = 0; r < 12; r++) for (const [tid, iv] of [['maj', [0, 4, 7]], ['min', [0, 3, 7]], ['dim', [0, 3, 6]], ['aug', [0, 4, 8]]]) {
          if (r === mod(root)) continue;
          if (!iv.every((i) => pcs.has(mod(r + i)))) continue;
          for (const set of [SETS3[0], SETS3[1]]) {
            const v = place(iv.map((i) => r + i), set, 4, 14)[0];
            if (v) out.push({ group: `${M.chordSymbol(r, tid)} sur ${M.pcName(root)}`, label: setName(set) + ' (basse ' + M.pcName(root) + ' jouée à part)', frets: v.frets, root, id, tri: [r, tid] });
          }
        }
        return out;
      } },
    { id: 'shell', name: 'Shells (1-3-7)', desc: 'Trois notes : la fondamentale, la tierce et la septième. Le son « big band » (Freddie Green), parfait pour accompagner et pour apprendre le jazz.',
      gen(root, id) {
        const iv = M.CHORD_BY_ID[id].iv.map((i) => mod(i));
        const third = iv.includes(4) ? 4 : iv.includes(3) ? 3 : iv.includes(5) ? 5 : null;
        const sev = iv.includes(10) ? 10 : iv.includes(11) ? 11 : iv.includes(9) ? 9 : null;
        if (third === null || sev === null) return [];
        const out = [];
        const a = place([root, root + sev, root + third], [0, 2, 3], 4, 14)[0];
        if (a) out.push({ group: 'Fondamentale corde 6', label: '1 – 7 – 3', frets: a.frets, root, id });
        const b = place([root, root + third, root + sev], [1, 2, 3], 4, 14)[0];
        if (b) out.push({ group: 'Fondamentale corde 5', label: '1 – 3 – 7', frets: b.frets, root, id });
        const c = place([root, root + sev, root + third], [1, 3, 4], 4, 14)[0];
        if (c) out.push({ group: 'Fondamentale corde 5', label: '1 – 7 – 3 (cordes 5-3-2)', frets: c.frets, root, id });
        return out;
      } },
    { id: 'drop2', name: 'Drop 2', desc: 'Un accord serré de 4 notes dont on descend d’une octave la 2e note la plus aiguë : 4 cordes voisines, 4 renversements. Le voicing jazz le plus utilisé à la guitare.',
      gen(root, id) {
        const f = fourOf(id);
        if (!f) return [];
        const out = [];
        for (const set of SETS4) for (let k = 0; k < 4; k++) {
          const close = inv(f, k);
          const v = place([close[2], close[0], close[1], close[3]].map((i) => root + i), set, 4, 15)[0];
          if (v) out.push({ group: setName(set), label: 'basse ' + M.degLabel(close[2], M.CHORD_BY_ID[id].iv), frets: v.frets, root, id });
        }
        return out;
      } },
    { id: 'drop3', name: 'Drop 3', desc: 'On descend la 3e note la plus aiguë : basse sur la corde 6 ou 5, une corde sautée. Son ample, idéal pour le chord-melody et la ballade.',
      gen(root, id) {
        const f = fourOf(id);
        if (!f) return [];
        const out = [];
        for (const set of SETS_DROP3) for (let k = 0; k < 4; k++) {
          const close = inv(f, k);
          const v = place([close[1], close[0], close[2], close[3]].map((i) => root + i), set, 5, 15)[0];
          if (v) out.push({ group: setName(set), label: 'basse ' + M.degLabel(close[1], M.CHORD_BY_ID[id].iv), frets: v.frets, root, id });
        }
        return out;
      } },
    { id: 'rootless', name: 'Sans fondamentale (A / B)', desc: 'Les voicings « Bill Evans » : 3-5-7-9 (forme A) et 7-9-3-5 (forme B). Le bassiste joue la fondamentale ; toi tu joues la couleur. Indispensable en jazz, gospel et R&B.',
      gen(root, id) {
        const c = M.CHORD_BY_ID[id];
        const iv = c.iv.map((i) => mod(i));
        const third = iv.includes(4) ? 4 : iv.includes(3) ? 3 : null;
        const sev = iv.includes(10) ? 10 : iv.includes(11) ? 11 : iv.includes(9) && iv.includes(6) ? 9 : null;
        if (third === null || sev === null) return [];
        const fifth = iv.includes(7) ? 7 : iv.includes(6) ? 6 : iv.includes(8) ? 8 : 7;
        const ninth = iv.includes(1) ? 1 : iv.includes(3) && iv.includes(4) ? 3 : 2;
        const top = iv.includes(9) && sev !== 9 ? 9 : fifth; // 13 à la place de la quinte si présente
        const forms = [['A', [third, top, sev, ninth]], ['B', [sev, ninth, third, top]]];
        const out = [];
        for (const [n, ord] of forms) for (const set of [SETS4[0], SETS4[1]]) {
          const v = place(ord.map((i) => root + i), set, 4, 15)[0];
          if (v) out.push({ group: 'Forme ' + n, label: ord.map((i) => M.degLabel(i, c.iv.concat([ninth, top]))).join('-') + ' · ' + setName(set), frets: v.frets, root, id });
        }
        return out;
      } },
    { id: 'quartal', name: 'Quartes', desc: 'Des notes de l’accord empilées en quartes justes : son moderne, ouvert (McCoy Tyner, « So What », neo-soul, praise moderne).',
      gen(root, id) {
        const pcs = new Set(M.CHORD_BY_ID[id].iv.map((i) => mod(root + i)));
        const out = [];
        for (const start of [...pcs]) {
          const stack = [0, 5, 10].map((i) => start + i);
          if (!stack.every((p) => pcs.has(mod(p)))) continue;
          const four = start + 15;
          const notes = pcs.has(mod(four)) ? stack.concat([four]) : stack;
          for (const set of notes.length === 4 ? [SETS4[0], SETS4[1]] : [SETS3[0], SETS3[1]]) {
            const v = place(notes, set, 4, 15)[0];
            if (v) out.push({ group: 'Depuis ' + M.degLabel(mod(start - root), M.CHORD_BY_ID[id].iv), label: setName(set), frets: v.frets, root, id });
          }
        }
        if (M.CHORD_BY_ID[id].iv.includes(3) && M.CHORD_BY_ID[id].iv.includes(10)) {
          const v = place([0, 5, 10, 15, 19].map((i) => root + i), [0, 1, 2, 3, 4], 4, 15)[0];
          if (v) out.push({ group: '« So What »', label: '3 quartes + tierce majeure (m11)', frets: v.frets, root, id: 'm11' });
        }
        return out;
      } },
    { id: 'ust', name: 'Structures supérieures', desc: 'Sur un accord de dominante : le triton (3 et ♭7) en bas, et une triade majeure au-dessus qui ajoute les tensions. La signature harmonique du gospel moderne.',
      gen(root, id) {
        if (!M.CHORD_BY_ID[id].iv.includes(4) || !M.CHORD_BY_ID[id].iv.includes(10)) return [];
        const UST = [[2, '13♯11 (triade II)', '13s11'], [9, '13♭9 (triade VI)', '13b9'], [8, '7♯5♯9 (triade ♭VI)', '7alt'], [3, '7♯9 (triade ♭III)', '7s9'], [6, '7♭9♭5 (triade ♭V)', '7b9'], [7, '9sus / 7 simple (triade V)', '9']];
        const out = [];
        for (const [t, name, cid] of UST) {
          const notes = [root + 4, root + 10, root + t, root + t + 4, root + t + 7];
          const v = place(notes, [1, 2, 3, 4, 5], 5, 15)[0] || place([root + 10, root + 4 + 12, root + t, root + t + 4, root + t + 7], [1, 2, 3, 4, 5], 5, 15)[0];
          if (v) out.push({ group: M.chordSymbol(mod(root + t), 'maj') + ' sur ' + M.pcName(root) + '7', label: name, frets: v.frets, root, id: cid });
        }
        return out;
      } },
  ];
  function family(fid, root, id) {
    const f = FAMILIES.find((x) => x.id === fid);
    try { return f ? f.gen(root, id) : []; } catch (e) { return []; }
  }
  return { FAMILIES, family, place };
})();
