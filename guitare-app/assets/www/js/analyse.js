// Onglet Analyse : détection en temps réel (micro ou lecture) et analyse complète de fichiers vidéo/audio.
'use strict';
const Analyse = (() => {
  const { $, $$ } = App;
  const mod = M.mod;
  const keyOpts = () => '<option value="auto">Automatique</option>' + [0, 1].map((mi) => Array.from({ length: 12 }, (_, t) => {
    const mode = mi ? 'minor' : 'major';
    return `<option value="${t}:${mode}">${M.keyName({ tonic: t, mode })}</option>`;
  }).join('')).join('');

  // =================== Moteur temps réel ===================
  const Live = {
    running: false, source: 'mic', ctx: null, stream: null, src: null, analyser: null, timer: null,
    ema: new Float64Array(12), bassEma: new Float64Array(12), keyAcc: new Float64Array(12), frames: 0,
    current: null, cand: null, candN: 0, silentFor: 0, key: null, manualKey: null, devs: [], tuning: 0, history: [], onChord: null,
    mediaNodes: new WeakMap(),
  };
  const level = () => +$('#liveLevel').value;
  const speed = () => +$('#liveSpeed').value;
  const sens = () => +$('#liveSens').value;

  function setupAnalyser(ctx) {
    const a = ctx.createAnalyser();
    a.fftSize = ctx.sampleRate > 30000 ? 16384 : 8192;
    a.smoothingTimeConstant = 0;
    a.minDecibels = -120; a.maxDecibels = 0;
    Live.freq = new Float32Array(a.frequencyBinCount);
    Live.mag = new Float32Array(a.frequencyBinCount);
    Live.td = new Float32Array(a.fftSize);
    return a;
  }
  async function startMic() {
    const ctx = Synth.ac();
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) throw new Error('Micro indisponible sur cet appareil');
    const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    Live.ctx = ctx; Live.stream = stream;
    Live.src = ctx.createMediaStreamSource(stream);
    Live.analyser = setupAnalyser(ctx);
    Live.src.connect(Live.analyser);
    Live.source = 'mic';
    begin();
  }
  function startMedia(el) {
    const ctx = Synth.ac();
    let node = Live.mediaNodes.get(el);
    if (!node) { node = ctx.createMediaElementSource(el); node.connect(ctx.destination); Live.mediaNodes.set(el, node); }
    Live.ctx = ctx; Live.src = node;
    Live.analyser = setupAnalyser(ctx);
    node.connect(Live.analyser);
    Live.source = 'media';
    begin();
  }
  function begin() {
    Live.running = true;
    Live.ema.fill(0); Live.bassEma.fill(0); Live.keyAcc.fill(0); Live.frames = 0; Live.devs = []; Live.tuning = 0;
    Live.current = null; Live.cand = null; Live.candN = 0;
    if (!Live.manualKey) Live.key = null;
    clearInterval(Live.timer);
    Live.timer = setInterval(tick, 70);
    App.keepScreenOn(true);
  }
  function stop() {
    Live.running = false;
    clearInterval(Live.timer);
    if (Live.stream) Live.stream.getTracks().forEach((t) => t.stop());
    if (Live.src && Live.analyser) { try { Live.src.disconnect(Live.analyser); } catch (e) { /* déjà déconnecté */ } }
    Live.stream = null; Live.analyser = null;
    App.keepScreenOn(false);
  }
  function tick() {
    const a = Live.analyser;
    if (!a) return;
    a.getFloatTimeDomainData(Live.td);
    let rms = 0;
    for (let i = 0; i < Live.td.length; i++) rms += Live.td[i] * Live.td[i];
    rms = Math.sqrt(rms / Live.td.length);
    if (Live.source === 'mic') $('#micLevel').style.width = Math.min(100, Math.sqrt(rms) * 400) + '%';
    const thr = 0.03 * Math.pow(0.6, sens());
    if (rms < thr) {
      Live.silentFor += 70;
      for (let i = 0; i < 12; i++) Live.ema[i] *= 0.85;
      if (Live.silentFor > 1300 && Live.current) { Live.current = null; emit(null); }
      drawChroma(Live.ema, null);
      return;
    }
    Live.silentFor = 0;
    a.getFloatFrequencyData(Live.freq);
    for (let i = 0; i < Live.freq.length; i++) Live.mag[i] = Math.pow(10, Live.freq[i] / 20);
    const r = DSP.chromaFromSpectrum(Live.mag, Live.ctx.sampleRate / a.fftSize, Live.tuning);
    // accordage glissant
    for (const d of r.devs) Live.devs.push(d);
    if (Live.devs.length > 600) Live.devs.splice(0, Live.devs.length - 600);
    if (Live.frames % 30 === 29) Live.tuning = DSP.estimateTuning(Live.devs);
    // lissage
    const cn = DSP.normalize(r.chroma), bn = DSP.normalize(r.bass);
    const alpha = 0.12 + speed() * 0.06;
    for (let i = 0; i < 12; i++) {
      Live.ema[i] = Live.ema[i] * (1 - alpha) + cn[i] * alpha;
      Live.bassEma[i] = Live.bassEma[i] * (1 - alpha * 0.8) + bn[i] * alpha * 0.8;
      Live.keyAcc[i] = Live.keyAcc[i] * 0.997 + cn[i];
    }
    Live.frames++;
    // tonalité
    if (!Live.manualKey && Live.frames > 40 && Live.frames % 15 === 0) {
      const k = DSP.detectKey(Live.keyAcc);
      if (k.confidence > 0.02 || Live.key) Live.key = { tonic: k.tonic, mode: k.mode };
      showKey(Live.key);
    }
    let bassPc = -1, bmax = 0, bsum = 0;
    for (let i = 0; i < 12; i++) { bsum += Live.bassEma[i]; if (Live.bassEma[i] > bmax) { bmax = Live.bassEma[i]; bassPc = i; } }
    if (bmax < (bsum / 12) * 2.2) bassPc = -1;
    const res = DSP.bestChord(Live.ema, level(), bassPc, Live.key || Live.manualKey);
    const need = Math.max(1, Math.round((12 - speed()) / 3));
    const same = (x, y) => x && y && x.root === y.root && x.id === y.id;
    if (res.score > 0.5) {
      if (same(res, Live.current)) { Live.cand = null; Live.candN = 0; if (res.bass !== Live.current.bass) { Live.current.bass = res.bass; emit(Live.current, true); } }
      else if (same(res, Live.cand)) { if (++Live.candN >= need) { Live.current = res; Live.cand = null; emit(res); } }
      else { Live.cand = res; Live.candN = 1; if (need === 1) { Live.current = res; emit(res); } }
    }
    drawChroma(Live.ema, Live.current);
    if (Live.current) Live.current.alts = res.alts;
  }
  function emit(ch, minor) { if (Live.onChord) Live.onChord(ch, minor); }

  function drawChroma(ch, chord) {
    const cv = $('#chromaCanvas');
    if (!cv || !cv.offsetParent) return;
    const dpr = window.devicePixelRatio || 1;
    const W = cv.clientWidth * dpr, H = cv.clientHeight * dpr;
    if (cv.width !== W) { cv.width = W; cv.height = H; }
    const g = cv.getContext('2d');
    g.clearRect(0, 0, W, H);
    let mx = 1e-9;
    for (let i = 0; i < 12; i++) mx = Math.max(mx, ch[i]);
    const pcs = chord ? M.CHORD_BY_ID[chord.id].iv.map((i) => mod(chord.root + i)) : [];
    const bw = W / 12;
    g.font = `${11 * dpr}px system-ui`; g.textAlign = 'center';
    for (let i = 0; i < 12; i++) {
      const h = (ch[i] / mx) * (H - 18 * dpr);
      g.fillStyle = chord && i === chord.root ? '#f5a524' : pcs.includes(i) ? '#2dd4bf' : '#3a404d';
      g.fillRect(i * bw + 3 * dpr, H - 16 * dpr - h, bw - 6 * dpr, h);
      g.fillStyle = '#8b93a3';
      g.fillText(M.pcName(i), i * bw + bw / 2, H - 3 * dpr);
    }
  }
  function showKey(key) {
    const b = $('#keyBadge');
    if (!key) { b.classList.add('hidden'); return; }
    b.textContent = '🔑 ' + M.keyName(key);
    b.classList.remove('hidden');
  }

  // =================== Interface micro ===================
  function renderLiveChord(ch) {
    const big = $('#liveChord');
    if (!ch) { big.classList.add('dim'); $('#liveAlts').innerHTML = ''; return; }
    const key = Live.manualKey || Live.key;
    M.setPreferFlats(key ? M.keyPrefersFlats(key) : false);
    big.classList.remove('dim');
    big.textContent = M.chordSymbol(ch.root, ch.id, ch.bass);
    $('#liveRoman').innerHTML = key ? `<b>${M.romanOf(ch.root, ch.id, key)}</b>` : '';
    $('#liveNotes').textContent = ' ' + M.chordNotes(ch.root, ch.id).join(' · ');
    $('#liveAlts').innerHTML = (ch.alts || []).slice(0, 3).map((a) => `<span>${M.chordSymbol(a.root, a.id)}</span>`).join('');
    const vs = G.voicings(ch.root, ch.id, ch.bass).slice(0, 3);
    $('#liveDiag').innerHTML = vs.map((v) => `<div data-noplay>${G.diagram(ch.root, ch.id, v.frets, { labels: App.settings.labels })}</div>`).join('');
    M.setPreferFlats(null);
  }
  function addHistory(ch) {
    const key = Live.manualKey || Live.key;
    M.setPreferFlats(key ? M.keyPrefersFlats(key) : false);
    const last = Live.history[Live.history.length - 1];
    const sym = M.chordSymbol(ch.root, ch.id, ch.bass);
    if (!last || last.sym !== sym) {
      Live.history.push({ sym, roman: key ? M.romanOf(ch.root, ch.id, key) : '', root: ch.root, id: ch.id });
      if (Live.history.length > 48) Live.history.shift();
    }
    M.setPreferFlats(null);
    const h = $('#liveHist');
    h.innerHTML = Live.history.map((x, i) => `<div class="hc ${i === Live.history.length - 1 ? 'last' : ''}"><b>${x.sym}</b><i>${x.roman}</i></div>`).join('');
  }
  function initMic() {
    $('#liveKey').innerHTML = keyOpts();
    $('#liveKey').addEventListener('change', (e) => {
      const v = e.target.value;
      if (v === 'auto') { Live.manualKey = null; Live.key = null; showKey(null); }
      else { const [t, m] = v.split(':'); Live.manualKey = { tonic: +t, mode: m }; showKey(Live.manualKey); }
    });
    $('#micBtn').addEventListener('click', async () => {
      if (Live.running && Live.source === 'mic') { stop(); micUi(false); return; }
      if (Live.running) stop();
      try {
        Live.onChord = (ch, minor) => { renderLiveChord(ch); if (ch && !minor) addHistory(ch); };
        await startMic();
        micUi(true);
      } catch (e) {
        App.toast('Impossible d’utiliser le micro : ' + (e.message || e) + '. Autorise le micro dans les paramètres Android.', 4500);
      }
    });
    $('#histClear').addEventListener('click', () => { Live.history = []; $('#liveHist').innerHTML = '<p class="muted">Les accords détectés apparaîtront ici.</p>'; });
    App.segmented($('#anaMode'), (v) => {
      $('#ana-mic').classList.toggle('hidden', v !== 'mic');
      $('#ana-file').classList.toggle('hidden', v !== 'file');
      if (v !== 'mic' && Live.running && Live.source === 'mic') { stop(); micUi(false); }
    });
    drawChroma(new Float64Array(12), null);
  }
  function micUi(on) {
    const b = $('#micBtn');
    b.textContent = on ? '■ Arrêter' : '▶ Écouter';
    b.classList.toggle('primary', !on); b.classList.toggle('danger', on);
    if (!on) { $('#micLevel').style.width = '0'; $('#liveChord').classList.add('dim'); }
  }

  // =================== Analyse de fichier ===================
  const F = { scoreMode: 'melody', score: null, next: null, signal: null, sr: 11025, result: null, transpose: 0, beatsChords: [], bars: [], raf: 0, worker: null, liveMode: false, liveSegs: [] };
  const media = () => $('#media');

  function progress(p, text) {
    $('#progressWrap').classList.toggle('hidden', p === null);
    if (p !== null) { $('#progressBar').style.width = Math.round(p * 100) + '%'; $('#progressText').textContent = text || ''; }
  }
  async function decodeInBrowser(file) {
    progress(0.02, 'Lecture du fichier…');
    const buf = await file.arrayBuffer();
    progress(0.08, 'Décodage audio…');
    const ctx = Synth.ac();
    const audio = await new Promise((res, rej) => ctx.decodeAudioData(buf, res, rej));
    progress(0.15, 'Préparation…');
    const len = Math.ceil(audio.duration * F.sr);
    const off = new OfflineAudioContext(1, len, F.sr);
    const s = off.createBufferSource();
    s.buffer = audio; s.connect(off.destination); s.start();
    const rendered = await off.startRendering();
    return rendered.getChannelData(0).slice();
  }
  async function decodeNative(id) {
    progress(0.02, 'Décodage de l’audio de la vidéo…');
    const poll = setInterval(() => { try { const p = +App.Android.decodeProgress(); if (p >= 0) progress(0.02 + p * 0.25, 'Décodage de l’audio… ' + Math.round(p * 100) + ' %'); } catch (e) { /* ignore */ } }, 250);
    try {
      const r = await fetch('/decode/' + id);
      if (!r.ok) throw new Error('décodage impossible (' + r.status + ')');
      const sr = +(r.headers.get('X-Sample-Rate') || F.sr);
      const ab = await r.arrayBuffer();
      if (ab.byteLength < 4000) throw new Error('piste audio vide');
      F.sr = sr;
      return new Float32Array(ab);
    } finally { clearInterval(poll); }
  }
  function runWorker(signal) {
    return new Promise((res, rej) => {
      if (F.worker) F.worker.terminate();
      let w;
      try { w = new Worker('js/worker.js'); } catch (e) { res(DSP.analyzeSignal(signal, F.sr, { level: +$('#fileLevel').value })); return; }
      F.worker = w;
      w.onmessage = (e) => {
        if (e.data.progress !== undefined) progress(0.3 + e.data.progress * 0.7, 'Reconnaissance des accords… ' + Math.round(e.data.progress * 100) + ' %');
        if (e.data.result) { w.terminate(); F.worker = null; res(e.data.result); }
        if (e.data.error) { w.terminate(); F.worker = null; rej(new Error(e.data.error)); }
      };
      w.onerror = (e) => { w.terminate(); F.worker = null; rej(new Error(e.message || 'erreur du calcul')); };
      w.postMessage({ x: signal.slice(), sr: F.sr, level: +$('#fileLevel').value });
    });
  }
  async function analyse() {
    const t0 = performance.now();
    const r = await runWorker(F.signal);
    F.result = r;
    progress(null);
    renderResult();
    App.toast(`Analyse terminée en ${((performance.now() - t0) / 1000).toFixed(1)} s`);
  }
  async function loadFile(src) {
    // src : { file } (navigateur) ou { id, name, mime } (Android)
    stopLiveMedia();
    F.result = null; F.signal = null; F.transpose = 0;
    $('#fileResult').classList.add('hidden');
    const name = src.file ? src.file.name : src.name;
    $('#fileName').textContent = name;
    const el = media();
    if (el.dataset.url) URL.revokeObjectURL(el.dataset.url);
    if (src.file) { const u = URL.createObjectURL(src.file); el.dataset.url = u; el.src = u; } else { el.dataset.url = ''; let u = '/media/' + src.id; try { u = App.Android.mediaUrl(src.id); } catch (e) { /* ancienne méthode */ } el.crossOrigin = 'anonymous'; el.src = u; }
    const isAudio = (src.file ? src.file.type : src.mime || '').startsWith('audio');
    el.style.maxHeight = isAudio ? '54px' : '';
    $('#mediaWrap').classList.remove('hidden');
    try {
      F.sr = 11025;
      F.signal = src.file ? await decodeInBrowser(src.file) : await decodeNative(src.id);
      progress(0.3, 'Reconnaissance des accords…');
      await analyse();
    } catch (e) {
      progress(null);
      App.toast('Analyse complète impossible (' + (e.message || e) + '). Passage en analyse pendant la lecture.', 5000);
      startLiveMedia();
    }
  }
  // Repli : détection en direct pendant la lecture
  function startLiveMedia() {
    F.liveMode = true; F.liveSegs = [];
    $('#fileResult').classList.remove('hidden');
    $('#fileInfos').innerHTML = '<span>Mode : <b>analyse pendant la lecture</b> — lance la vidéo</span>';
    $('#fileGrid').innerHTML = '';
    Live.onChord = (ch) => {
      const now = $('#fileNow');
      if (!ch) { now.textContent = '—'; return; }
      now.textContent = M.chordSymbol(mod(ch.root + F.transpose), ch.id, ch.bass === undefined ? undefined : mod(ch.bass + F.transpose));
      const vs = G.voicings(mod(ch.root + F.transpose), ch.id).slice(0, 3);
      $('#fileDiag').innerHTML = vs.map((v) => `<div data-noplay>${G.diagram(mod(ch.root + F.transpose), ch.id, v.frets, { labels: App.settings.labels })}</div>`).join('');
      F.liveSegs.push({ t: media().currentTime, sym: now.textContent });
      $('#fileGrid').innerHTML = F.liveSegs.slice(-32).map((s) => `<div class="bar"><div class="bc">${s.sym}</div><div class="br">${fmtTime(s.t)}</div></div>`).join('');
    };
    try { startMedia(media()); } catch (e) { App.toast('Lecture audio indisponible : ' + e.message); }
  }
  function stopLiveMedia() { if (F.liveMode) { stop(); F.liveMode = false; } }

  const fmtTime = (t) => Math.floor(t / 60) + ':' + String(Math.floor(t % 60)).padStart(2, '0');
  function trKey() { const k = F.result.key; return { tonic: mod(k.tonic + F.transpose), mode: k.mode }; }
  function symOf(sg) { return sg.root < 0 ? 'N.C.' : M.chordSymbol(mod(sg.root + F.transpose), sg.id, sg.bass === undefined ? undefined : mod(sg.bass + F.transpose)); }

  function renderResult() {
    const r = F.result;
    if (!r) return;
    const key = trKey();
    M.setPreferFlats(M.keyPrefersFlats(key));
    const distinct = new Set(r.segments.filter((s) => s.root >= 0).map((s) => s.root + s.id));
    $('#fileInfos').innerHTML = `<span>Tonalité <b>${M.keyName(key)}</b></span><span>Tempo <b>${Math.round(r.bpm)}</b> BPM</span>
      <span>Durée <b>${fmtTime(r.duration)}</b></span><span>Accordage <b>${r.tuning >= 0 ? '+' : ''}${Math.round(r.tuning * 100)}</b> cents</span><span><b>${distinct.size}</b> accords différents</span>`;
    $('#trLabel').textContent = 'Transposition ' + (F.transpose > 0 ? '+' : '') + F.transpose;
    // accords par temps, puis mesures de 4 temps
    const segAt = (t) => r.segments.find((s) => s.start <= t + 0.02 && s.end > t + 0.02);
    const beats = r.beats.length ? r.beats : [0];
    const per = 4;
    F.bars = [];
    for (let b = 0; b < beats.length; b += per) {
      const t0 = beats[b], t1 = b + per < beats.length ? beats[b + per] : r.duration;
      const syms = [], romans = [];
      for (let k = b; k < Math.min(b + per, beats.length); k++) {
        const sg = segAt(beats[k]);
        if (!sg) continue;
        const s = symOf(sg);
        if (syms[syms.length - 1] !== s) { syms.push(s); romans.push(sg.root >= 0 ? M.romanOf(mod(sg.root + F.transpose), sg.id, key) : ''); }
      }
      F.bars.push({ t0, t1, syms, romans });
    }
    // fusionne les mesures vides de début (silence)
    $('#fileGrid').innerHTML = F.bars.map((b, i) => `<div class="bar" data-i="${i}"><div class="bn">${i + 1}</div><div class="bc">${b.syms.map((s) => `<span>${s}</span>`).join('')}</div><div class="br">${b.romans.join(' ')}</div></div>`).join('');
    capoHint();
    F.score = Score.mount($('#scoreBox'), r, { transpose: F.transpose, mode: F.scoreMode, flats: M.keyPrefersFlats(key) }, (t) => { media().currentTime = t; updateNow(true); });
    M.setPreferFlats(null);
    $('#fileResult').classList.remove('hidden');
    updateNow(true);
  }
  const OPEN_MAJ = new Set([0, 9, 7, 4, 2]); // C A G E D
  const OPEN_MIN = new Set([9, 4, 2]); // Am Em Dm
  function capoHint() {
    const segs = F.result.segments.filter((s) => s.root >= 0);
    const uniq = [...new Map(segs.map((s) => [s.root + ':' + s.id, s])).values()];
    const easy = (c) => {
      let n = 0;
      for (const s of uniq) {
        const r = mod(s.root + F.transpose - c);
        const isMin = ['min', 'm7', 'm9', 'madd9'].includes(s.id), isMaj = ['maj', '7', 'maj7', 'add9', 'sus2', 'sus4', '6', '9', 'maj9'].includes(s.id);
        if ((isMaj && OPEN_MAJ.has(r)) || (isMin && OPEN_MIN.has(r))) n++;
      }
      return n;
    };
    let best = 0, bestN = easy(0);
    for (let c = 1; c <= 7; c++) { const n = easy(c); if (n > bestN) { bestN = n; best = c; } }
    $('#capoHint').textContent = best && uniq.length ? `💡 Plus facile avec un capodastre en case ${best} : joue les formes transposées de ${-best} demi-tons (${bestN}/${uniq.length} accords en accords ouverts).` : '';
  }
  let lastBar = -1;
  function updateNow(force) {
    const r = F.result;
    if (!r) return;
    const t = media().currentTime || 0;
    let i = 0;
    while (i < r.segments.length - 1 && r.segments[i].end <= t) i++;
    const sg = r.segments[i];
    const key = trKey();
    M.setPreferFlats(M.keyPrefersFlats(key));
    const sym = symOf(sg);
    const now = $('#fileNow');
    if (force || now.dataset.s !== sym) {
      now.dataset.s = sym; now.textContent = sym;
      $('#fileNowSub').innerHTML = sg.root >= 0 ? `<b>${M.romanOf(mod(sg.root + F.transpose), sg.id, key)}</b> · ${M.chordNotes(mod(sg.root + F.transpose), sg.id).join(' ')}` : '';
      const nx = r.segments.slice(i + 1).find((s) => s.root >= 0);
      F.next = nx ? { root: mod(nx.root + F.transpose), id: nx.id } : null;
      $('#passNext').textContent = nx ? '🎯 Accords de passage vers ' + symOf(nx) : '🎯 Accords de passage';
      $('#fileNext').textContent = nx ? symOf(nx) : '—';
      const vs = sg.root >= 0 ? G.voicings(mod(sg.root + F.transpose), sg.id, sg.bass === undefined ? undefined : mod(sg.bass + F.transpose)).slice(0, 3) : [];
      $('#fileDiag').innerHTML = vs.map((v) => `<div data-noplay>${G.diagram(mod(sg.root + F.transpose), sg.id, v.frets, { labels: App.settings.labels })}</div>`).join('');
    }
    M.setPreferFlats(null);
    if (F.score) F.score.update(t);
    const bi = F.bars.findIndex((b) => b.t0 <= t && b.t1 > t);
    if (bi !== lastBar || force) {
      $$('#fileGrid .bar.now').forEach((x) => x.classList.remove('now'));
      const el = $(`#fileGrid .bar[data-i="${bi}"]`);
      if (el) { el.classList.add('now'); if (!media().paused && el.offsetParent) el.scrollIntoView({ block: 'nearest', behavior: 'smooth' }); }
      lastBar = bi;
    }
  }
  function loop() {
    updateNow(false);
    if (!media().paused) F.raf = requestAnimationFrame(loop);
  }
  function exportText() {
    const r = F.result;
    if (!r) return '';
    const key = trKey();
    M.setPreferFlats(M.keyPrefersFlats(key));
    let s = `${$('#fileName').textContent}\nTonalité : ${M.keyName(key)} · Tempo : ${Math.round(r.bpm)} BPM\n\n`;
    F.bars.forEach((b, i) => { s += '| ' + (b.syms.join(' ') || '%') + ' '; if (i % 4 === 3) s += '|\n'; });
    M.setPreferFlats(null);
    return s + '|\n\n(analysé avec AccordLive)';
  }
  function initFile() {
    $('#pickBtn').addEventListener('click', () => {
      if (App.Android && App.Android.pickMedia) { App.Android.pickMedia(); return; }
      $('#fileInput').click();
    });
    $('#fileInput').addEventListener('change', (e) => { const f = e.target.files[0]; if (f) loadFile({ file: f }); e.target.value = ''; });
    window.onMediaPicked = (id, name, mime) => { App.show('analyse'); $('#anaMode button[data-v="file"]').click(); loadFile({ id, name, mime }); };
    const el = media();
    el.addEventListener('play', () => { cancelAnimationFrame(F.raf); F.raf = requestAnimationFrame(loop); App.keepScreenOn(true); });
    el.addEventListener('pause', () => App.keepScreenOn(false));
    el.addEventListener('seeked', () => updateNow(true));
    $('#fileGrid').addEventListener('click', (e) => {
      const b = e.target.closest('.bar');
      if (!b || !F.bars[+b.dataset.i]) return;
      el.currentTime = F.bars[+b.dataset.i].t0 + 0.01;
      updateNow(true);
    });
    $('#trUp').addEventListener('click', () => { F.transpose = Math.min(11, F.transpose + 1); renderResult(); });
    $('#trDown').addEventListener('click', () => { F.transpose = Math.max(-11, F.transpose - 1); renderResult(); });
    $('#fileLevel').addEventListener('change', async () => { if (F.signal) { progress(0.3, 'Nouvelle analyse…'); await analyse(); } });
    $('#fileCopy').addEventListener('click', () => App.share(exportText()));
    App.segmented($('#viewMode'), (v) => { $('#scorePane').classList.toggle('hidden', v !== 'score'); $('#fileGrid').classList.toggle('hidden', v !== 'grid'); updateNow(true); });
    App.segmented($('#scoreMode'), (v) => { F.scoreMode = v; renderResult(); });
    $('#passNext').addEventListener('click', () => { const n = F.next || (F.result && F.result.segments.find((s) => s.root >= 0)); if (n) PassingUI.openFor(mod(n.root + (F.next ? 0 : F.transpose)), n.id); });
  }

  initMic();
  initFile();
  App.onSettings(() => { if (F.result) renderResult(); });
  return { Live, startMic, stop, loadFile, F };
})();
