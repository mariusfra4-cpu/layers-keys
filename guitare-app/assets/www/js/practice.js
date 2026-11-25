// Onglet Exercices : liste, séance, score.
'use strict';
const Practice = (() => {
  const { $, $$ } = App;
  const S = { ex: null, lvl: 0, q: null, answered: false, ok: 0, total: 0 };

  function renderList() {
    const st = EX.loadStats();
    const groups = [...new Set(EX.LIST.map((e) => e.group))];
    $('#exList').innerHTML = groups.map((g) => `<div class="lcat">${g}</div>` + EX.LIST.filter((e) => e.group === g).map((e) => {
      const s = st[e.id];
      const pct = s && s.total ? Math.round((100 * s.ok) / s.total) + ' % · record ' + s.best : '';
      return `<div class="exitem" data-e="${e.id}"><span class="es">${pct}</span><div class="en">${e.name}</div><div class="ed">${e.desc}</div></div>`;
    }).join('')).join('');
  }
  function open(id) {
    S.ex = EX.BY_ID[id];
    S.lvl = App.load('gx-lvl-' + id, 0);
    if (S.lvl >= S.ex.levels.length) S.lvl = 0;
    S.ok = 0; S.total = 0;
    $('#exList').classList.add('hidden');
    $('#exView').classList.remove('hidden');
    next();
  }
  function close() { $('#exView').classList.add('hidden'); $('#exList').classList.remove('hidden'); renderList(); }
  function next() {
    S.q = S.ex.gen(S.lvl);
    S.answered = false;
    const q = S.q, e = S.ex;
    const grid = e.grid ? 'grid12' : '';
    $('#exView').innerHTML = `<button class="back" id="exBack">‹ Exercices</button>
      <div class="exbox"><div class="exscore"><span>${e.name}</span><span>${S.ok} / ${S.total}${S.total ? ' · ' + Math.round((100 * S.ok) / S.total) + ' %' : ''}</span></div>
      <div class="lvls">${e.levels.map((l, i) => `<button data-l="${i}" class="${i === S.lvl ? 'on' : ''}">${l}</button>`).join('')}</div>
      <div class="exprompt">${q.prompt}</div>${q.visual ? `<div class="fbwrap">${q.visual}</div>` : ''}
      ${q.play && !q.playAfter ? '<div class="row" style="justify-content:center"><button class="btn primary" id="exPlay">▶ Réécouter</button></div>' : ''}
      <div class="choices ${grid}">${q.choices.map((c, i) => `<button data-i="${i}">${c}</button>`).join('')}</div>
      <div class="exfeed" id="exFeed"></div>
      <div class="row" style="justify-content:flex-end"><button class="btn" id="exNext">Suivant ›</button></div></div>`;
    $('#exBack').addEventListener('click', close);
    $('#exNext').addEventListener('click', next);
    $$('.lvls button').forEach((b) => b.addEventListener('click', () => { S.lvl = +b.dataset.l; App.save('gx-lvl-' + e.id, S.lvl); next(); }));
    if ($('#exPlay')) $('#exPlay').addEventListener('click', () => q.play());
    $$('.choices button').forEach((b) => b.addEventListener('click', () => answer(+b.dataset.i)));
    if (q.play && !q.playAfter) setTimeout(() => q.play(), 150);
  }
  function answer(i) {
    if (S.answered) return;
    S.answered = true;
    const q = S.q;
    const ok = i === q.answer;
    S.total++; if (ok) S.ok++;
    const st = EX.record(S.ex.id, ok);
    $$('.choices button').forEach((b) => { const k = +b.dataset.i; if (k === q.answer) b.classList.add('ok'); else if (k === i) b.classList.add('ko'); });
    $('#exFeed').innerHTML = `${ok ? '✅ <b>Bravo !</b>' : '❌ <b>Raté.</b>'} ${q.explain || ''}${st.streak > 2 ? ` <span class="muted">· série de ${st.streak}</span>` : ''}
      ${q.play ? ' <button class="pl" id="exHear">▶ écouter</button>' : ''}`;
    if ($('#exHear')) $('#exHear').addEventListener('click', () => q.play());
    if (q.playAfter && q.play) q.play();
    $('.exscore span:last-child').textContent = `${S.ok} / ${S.total} · ${Math.round((100 * S.ok) / S.total)} %`;
  }
  $('#exList').addEventListener('click', (e) => { const it = e.target.closest('.exitem'); if (it) open(it.dataset.e); });
  renderList();
  App.onTab('ex', () => { if ($('#exView').classList.contains('hidden')) renderList(); });
  return { open };
})();
