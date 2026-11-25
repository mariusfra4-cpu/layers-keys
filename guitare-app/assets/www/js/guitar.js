// Manche de guitare : recherche de doigtés, diagrammes d'accords (SVG), manche complet.
'use strict';
const G = (() => {
  const TUNINGS = {
    standard: { name: 'Standard (E A D G B E)', midi: [40, 45, 50, 55, 59, 64] },
    dropd: { name: 'Drop D (D A D G B E)', midi: [38, 45, 50, 55, 59, 64] },
    halfdown: { name: '½ ton plus bas (E♭)', midi: [39, 44, 49, 54, 58, 63] },
    dadgad: { name: 'DADGAD', midi: [38, 45, 50, 55, 57, 62] },
    openg: { name: 'Open G (D G D G B D)', midi: [38, 43, 50, 55, 59, 62] },
    opend: { name: 'Open D (D A D F♯ A D)', midi: [38, 45, 50, 54, 57, 62] },
  };
  let tuning = TUNINGS.standard.midi;
  function setTuning(id) { tuning = (TUNINGS[id] || TUNINGS.standard).midi; vcache.clear(); }
  const getTuning = () => tuning;
  const mod = (n) => ((n % 12) + 12) % 12;
  const vcache = new Map();

  // ---------- Recherche de doigtés ----------
  function voicings(root, id, bass, max = 10) {
    const key = root + ':' + id + ':' + bass;
    if (vcache.has(key)) return vcache.get(key);
    const c = M.CHORD_BY_ID[id];
    const pcs = new Set(c.iv.map((i) => mod(root + i)));
    const bassPc = bass !== undefined && bass !== null ? mod(bass) : mod(root);
    if (!pcs.has(bassPc)) pcs.add(bassPc);
    const ess = new Set(c.ess.map((i) => mod(root + i)));
    if (bass !== undefined && bass !== null) ess.add(bassPc);
    const minStrings = Math.min(c.pcs.length >= 4 ? 4 : 3, 6);
    const found = new Map();
    for (let p = 0; p <= 12; p++) {
      const lo = Math.max(1, p), hi = p + 3;
      const opts = tuning.map((open) => {
        const o = [-1];
        if (pcs.has(mod(open))) o.push(0);
        for (let f = lo; f <= hi; f++) if (pcs.has(mod(open + f))) o.push(f);
        return o;
      });
      const cur = new Array(6);
      (function dfs(s) {
        if (s === 6) { evaluate(cur.slice()); return; }
        for (const f of opts[s]) {
          // la première corde jouée doit porter la basse
          if (f >= 0 && cur.slice(0, s).every((x) => x < 0) && mod(tuning[s] + f) !== bassPc) continue;
          cur[s] = f;
          dfs(s + 1);
        }
      })(0);
    }
    function evaluate(fr) {
      const sounding = fr.map((f, s) => (f >= 0 ? s : -1)).filter((s) => s >= 0);
      if (sounding.length < minStrings) return;
      const first = sounding[0], last = sounding[sounding.length - 1];
      let gaps = 0;
      for (let s = first; s <= last; s++) if (fr[s] < 0) gaps++;
      if (gaps > 1) return;
      const have = new Set(sounding.map((s) => mod(tuning[s] + fr[s])));
      for (const e of ess) if (!have.has(e)) return;
      const fretted = fr.filter((f) => f > 0);
      if (fretted.length) {
        const mn = Math.min(...fretted), mx = Math.max(...fretted);
        if (mx - mn > 3) return;
        if (fretted.length > 4) {
          // il faut un barré : il couvre toutes les cordes à partir de sa première corde (pas de corde à vide après)
          const atMin = fretted.filter((f) => f === mn).length;
          if (fretted.length - atMin + 1 > 4) return;
          const firstMin = fr.indexOf(mn);
          for (let s = firstMin; s < 6; s++) if (fr[s] === 0) return;
        }
      }
      const mn = fretted.length ? Math.min(...fretted) : 0;
      const mx = fretted.length ? Math.max(...fretted) : 0;
      let score = sounding.length * 1.0 + have.size * 0.6 - mn * 0.1 - gaps * 1.6 - (mx - mn) * 0.25;
      const opens = fr.filter((f) => f === 0).length;
      if (opens && mx <= 4) score += 0.4 * opens;
      if (opens && mx > 5) score -= 0.5 * opens;
      if (first <= 1) score += 0.6;
      // doublure de la tierce/septième peu utile : légère pénalité pour notes répétées
      score -= (sounding.length - have.size) * 0.15;
      const k = fr.join(',');
      if (!found.has(k) || found.get(k).score < score) found.set(k, { frets: fr, score, minFret: mn, maxFret: mx });
    }
    const all = [...found.values()].sort((a, b) => b.score - a.score);
    const picked = [];
    for (const v of all) {
      if (picked.length >= max) break;
      // diversité : pas deux doigtés trop semblables
      if (picked.some((q) => similar(q.frets, v.frets))) continue;
      picked.push(v);
    }
    picked.sort((a, b) => a.minFret - b.minFret || b.score - a.score);
    vcache.set(key, picked);
    return picked;
  }
  function similar(a, b) {
    let diff = 0;
    for (let i = 0; i < 6; i++) if (a[i] !== b[i]) diff++;
    return diff <= 1;
  }
  function voicingMidis(fr) { return fr.map((f, s) => (f >= 0 ? tuning[s] + f : -1)).filter((m) => m >= 0); }

  // Notes MIDI simples pour jouer un accord (sans doigté)
  function chordMidis(root, id, bass) {
    const v = voicings(root, id, bass, 3);
    if (v.length) return voicingMidis(v[0].frets);
    const base = 48 + mod(root);
    return M.CHORD_BY_ID[id].iv.map((i) => base + i);
  }

  // ---------- Diagramme d'accord (SVG) ----------
  function diagram(root, id, fr, opts = {}) {
    const W = opts.w || 120, H = opts.h || 150;
    const labels = opts.labels || 'deg';
    const iv = M.CHORD_BY_ID[id].iv;
    const fretted = fr.filter((f) => f > 0);
    const mx = fretted.length ? Math.max(...fretted) : 0, mn = fretted.length ? Math.min(...fretted) : 0;
    const base = mx <= 4 ? 1 : mn;
    const nF = 5, x0 = 22, x1 = W - 12, y0 = 30, y1 = H - 12;
    const sx = (x1 - x0) / 5, sy = (y1 - y0) / nF;
    let s = `<svg viewBox="0 0 ${W} ${H}" class="diag" role="img" aria-label="${M.chordSymbol(root, id)}">`;
    if (base === 1) s += `<rect x="${x0 - 1}" y="${y0 - 4}" width="${x1 - x0 + 2}" height="5" class="nut"/>`;
    else s += `<text x="${x0 - 6}" y="${y0 + sy * 0.65}" class="dfret" text-anchor="end">${base}</text>`;
    for (let i = 0; i <= nF; i++) s += `<line x1="${x0}" x2="${x1}" y1="${y0 + i * sy}" y2="${y0 + i * sy}" class="dline"/>`;
    for (let i = 0; i < 6; i++) s += `<line x1="${x0 + i * sx}" x2="${x0 + i * sx}" y1="${y0}" y2="${y1}" class="dline"/>`;
    // barré
    if (fretted.length >= 2) {
      const atMin = fr.map((f, i) => (f === mn ? i : -1)).filter((i) => i >= 0);
      if (atMin.length >= 2 && fr.slice(atMin[0]).every((f) => f !== 0)) {
        const a = atMin[0], b = atMin[atMin.length - 1];
        const y = y0 + (mn - base + 0.5) * sy;
        s += `<rect x="${x0 + a * sx - 7}" y="${y - 7}" width="${(b - a) * sx + 14}" height="14" rx="7" class="barre"/>`;
      }
    }
    for (let i = 0; i < 6; i++) {
      const f = fr[i], x = x0 + i * sx;
      if (f < 0) { s += `<text x="${x}" y="${y0 - 10}" class="dmute" text-anchor="middle">×</text>`; continue; }
      const midi = tuning[i] + f;
      const semi = mod(midi - root);
      const lab = labels === 'deg' ? M.degLabel(semi, iv) : labels === 'note' ? M.pcName(midi) : '';
      const isRoot = semi === 0;
      if (f === 0) {
        s += `<circle cx="${x}" cy="${y0 - 13}" r="6" class="dopen ${isRoot ? 'root' : ''}"/>`;
        if (lab) s += `<text x="${x}" y="${y0 - 24}" class="dlabsm" text-anchor="middle">${lab}</text>`;
      } else {
        const y = y0 + (f - base + 0.5) * sy;
        s += `<circle cx="${x}" cy="${y}" r="${Math.min(sx, sy) * 0.42}" class="ddot ${isRoot ? 'root' : ''}"/>`;
        if (lab) s += `<text x="${x}" y="${y + 3.6}" class="dlab" text-anchor="middle">${lab}</text>`;
      }
    }
    s += '</svg>';
    return s;
  }

  // ---------- Manche complet ----------
  // marks : Map clé "corde:frette" → {cls, label} ou fonction (corde, frette, midi) → {cls,label}|null
  function fretboard(opts = {}) {
    const frets = opts.frets || 15;
    const W = opts.w || 900, H = opts.h || 190;
    const x0 = 34, x1 = W - 8, y0 = 18, y1 = H - 24;
    const fw = (x1 - x0) / (frets + 0.6);
    const sy = (y1 - y0) / 5;
    const fx = (f) => (f === 0 ? x0 - fw * 0.35 : x0 + fw * 0.6 + (f - 0.5) * fw);
    let s = `<svg viewBox="0 0 ${W} ${H}" class="fb" preserveAspectRatio="xMinYMid meet">`;
    // repères
    for (const f of [3, 5, 7, 9, 15, 17]) if (f <= frets) s += `<circle cx="${fx(f)}" cy="${(y0 + y1) / 2}" r="5" class="inlay"/>`;
    if (frets >= 12) { s += `<circle cx="${fx(12)}" cy="${y0 + sy * 1.5}" r="5" class="inlay"/><circle cx="${fx(12)}" cy="${y0 + sy * 3.5}" r="5" class="inlay"/>`; }
    s += `<rect x="${x0 + fw * 0.6 - 3}" y="${y0 - 2}" width="5" height="${y1 - y0 + 4}" class="nut"/>`;
    for (let f = 1; f <= frets; f++) {
      const x = x0 + fw * 0.6 + f * fw;
      s += `<line x1="${x}" x2="${x}" y1="${y0}" y2="${y1}" class="fwire"/>`;
      s += `<text x="${fx(f)}" y="${H - 6}" class="fnum" text-anchor="middle">${f}</text>`;
    }
    for (let i = 0; i < 6; i++) {
      const y = y0 + i * sy;
      s += `<line x1="${x0 + fw * 0.6}" x2="${x1}" y1="${y}" y2="${y}" class="fstring" style="stroke-width:${0.8 + (i) * 0.35}"/>`;
    }
    // cordes : du haut (Mi aigu) vers le bas (Mi grave)
    for (let i = 0; i < 6; i++) {
      const strIdx = 5 - i;
      const y = y0 + i * sy;
      for (let f = 0; f <= frets; f++) {
        const midi = tuning[strIdx] + f;
        const mk = opts.mark ? opts.mark(strIdx, f, midi) : null;
        const x = fx(f);
        if (opts.clickable) s += `<rect x="${x - fw / 2}" y="${y - sy / 2}" width="${fw}" height="${sy}" class="fhit" data-s="${strIdx}" data-f="${f}"/>`;
        if (!mk) continue;
        s += `<g class="fmark ${mk.cls || ''}" data-s="${strIdx}" data-f="${f}"><circle cx="${x}" cy="${y}" r="${Math.min(fw, sy) * 0.4}"/>`;
        if (mk.label) s += `<text x="${x}" y="${y + 3.8}" text-anchor="middle">${mk.label}</text>`;
        s += '</g>';
      }
    }
    s += '</svg>';
    return s;
  }
  function scaleMarker(rootPc, iv, labels = 'note') {
    const set = new Set(iv.map((i) => mod(rootPc + i)));
    return (s, f, midi) => {
      const pc = mod(midi);
      if (!set.has(pc)) return null;
      const semi = mod(pc - rootPc);
      const lab = labels === 'deg' ? M.degLabel(semi, iv).replace('11', '4').replace('9', '2').replace('13', '6') : M.pcName(pc);
      return { cls: semi === 0 ? 'root' : '', label: lab };
    };
  }

  return { TUNINGS, setTuning, getTuning, voicings, voicingMidis, chordMidis, diagram, fretboard, scaleMarker };
})();
