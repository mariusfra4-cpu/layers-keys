// Partition (portée en clé de sol, guitare = une octave plus bas que l'écrit) + tablature, synchronisées à la lecture.
'use strict';
const Score = (() => {
  const mod = M.mod;
  const PPB = 58; // pixels par temps
  const X0 = 64; // marge (clé, TAB)
  const Y = { chord: 16, staff: 46, sp: 8, tab: 150, tsp: 12 };
  const H = Y.tab + Y.tsp * 5 + 26;

  // ---------- temps ↔ position en temps (beats) ----------
  function timeline(beats, duration) {
    const b = beats.length >= 2 ? beats.slice() : [0, 0.5];
    const per = b.length > 1 ? (b[b.length - 1] - b[0]) / (b.length - 1) : 0.5;
    while (b[b.length - 1] < duration + per) b.push(b[b.length - 1] + per);
    const toBeat = (t) => {
      if (t <= b[0]) return (t - b[0]) / per;
      let lo = 0, hi = b.length - 1;
      while (hi - lo > 1) { const m = (lo + hi) >> 1; if (b[m] <= t) lo = m; else hi = m; }
      return lo + (t - b[lo]) / (b[hi] - b[lo]);
    };
    const toTime = (x) => {
      const i = Math.max(0, Math.min(b.length - 2, Math.floor(x)));
      return b[i] + (x - i) * (b[i + 1] - b[i]);
    };
    return { toBeat, toTime, count: b.length };
  }

  // ---------- orthographe et position sur la portée ----------
  const LET_S = [0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6], ACC_S = [0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0];
  const LET_F = [0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6], ACC_F = [0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0];
  function spell(midi, flats) {
    const w = midi + 12; // écriture guitare : une octave au-dessus du son réel
    const pc = mod(w), oct = Math.floor(w / 12) - 1;
    const letter = (flats ? LET_F : LET_S)[pc];
    return { d: letter + 7 * oct, acc: (flats ? ACC_F : ACC_S)[pc] };
  }
  const yOfD = (d) => Y.staff + (38 - d) * (Y.sp / 2); // ligne du haut = F5 (d = 38)

  // ---------- doigté de la mélodie (programmation dynamique) ----------
  function fingering(notes) {
    const tu = G.getTuning();
    const cands = notes.map((n) => {
      const c = [];
      for (let s = 0; s < 6; s++) { const f = n.midi - tu[s]; if (f >= 0 && f <= 17) c.push({ s, f }); }
      return c.length ? c : [{ s: 5, f: Math.max(0, n.midi - tu[5]) }];
    });
    const cost = [], back = [];
    cands.forEach((cs, i) => {
      cost.push(new Float64Array(cs.length)); back.push(new Int32Array(cs.length));
      cs.forEach((c, k) => {
        const own = c.f * 0.08 + (c.f > 12 ? 1 : 0);
        if (!i) { cost[i][k] = own; return; }
        let best = Infinity, bi = 0;
        cands[i - 1].forEach((p, j) => {
          const pf = p.f || c.f, cf = c.f || p.f;
          const v = cost[i - 1][j] + Math.abs(cf - pf) * 0.6 + Math.abs(c.s - p.s) * 0.15 + (Math.abs(cf - pf) > 4 ? 2 : 0);
          if (v < best) { best = v; bi = j; }
        });
        cost[i][k] = best + own; back[i][k] = bi;
      });
    });
    const out = new Array(notes.length);
    if (!notes.length) return out;
    let k = 0;
    const last = cost[cost.length - 1];
    for (let j = 1; j < last.length; j++) if (last[j] < last[k]) k = j;
    for (let i = notes.length - 1; i >= 0; i--) { out[i] = cands[i][k]; k = back[i][k]; }
    return out;
  }
  // doigté d'accord proche de la position précédente
  function chordVoicing(root, id, bass, prevMin) {
    const vs = G.voicings(root, id, bass, 10);
    if (!vs.length) return null;
    if (prevMin === null) return vs.find((v) => v.minFret <= 5) || vs[0];
    return vs.slice().sort((a, b) => Math.abs(a.minFret - prevMin) - Math.abs(b.minFret - prevMin) + (b.score - a.score) * 0.05)[0];
  }

  // ---------- dessin ----------
  // r : résultat de l'analyse ; opts : { transpose, mode: 'melody' | 'chords', flats }
  function build(r, opts) {
    const tr = opts.transpose || 0;
    const tl = timeline(r.beats, r.duration);
    const nBeats = Math.ceil(tl.toBeat(r.duration)) + 1;
    const W = X0 + nBeats * PPB + 40;
    const xb = (beat) => X0 + beat * PPB;
    const q = (t) => Math.round(tl.toBeat(t) * 4) / 4; // quantification à la double-croche
    let s = `<svg class="score" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" xmlns="http://www.w3.org/2000/svg">`;
    // portée
    for (let i = 0; i < 5; i++) s += `<line x1="0" x2="${W}" y1="${Y.staff + i * Y.sp}" y2="${Y.staff + i * Y.sp}" class="sl"/>`;
    for (let i = 0; i < 6; i++) s += `<line x1="0" x2="${W}" y1="${Y.tab + i * Y.tsp}" y2="${Y.tab + i * Y.tsp}" class="tl"/>`;
    // barres de mesure (4 temps)
    for (let b = 0; b <= nBeats; b += 4) {
      const x = xb(b) - 6;
      s += `<line x1="${x}" x2="${x}" y1="${Y.staff}" y2="${Y.staff + 4 * Y.sp}" class="bl"/><line x1="${x}" x2="${x}" y1="${Y.tab}" y2="${Y.tab + 5 * Y.tsp}" class="bl"/>`;
      s += `<text x="${x + 3}" y="${Y.staff - 6}" class="bn">${b / 4 + 1}</text>`;
    }
    // accords (symboles) + doigtés en mode « accords »
    const segs = r.segments.filter((g) => g.root >= 0);
    let prevMin = null, lastX = -1e9;
    for (const g of segs) {
      const b0 = Math.round(tl.toBeat(g.start) * 2) / 2;
      const x = xb(b0);
      const root = mod(g.root + tr), bass = g.bass === undefined ? undefined : mod(g.bass + tr);
      const sym = M.chordSymbol(root, g.id, bass);
      if (x - lastX > 34) { s += `<text x="${x}" y="${Y.chord}" class="cs">${sym}</text>`; lastX = x + sym.length * 4; }
      if (opts.mode === 'chords') {
        const v = chordVoicing(root, g.id, bass, prevMin);
        if (v) {
          prevMin = v.minFret;
          const b1 = Math.max(b0 + 1, Math.round(tl.toBeat(g.end) * 2) / 2);
          for (let bb = b0; bb < b1; bb += 1) {
            const xx = xb(bb);
            v.frets.forEach((f, st) => { if (f >= 0) s += `<text x="${xx}" y="${Y.tab + (5 - st) * Y.tsp + 4}" class="tn ${bb === b0 ? '' : 'rep'}">${f}</text>`; });
            // barre oblique de rythme sur la portée
            s += `<line x1="${xx - 4}" x2="${xx + 4}" y1="${Y.staff + 2.5 * Y.sp}" y2="${Y.staff + 1.5 * Y.sp}" class="slash"/>`;
          }
        }
      }
    }
    // mélodie
    if (opts.mode !== 'chords') {
      const notes = (r.notes || []).map((n) => {
        const b0 = q(n.t), b1 = Math.max(b0 + 0.25, q(n.t + n.d));
        return { midi: n.midi + tr, b0, dur: b1 - b0, t: n.t };
      }).filter((n, i, a) => !i || n.b0 > a[i - 1].b0);
      // legato : une note dure jusqu'à la suivante si l'écart est petit (évite les fausses doubles-croches)
      for (let i = 0; i < notes.length - 1; i++) {
        const gap = notes[i + 1].b0 - notes[i].b0;
        notes[i].dur = gap - notes[i].dur <= 0.5 ? gap : Math.min(notes[i].dur, gap);
      }
      const fing = fingering(notes);
      notes.forEach((n, i) => {
        const x = xb(n.b0);
        const sp = spell(n.midi, opts.flats);
        const y = yOfD(sp.d);
        // lignes supplémentaires
        for (let d = 28; d >= sp.d; d -= 2) s += `<line x1="${x - 8}" x2="${x + 8}" y1="${yOfD(d)}" y2="${yOfD(d)}" class="sl"/>`;
        for (let d = 40; d <= sp.d; d += 2) s += `<line x1="${x - 8}" x2="${x + 8}" y1="${yOfD(d)}" y2="${yOfD(d)}" class="sl"/>`;
        if (sp.acc) s += `<text x="${x - 13}" y="${y + 4}" class="acc">${sp.acc > 0 ? '♯' : '♭'}</text>`;
        const q16 = Math.round(n.dur * 4);
        const hollow = q16 >= 8;
        s += `<ellipse cx="${x}" cy="${y}" rx="4.6" ry="3.4" transform="rotate(-20 ${x} ${y})" class="nh ${hollow ? 'hollow' : ''}"/>`;
        if (q16 < 16) {
          const up = sp.d < 34;
          const sx = up ? x + 4.2 : x - 4.2, sy2 = up ? y - 26 : y + 26;
          s += `<line x1="${sx}" x2="${sx}" y1="${y}" y2="${sy2}" class="stem"/>`;
          const flags = q16 <= 1 ? 2 : q16 <= 3 ? 1 : 0;
          for (let k = 0; k < flags; k++) {
            const fy = sy2 + (up ? k * 6 : -k * 6);
            s += `<path d="M${sx} ${fy} q 7 ${up ? 5 : -5} 6 ${up ? 13 : -13}" class="flag"/>`;
          }
        }
        if (q16 === 3 || q16 === 6 || q16 === 12) s += `<circle cx="${x + 8}" cy="${y}" r="1.6" class="dot"/>`;
        const fg = fing[i];
        if (fg) s += `<rect x="${x - 6}" y="${Y.tab + (5 - fg.s) * Y.tsp - 6}" width="12" height="12" class="tbg"/><text x="${x}" y="${Y.tab + (5 - fg.s) * Y.tsp + 4}" class="tn">${fg.f}</text>`;
      });
      if (!notes.length) s += `<text x="${X0}" y="${Y.staff + 20}" class="empty">Pas de mélodie détectée : essaie le mode « Accords ».</text>`;
    }
    s += `<rect id="scoreCursor" x="${X0 - 1}" y="4" width="2" height="${H - 8}" class="cur"/></svg>`;
    return { svg: s, tl, W, xb };
  }

  // Gouttière fixe à gauche (clé de sol 8, TAB)
  function gutter() {
    return `<svg class="sgut" width="${X0 - 8}" height="${H}" viewBox="0 0 ${X0 - 8} ${H}">
      <rect x="0" y="0" width="${X0 - 8}" height="${H}" class="gbg"/>
      ${[0, 1, 2, 3, 4].map((i) => `<line x1="0" x2="${X0 - 8}" y1="${Y.staff + i * Y.sp}" y2="${Y.staff + i * Y.sp}" class="sl"/>`).join('')}
      ${[0, 1, 2, 3, 4, 5].map((i) => `<line x1="0" x2="${X0 - 8}" y1="${Y.tab + i * Y.tsp}" y2="${Y.tab + i * Y.tsp}" class="tl"/>`).join('')}
      <path d="${CLEF}" transform="translate(10 ${Y.staff - 14}) scale(0.95)" class="clef"/>
      <text x="22" y="${Y.staff + 4 * Y.sp + 26}" class="eight">8</text>
      <text x="8" y="${Y.tab + 18}" class="tabl">T</text><text x="8" y="${Y.tab + 33}" class="tabl">A</text><text x="8" y="${Y.tab + 48}" class="tabl">B</text>
    </svg>`;
  }
  // Clé de sol simplifiée (tracé vectoriel, sans police musicale)
  const CLEF = 'M14 62 C 6 62 4 54 10 52 C 15 50 18 57 13 60 M13 60 C 17 60 19 56 18 50 L 12 6 C 11 0 18 -2 19 5 C 20 13 8 20 6 30 C 4 40 12 47 19 46 C 27 45 29 35 22 32 C 15 29 9 35 12 40';

  // ---------- composant : défilement synchronisé ----------
  function mount(el, r, opts, onSeek) {
    const built = build(r, opts);
    el.innerHTML = `<div class="sview">${gutter()}<div class="sscroll"><div class="sinner">${built.svg}</div></div></div>`;
    const scroll = el.querySelector('.sscroll'), inner = el.querySelector('.sinner'), cur = el.querySelector('#scoreCursor');
    let userScroll = 0;
    scroll.addEventListener('touchstart', () => { userScroll = Date.now(); }, { passive: true });
    scroll.addEventListener('wheel', () => { userScroll = Date.now(); }, { passive: true });
    inner.addEventListener('click', (e) => {
      const rect = inner.getBoundingClientRect();
      const x = e.clientX - rect.left;
      onSeek(built.tl.toTime((x - X0) / PPB));
    });
    return {
      update(t) {
        const x = built.xb(built.tl.toBeat(t));
        cur.setAttribute('x', x - 1);
        if (Date.now() - userScroll > 2500) scroll.scrollLeft = Math.max(0, x - scroll.clientWidth * 0.3);
      },
    };
  }
  return { mount, build, fingering, spell };
})();
