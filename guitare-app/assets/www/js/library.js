// Onglets Accords, Progressions et Théorie.
'use strict';
const Library = (() => {
  const { $, $$ } = App;
  const mod = M.mod;

  // =================== Dictionnaire d'accords ===================
  const C = { root: 0, id: 'maj7', bass: undefined };
  const GROUPS = [
    ['Triades', ['maj', 'min', 'dim', 'aug', 'sus2', 'sus4', '5']],
    ['Septièmes et sixtes', ['maj7', '7', 'm7', 'm7b5', 'dim7', 'mmaj7', '7sus4', '6', 'm6']],
    ['Extensions (R&B, neo-soul, jazz)', ['add9', 'madd9', 'maj9', '9', 'm9', '9sus4', 'm11', '13', 'maj13', 'm13', '69']],
    ['Altérés (jazz, gospel)', ['7b9', '7s9', '7s5', '7b5', '7s11', 'maj7s11', '13s11', '13b9', '7alt']],
  ];
  function renderChordPickers() {
    $('#rootPick').innerHTML = Array.from({ length: 12 }, (_, i) => `<button data-r="${i}" class="${i === C.root ? 'on' : ''}">${M.pcName(i)}</button>`).join('');
    $('#typePick').innerHTML = GROUPS.map(([g, ids]) => `<div class="tgroup">${g}</div>` + ids.map((id) => `<button data-id="${id}" class="${id === C.id ? 'on' : ''}">${M.CHORD_BY_ID[id].sym || 'maj'}</button>`).join('')).join('');
  }
  function renderChord() {
    const c = M.CHORD_BY_ID[C.id];
    M.setPreferFlats([1, 3, 8, 10].includes(C.root) ? true : C.root === 6 ? false : null);
    const vs = G.voicings(C.root, C.id, C.bass, 12);
    const sym = M.chordSymbol(C.root, C.id, C.bass);
    $('#chordInfo').innerHTML = `<div class="row between"><div class="chordtitle">${sym}</div><button class="btn small" id="chPlay">▶ Écouter</button></div>
      <div>${c.name} · <span class="muted">${c.style}</span></div>
      <div class="formula">${c.iv.map((i) => `<span class="${i === 0 ? 'r' : ''}">${M.degLabel(i, c.iv)} = ${M.pcName(C.root + i)}</span>`).join('')}</div>
      <div class="muted small">Notes essentielles : ${c.ess.map((i) => M.degLabel(i, c.iv)).join(', ')} · ${vs.length} doigté${vs.length > 1 ? 's' : ''} trouvé${vs.length > 1 ? 's' : ''}</div>`;
    $('#chordDiags').innerHTML = vs.map((v) => App.chordCard(C.root, C.id, C.bass, { voicing: v, showFrets: true })).join('') || '<p class="muted">Aucun doigté jouable trouvé pour cet accord dans cet accordage.</p>';
    $('#chPlay').addEventListener('click', () => Synth.strum(G.chordMidis(C.root, C.id, C.bass)));
    M.setPreferFlats(null);
  }
  function initChords() {
    renderChordPickers();
    renderChord();
    $('#rootPick').addEventListener('click', (e) => { const b = e.target.closest('button'); if (!b) return; C.root = +b.dataset.r; C.bass = undefined; renderChordPickers(); renderChord(); });
    $('#typePick').addEventListener('click', (e) => { const b = e.target.closest('button'); if (!b) return; C.id = b.dataset.id; C.bass = undefined; renderChordPickers(); renderChord(); });
    $('#chordSearch').addEventListener('input', (e) => {
      const p = M.parseChord(e.target.value);
      if (!p) return;
      Object.assign(C, p); renderChordPickers(); renderChord();
    });
    App.segmented($('#chordMode'), (v) => { $('#chords-lib').classList.toggle('hidden', v !== 'lib'); $('#chords-find').classList.toggle('hidden', v !== 'find'); if (v === 'find') renderFinder(); });
    initFinder();
  }

  // =================== Identifier un accord ===================
  const fz = [-1, -1, -1, -1, -1, -1];
  function renderFinder() {
    const sel = new Set(fz.map((f, s) => (f >= 0 ? s + ':' + f : '')));
    $('#finderBoard').innerHTML = `<div class="fbwrap">${G.fretboard({ frets: 12, clickable: true, mark: (s, f, midi) => (sel.has(s + ':' + f) ? { cls: 'sel', label: M.pcName(midi) } : null) })}</div>
      <div class="muted small">Cordes : ${fz.map((f, s) => `${['E', 'A', 'D', 'G', 'B', 'e'][s]}=${f < 0 ? '×' : f}`).join(' · ')}</div>`;
    const midis = fz.map((f, s) => (f >= 0 ? G.getTuning()[s] + f : -1)).filter((m) => m >= 0);
    const out = $('#finderResult');
    if (midis.length < 2) { out.innerHTML = '<p class="muted">Place au moins 2 notes.</p>'; return; }
    const res = M.identify(midis.map((m) => mod(m)), mod(midis[0]));
    const notes = [...new Set(midis.map((m) => M.pcName(m)))].join(' ');
    if (!res.length) { out.innerHTML = `<p>Notes : <b>${notes}</b></p><p class="muted">Pas d’accord standard reconnu.</p>`; return; }
    const f = res[0];
    out.innerHTML = `<div class="res">${M.chordSymbol(f.root, f.id, f.bass)}</div><div>${M.CHORD_BY_ID[f.id].name} · notes : ${notes}</div>
      ${res.length > 1 ? `<div class="res2">Autres noms possibles : ${res.slice(1).map((r) => M.chordSymbol(r.root, r.id, r.bass)).join(', ')}</div>` : ''}`;
  }
  function initFinder() {
    $('#finderBoard').addEventListener('click', (e) => {
      const h = e.target.closest('[data-s]');
      if (!h) return;
      const s = +h.dataset.s, f = +h.dataset.f;
      fz[s] = fz[s] === f ? -1 : f;
      renderFinder();
    });
    $('#finderClear').addEventListener('click', () => { fz.fill(-1); renderFinder(); });
    $('#finderPlay').addEventListener('click', () => Synth.strum(fz.map((f, s) => (f >= 0 ? G.getTuning()[s] + f : -1)).filter((m) => m >= 0)));
  }

  // =================== Progressions ===================
  let style = 'Tous', playing = null;
  const P = { prog: null, key: 0 };
  function renderProgList() {
    $('#styleChips').innerHTML = ['Tous', ...PROG_STYLES].map((s) => `<button class="${s === style ? 'on' : ''}" data-s="${s}">${s}</button>`).join('');
    const list = PROGS.filter((p) => style === 'Tous' || p.style === style);
    $('#progList').innerHTML = list.map((p) => {
      const romans = p.chords.map(([s, id, , b]) => M.romanOf(s, id, { tonic: 0, mode: p.mode }) + (b !== undefined ? '/' + (b === 4 ? '3' : b === 7 ? '5' : b) : ''));
      return `<div class="pitem" data-p="${p.id}"><div class="ps">${p.style}</div><div class="pn">${p.name}</div><div class="pc">${romans.join(' – ')}</div></div>`;
    }).join('');
  }
  function openProg(id) {
    const p = PROGS.find((x) => x.id === id);
    if (!p) return;
    P.prog = p;
    P.key = p.mode === 'minor' ? 9 : 0;
    $('#progList').classList.add('hidden'); $('#styleChips').classList.add('hidden');
    $('#progDetail').classList.remove('hidden');
    renderProg();
    window.scrollTo(0, 0);
  }
  function renderProg() {
    const p = P.prog;
    const key = { tonic: P.key, mode: p.mode };
    M.setPreferFlats(M.keyPrefersFlats(key));
    const ch = p.chords.map(([s, id, beats, b]) => ({ root: mod(P.key + s), id, beats, bass: b === undefined ? undefined : mod(P.key + b), roman: M.romanOf(mod(P.key + s), id, key) }));
    $('#progDetail').innerHTML = `<button class="back" id="progBack">‹ Progressions</button>
      <div class="card"><div class="ps muted small">${p.style}</div><h3>${p.name}</h3><p>${p.desc}</p><p class="muted">💡 ${p.tip}</p>
      <div class="row wrap"><select id="progKey">${Array.from({ length: 12 }, (_, i) => `<option value="${i}" ${i === P.key ? 'selected' : ''}>${M.keyName({ tonic: i, mode: p.mode })}</option>`).join('')}</select>
      <label class="muted small">Tempo <input type="number" id="progBpm" value="${p.bpm}" min="40" max="220" style="width:64px" class="search"></label>
      <button class="btn primary" id="progPlay">${playing ? '■ Stop' : '▶ Jouer en boucle'}</button></div>
      <div class="pchords" id="progChords" data-noplay>${ch.map((c, i) => App.chordCard(c.root, c.id, c.bass, { roman: c.roman }).replace('class="dcard"', `class="dcard" data-i="${i}"`)).join('')}</div>
      <p class="muted small">Grille : ${ch.map((c) => M.chordSymbol(c.root, c.id, c.bass) + (c.beats !== 4 ? ` (${c.beats} t.)` : '')).join(' | ')}</p></div>`;
    M.setPreferFlats(null);
    $('#progBack').addEventListener('click', closeProg);
    $('#progKey').addEventListener('change', (e) => { P.key = +e.target.value; stopProg(); renderProg(); });
    $('#progPlay').addEventListener('click', () => { if (playing) stopProg(); else playProg(ch); renderProgButton(); });
    $$('#progChords .dcard').forEach((d) => d.addEventListener('click', () => App.playCard(d)));
  }
  function renderProgButton() { const b = $('#progPlay'); if (b) b.textContent = playing ? '■ Stop' : '▶ Jouer en boucle'; }
  function playProg(ch) {
    const bpm = Math.max(40, Math.min(220, +$('#progBpm').value || 90));
    const beat = 60 / bpm;
    const midis = ch.map((c) => G.chordMidis(c.root, c.id, c.bass));
    let i = 0, next = Synth.now() + 0.1;
    const st = { timer: 0 };
    playing = st;
    const sched = () => {
      while (next < Synth.now() + 0.25) {
        const c = ch[i % ch.length];
        const ms = midis[i % ch.length];
        const when = next - Synth.now();
        // basse + accord gratté sur chaque demi-mesure
        Synth.note(ms[0] - (ms[0] >= 52 ? 12 : 0), when, 0.6, beat * c.beats);
        for (let b = 0; b < c.beats; b += 2) Synth.strum(ms, when + b * beat, { dur: beat * Math.min(2, c.beats), vel: b ? 0.4 : 0.55, up: false });
        const idx = i % ch.length;
        setTimeout(() => { $$('#progChords .dcard').forEach((d) => d.classList.toggle('now', +d.dataset.i === idx)); }, Math.max(0, when * 1000));
        next += beat * c.beats;
        i++;
      }
      st.timer = setTimeout(sched, 60);
    };
    sched();
  }
  function stopProg() { if (playing) clearTimeout(playing.timer); playing = null; $$('#progChords .dcard.now').forEach((d) => d.classList.remove('now')); renderProgButton(); }
  function closeProg() { stopProg(); $('#progDetail').classList.add('hidden'); $('#progList').classList.remove('hidden'); $('#styleChips').classList.remove('hidden'); }
  function initProgs() {
    renderProgList();
    $('#styleChips').addEventListener('click', (e) => { const b = e.target.closest('button'); if (!b) return; style = b.dataset.s; renderProgList(); });
    $('#progList').addEventListener('click', (e) => { const it = e.target.closest('.pitem'); if (it) openProg(it.dataset.p); });
  }

  // =================== Théorie ===================
  const S = { root: 0, scale: 'major', labels: 'note' };
  function renderScale() {
    const sc = M.SCALE_BY_ID[S.scale];
    M.setPreferFlats([1, 3, 5, 8, 10].includes(S.root) || ['minor', 'dorian', 'phryg', 'locrian', 'pentam', 'blues'].includes(S.scale) && [0, 2, 5, 7].includes(S.root));
    $('#scBoard').innerHTML = G.fretboard({ frets: 15, mark: G.scaleMarker(S.root, sc.iv, S.labels) });
    const mode = M.MODES.find((m) => m.iv.join() === sc.iv.join());
    $('#scInfo').innerHTML = `${sc.iv.map((i) => M.pcName(S.root + i)).join(' · ')}${mode ? ` — ${mode.color} <i>${mode.use}</i>` : ''}`;
    M.setPreferFlats(null);
  }
  function renderLessonList() {
    $('#lessonList').innerHTML = LESSON_CATS.map((c) => `<div class="lcat">${c}</div>` + LESSONS.filter((l) => l.cat === c).map((l) => `<div class="litem" data-l="${l.id}">${l.title}</div>`).join('')).join('');
  }
  function openLesson(id) {
    const l = LESSONS.find((x) => x.id === id);
    if (!l) return;
    App.show('theory');
    const idx = LESSONS.indexOf(l);
    const prev = LESSONS[idx - 1], next = LESSONS[idx + 1];
    $('#theoryHome').classList.add('hidden');
    const v = $('#lessonView');
    v.classList.remove('hidden');
    v.innerHTML = `<button class="back" id="lessonBack">‹ Théorie</button><article class="lesson"><div class="muted small">${l.cat}</div><h2>${l.title}</h2>${l.body}</article>
      <div class="row between">${prev ? `<button class="btn small" data-go="${prev.id}">‹ ${prev.title}</button>` : '<span></span>'}${next ? `<button class="btn small" data-go="${next.id}">${next.title} ›</button>` : ''}</div>`;
    App.hydrate(v);
    $('#lessonBack').addEventListener('click', closeLesson);
    $$('[data-go]', v).forEach((b) => b.addEventListener('click', () => openLesson(b.dataset.go)));
    window.scrollTo(0, 0);
  }
  function closeLesson() { $('#lessonView').classList.add('hidden'); $('#theoryHome').classList.remove('hidden'); }
  function initTheory() {
    $('#scRoot').innerHTML = Array.from({ length: 12 }, (_, i) => `<option value="${i}">${M.pcName(i)}</option>`).join('');
    $('#scType').innerHTML = M.SCALES.map((s) => `<option value="${s.id}">${s.name}</option>`).join('');
    $('#scRoot').addEventListener('change', (e) => { S.root = +e.target.value; renderScale(); });
    $('#scType').addEventListener('change', (e) => { S.scale = e.target.value; renderScale(); });
    App.segmented($('#scLabels'), (v) => { S.labels = v; renderScale(); });
    $('#scPlay').addEventListener('click', () => {
      const sc = M.SCALE_BY_ID[S.scale];
      const base = 52 + mod(S.root - 4);
      const up = sc.iv.concat([12]).map((i) => base + i);
      Synth.arpeggio(up.concat(up.slice(0, -1).reverse()), 0, 0.2, 0.6);
    });
    $('#lessonList').addEventListener('click', (e) => { const it = e.target.closest('.litem'); if (it) openLesson(it.dataset.l); });
    renderScale();
    renderLessonList();
  }

  initChords(); initProgs(); initTheory();
  App.onSettings(() => { renderChordPickers(); renderChord(); renderScale(); if (P.prog && !$('#progDetail').classList.contains('hidden')) renderProg(); });
  return { openProg, openLesson };
})();
