// Interface des accords de passage (onglet Progressions).
'use strict';
const PassingUI = (() => {
  const { $, $$ } = App;
  const S = { root: 0, type: 'maj7', style: 'Tous', playing: 0, list: [] };
  const TYPE_NAMES = { maj7: 'maj7', maj: 'majeur', maj9: 'maj9', '69': '6/9', min: 'mineur', m7: 'm7', m9: 'm9', m6: 'm6', '7': '7 (dominante)', '9': '9', '13': '13' };

  function chip(c, rel, cls = '') { return `<span class="cchip ${cls}"><b>${M.chordSymbol(c.root, c.id, c.bass)}</b><i>${rel}</i></span>`; }
  function render() {
    $('#passStyles').innerHTML = Passing.STYLES.map((s) => `<button class="${s === S.style ? 'on' : ''}" data-s="${s}">${s}</button>`).join('');
    const list = Passing.forTarget(S.root, S.type, S.style);
    const target = { root: S.root, id: S.type };
    M.setPreferFlats([1, 3, 5, 8, 10].includes(S.root) ? true : [2, 4, 6, 9, 11].includes(S.root) && !Passing.isMinor(S.type) ? false : null);
    const groups = new Map();
    list.forEach((a, i) => { if (!groups.has(a.rule)) groups.set(a.rule, []); groups.get(a.rule).push({ a, i }); });
    $('#passList').innerHTML = `<p class="muted small">${list.length} façons d’arriver sur <b>${M.chordSymbol(S.root, S.type)}</b>${S.style !== 'Tous' ? ' en style ' + S.style : ''}. Touche ▶ pour écouter, « doigtés » pour les diagrammes.</p>` +
      [...groups.entries()].map(([r, items]) => `<div class="prule"><div class="tags">${r.styles.join(' · ')} · niveau ${r.level}</div><h4>${r.name}</h4><p class="why">${r.why}</p>
        ${items.map(({ a, i }) => `<div class="pline" data-i="${i}"><button class="pb" data-play="${i}">▶</button>
          ${a.chords.map((c) => chip(c, Passing.relRoman(c, S.root))).join('<span class="parrow">›</span>')}<span class="parrow">›</span>${chip(a.bassTarget !== undefined ? { ...target, bass: a.bassTarget } : target, 'I', 'tgt')}
          <button class="pd" data-d="${i}">doigtés</button>${a.note ? `<div class="muted small" style="width:100%">${a.note}</div>` : ''}</div>`).join('')}</div>`).join('');
    M.setPreferFlats(null);
    S.list = list;
  }
  function seqOf(a) { return a.chords.concat([{ root: S.root, id: S.type, bass: a.bassTarget }]); }
  function play(i) {
    const seq = seqOf(S.list[i]);
    const beat = 60 / Math.max(40, Math.min(200, +$('#passBpm').value || 80));
    const token = ++S.playing;
    const line = $(`.pline[data-i="${i}"]`);
    seq.forEach((c, k) => {
      const dur = (k === seq.length - 1 ? 4 : 2) * beat;
      const when = k * 2 * beat;
      const ms = G.chordMidis(c.root, c.id, c.bass);
      Synth.note(ms[0] - (ms[0] >= 52 ? 12 : 0), when, 0.55, dur);
      Synth.strum(ms, when, { dur, vel: 0.5 });
      setTimeout(() => { if (token === S.playing && line) $$('.cchip', line).forEach((x, j) => x.classList.toggle('now', j === k)); }, when * 1000);
    });
    setTimeout(() => { if (token === S.playing && line) $$('.cchip', line).forEach((x) => x.classList.toggle('now', false)); }, (seq.length * 2 + 2) * beat * 1000);
  }
  function diags(i) {
    const line = $(`.pline[data-i="${i}"]`);
    const old = $('.pdiags', line);
    if (old) { line.removeChild(old); return; }
    const d = document.createElement('div');
    d.className = 'pdiags';
    d.innerHTML = seqOf(S.list[i]).map((c) => App.chordCard(c.root, c.id, c.bass)).join('');
    line.appendChild(d);
  }
  // ouvre le générateur vers un accord donné (depuis l'analyse d'une chanson)
  function openFor(root, id) {
    S.root = root;
    S.type = Passing.TARGETS.includes(id) ? id : Passing.isMinor(id) ? 'm7' : M.CHORD_BY_ID[id].iv.includes(10) ? '7' : 'maj7';
    App.show('progs');
    $('#progMode button[data-v="pass"]').click();
    $('#passRoot').value = S.root; $('#passType').value = S.type;
    render();
  }
  function init() {
    $('#passCount').textContent = `${Passing.total().toLocaleString('fr-FR')} enchaînements générés (${Passing.RULES.length} techniques × 12 tonalités × ${Passing.TARGETS.length} types d’accord d’arrivée).`;
    $('#passRoot').innerHTML = Array.from({ length: 12 }, (_, i) => `<option value="${i}">${M.pcName(i)}</option>`).join('');
    $('#passType').innerHTML = Passing.TARGETS.map((t) => `<option value="${t}">${TYPE_NAMES[t]}</option>`).join('');
    $('#passType').value = S.type;
    $('#passRoot').addEventListener('change', (e) => { S.root = +e.target.value; render(); });
    $('#passType').addEventListener('change', (e) => { S.type = e.target.value; render(); });
    $('#passStyles').addEventListener('click', (e) => { const b = e.target.closest('button'); if (b) { S.style = b.dataset.s; render(); } });
    $('#passList').addEventListener('click', (e) => {
      const p = e.target.closest('[data-play]'); if (p) { play(+p.dataset.play); return; }
      const d = e.target.closest('[data-d]'); if (d) diags(+d.dataset.d);
    });
    App.segmented($('#progMode'), (v) => {
      $('#passView').classList.toggle('hidden', v !== 'pass');
      $('#progListWrap').classList.toggle('hidden', v === 'pass');
      if (v === 'pass') render();
    });
  }
  init();
  App.onSettings(() => { if (!$('#passView').classList.contains('hidden')) render(); });
  return { openFor };
})();
