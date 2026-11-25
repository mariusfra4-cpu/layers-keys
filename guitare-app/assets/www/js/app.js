// Noyau de l'interface : navigation, réglages, utilitaires partagés.
'use strict';
const App = (() => {
  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
  const Android = window.AndroidBridge || null;

  const settings = Object.assign({ notation: 'en', tuning: 'standard', labels: 'deg' }, load('gx-settings', {}));
  function load(k, d) { try { const v = localStorage.getItem(k); return v ? JSON.parse(v) : d; } catch (e) { return d; } }
  function save(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* stockage indisponible */ } }
  function applySettings() {
    M.setNotation(settings.notation);
    G.setTuning(settings.tuning);
    save('gx-settings', settings);
    listeners.forEach((f) => f());
  }
  const listeners = [];
  const onSettings = (f) => listeners.push(f);

  // ---------- navigation ----------
  const tabHooks = {};
  function show(tab) {
    $$('#nav button').forEach((b) => b.classList.toggle('on', b.dataset.t === tab));
    $$('main .tab').forEach((s) => s.classList.toggle('active', s.id === 'tab-' + tab));
    window.scrollTo(0, 0);
    if (tabHooks[tab]) tabHooks[tab]();
  }
  function onTab(tab, f) { tabHooks[tab] = f; }
  function segmented(el, cb) {
    el.addEventListener('click', (e) => {
      const b = e.target.closest('button');
      if (!b) return;
      $$('button', el).forEach((x) => x.classList.toggle('on', x === b));
      cb(b.dataset.v);
    });
  }

  let toastTimer;
  function toast(msg, ms = 2200) {
    const t = $('#toast');
    t.textContent = msg; t.classList.add('show');
    clearTimeout(toastTimer); toastTimer = setTimeout(() => t.classList.remove('show'), ms);
  }

  // ---------- cartes d'accords ----------
  function chordCard(root, id, bass, opts = {}) {
    const vs = G.voicings(root, id, bass);
    const v = opts.voicing || vs[opts.vi || 0];
    const sym = M.chordSymbol(root, id, bass);
    const diag = v ? G.diagram(root, id, v.frets, { labels: settings.labels }) : '<div class="muted small" style="padding:30px 0">pas de doigté</div>';
    const fr = v ? v.frets.map((f) => (f < 0 ? 'x' : f)).join(' ') : '';
    return `<div class="dcard" data-root="${root}" data-id="${id}" data-bass="${bass === undefined ? '' : bass}" data-frets="${v ? v.frets.join(',') : ''}">
      ${opts.roman ? `<div class="rn">${opts.roman}</div>` : ''}<div class="dn">${sym}</div>${diag}${opts.showFrets ? `<div class="df">${fr}</div>` : ''}</div>`;
  }
  function playCard(card) {
    const fr = card.dataset.frets;
    if (fr) Synth.strum(G.voicingMidis(fr.split(',').map(Number)));
    else Synth.strum(G.chordMidis(+card.dataset.root, card.dataset.id));
  }
  document.addEventListener('click', (e) => {
    const c = e.target.closest('.dcard');
    if (c && !c.closest('[data-noplay]')) playCard(c);
  });

  // ---------- éléments interactifs des cours ----------
  function hydrate(root) {
    $$('.pl', root).forEach((b) => b.addEventListener('click', () => {
      const notes = b.dataset.n.split(/\s+/).map(M.midiFromName).filter((x) => x !== null);
      if (b.dataset.m === 'chord') Synth.strum(notes, 0, { gap: 0.03 });
      else Synth.arpeggio(notes, 0, 0.3, 1.0);
    }));
    $$('.xc', root).forEach((d) => {
      d.innerHTML = d.dataset.c.split(/\s+/).map((s) => {
        const p = M.parseChord(s);
        return p ? chordCard(p.root, p.id, p.bass) : '';
      }).join('');
    });
    $$('.xs', root).forEach((d) => {
      const r = M.pcFromName(d.dataset.r), sc = M.SCALE_BY_ID[d.dataset.s];
      const midis = sc.iv.concat([12]).map((i) => 48 + r + i + (r > 4 ? 0 : 12));
      d.innerHTML = `<div class="fbwrap">${G.fretboard({ frets: 15, mark: G.scaleMarker(r, sc.iv, d.dataset.l) })}</div>
        <button class="pl">▶ ${M.pcName(r)} ${sc.name}</button>`;
      d.querySelector('.pl').addEventListener('click', () => Synth.arpeggio(midis, 0, 0.25, 0.8));
    });
    $$('.xd', root).forEach((d) => {
      let tonic = M.pcFromName(d.dataset.k);
      const mode = d.dataset.m;
      const render = () => {
        M.setPreferFlats(M.keyPrefersFlats({ tonic, mode }));
        const ch = M.diatonicChords(tonic, mode, true);
        d.innerHTML = `<div class="row"><select>${Array.from({ length: 12 }, (_, i) => `<option value="${i}" ${i === tonic ? 'selected' : ''}>${M.keyName({ tonic: i, mode })}</option>`).join('')}</select></div>
          <div class="dia-tbl">${ch.map((c) => `<button data-r="${c.root}" data-id="${c.id}"><i>${c.roman}</i><b>${M.chordSymbol(c.root, c.id)}</b></button>`).join('')}</div>
          <div class="xc">${ch.map((c) => chordCard(c.root, c.id, undefined, { roman: c.roman })).join('')}</div>`;
        M.setPreferFlats(null);
        d.querySelector('select').addEventListener('change', (e) => { tonic = +e.target.value; render(); });
        $$('.dia-tbl button', d).forEach((b) => b.addEventListener('click', () => Synth.strum(G.chordMidis(+b.dataset.r, b.dataset.id))));
      };
      render();
    });
    $$('.ivtable', root).forEach((d) => {
      d.innerHTML = `<div class="tbl-wrap"><table class="t"><tr><th>½ t.</th><th>Nom</th><th>Ex. (C)</th><th>Repère</th><th></th></tr>${M.INTERVALS.map((iv) =>
        `<tr><td>${iv.st}</td><td><b>${iv.short}</b> ${iv.name}<br><span class="muted small">${iv.feel}</span></td><td>C–${M.pcName(iv.st)}</td><td class="small">${iv.song}</td><td><button class="pl" data-st="${iv.st}">▶</button></td></tr>`).join('')}</table></div>`;
      $$('button[data-st]', d).forEach((b) => b.addEventListener('click', () => { const s = +b.dataset.st; Synth.note(60, 0); Synth.note(60 + s, 0.55); Synth.note(60, 1.2); Synth.note(60 + s, 1.2); }));
    });
    $$('.modetable', root).forEach((d) => {
      d.innerHTML = `<div class="tbl-wrap"><table class="t"><tr><th>Mode</th><th>Formule</th><th>Accord</th><th>Couleur et usage</th></tr>${M.MODES.map((m) =>
        `<tr><td><b>${m.deg}. ${m.name}</b></td><td class="small">${m.iv.map((i) => ['1', '♭2', '2', '♭3', '3', '4', '♯4', '5', '♭6', '6', '♭7', '7'][i]).join(' ')}</td><td>${M.CHORD_BY_ID[m.chord].sym}</td><td class="small">${m.color} ${m.use}</td></tr>`).join('')}</table></div>`;
    });
    $$('.modeplay', root).forEach((d) => {
      const order = [3, 0, 4, 1, 5, 2, 6];
      d.innerHTML = order.map((i) => `<button class="pl" data-i="${i}">▶ C ${M.MODES[i].name.toLowerCase()}</button>`).join(' ');
      $$('button', d).forEach((b) => b.addEventListener('click', () => {
        const m = M.MODES[+b.dataset.i];
        Synth.note(36, 0, 0.5, 4);
        Synth.arpeggio(m.iv.concat([12]).map((x) => 60 + x), 0, 0.27, 0.9);
      }));
    });
    $$('.circle', root).forEach((d) => { d.innerHTML = circleOfFifths(); });
    $$('.xp', root).forEach((a) => a.addEventListener('click', () => { show('progs'); Library.openProg(a.dataset.p); }));
    $$('.lk', root).forEach((a) => a.addEventListener('click', () => Library.openLesson(a.dataset.l)));
  }
  function circleOfFifths() {
    const R = 150, r2 = 105, cx = 170, cy = 170;
    let s = `<svg viewBox="0 0 340 340"><circle cx="${cx}" cy="${cy}" r="${R + 15}" class="seg"/><circle cx="${cx}" cy="${cy}" r="${r2 - 18}" class="seg"/>`;
    for (let i = 0; i < 12; i++) {
      const pc = (i * 7) % 12;
      const a = (i / 12) * 2 * Math.PI - Math.PI / 2;
      const maj = M.pcName(pc, i >= 6), min = M.pcName((pc + 9) % 12, i >= 6) + 'm';
      s += `<text x="${cx + Math.cos(a) * R}" y="${cy + Math.sin(a) * R + 5}" text-anchor="middle">${maj}</text>`;
      s += `<text x="${cx + Math.cos(a) * r2}" y="${cy + Math.sin(a) * r2 + 4}" text-anchor="middle" class="mi">${min}</text>`;
    }
    return s + '</svg>';
  }

  // ---------- pont Android ----------
  function keepScreenOn(on) { try { if (Android) Android.keepScreenOn(!!on); } catch (e) { /* hors Android */ } }
  function share(text) {
    try { if (Android) { Android.shareText(text); return; } } catch (e) { /* hors Android */ }
    if (navigator.clipboard) navigator.clipboard.writeText(text).then(() => toast('Copié dans le presse-papiers'));
  }

  function init() {
    $('#nav').addEventListener('click', (e) => { const b = e.target.closest('button'); if (b) show(b.dataset.t); });
    M.setNotation(settings.notation);
    G.setTuning(settings.tuning);
    // bouton « retour » d'Android : ferme la vue ouverte, puis revient à l'onglet Analyse
    window.onBack = () => {
      for (const id of ['#lessonBack', '#progBack', '#exBack']) {
        const b = $(id);
        if (b && b.offsetParent) { b.click(); return true; }
      }
      if (!$('#tab-analyse').classList.contains('active')) { show('analyse'); return true; }
      return false;
    };
  }
  init();
  return { $, $$, settings, applySettings, onSettings, show, onTab, segmented, toast, chordCard, playCard, hydrate, Android, keepScreenOn, share, load, save };
})();
