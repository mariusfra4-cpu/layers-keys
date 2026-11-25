// Onglet Outils : accordeur, métronome, réglages.
'use strict';
const Tools = (() => {
  const { $, $$ } = App;

  // =================== Accordeur ===================
  const T = { on: false, stream: null, an: null, buf: null, timer: 0, hist: [], target: -1 };
  function renderStrings() {
    const tu = G.getTuning();
    $('#tStrings').innerHTML = tu.map((m, i) => `<button data-i="${i}" class="${i === T.target ? 'on' : ''}">${M.pcName(m)}<sub>${Math.floor(m / 12) - 1}</sub></button>`).join('');
  }
  async function startTuner() {
    const ctx = Synth.ac();
    T.stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    const src = ctx.createMediaStreamSource(T.stream);
    T.an = ctx.createAnalyser();
    T.an.fftSize = 4096;
    src.connect(T.an);
    T.buf = new Float32Array(T.an.fftSize);
    T.on = true; T.hist = [];
    T.timer = setInterval(tunerTick, 50);
    App.keepScreenOn(true);
  }
  function stopTuner() {
    T.on = false; clearInterval(T.timer);
    if (T.stream) T.stream.getTracks().forEach((t) => t.stop());
    T.stream = null;
    App.keepScreenOn(false);
  }
  function tunerTick() {
    T.an.getFloatTimeDomainData(T.buf);
    const sr = Synth.ac().sampleRate;
    // décimation ×2 pour accélérer YIN
    const n = T.buf.length >> 1, x = new Float32Array(n);
    for (let i = 0; i < n; i++) x[i] = 0.5 * (T.buf[2 * i] + T.buf[2 * i + 1]);
    const r = DSP.yin(x, sr / 2, 0.15, 60, 1200);
    if (!r.freq || r.clarity < 0.8) return;
    T.hist.push(r.freq);
    if (T.hist.length > 5) T.hist.shift();
    const f = [...T.hist].sort((a, b) => a - b)[T.hist.length >> 1];
    const midiF = M.freqToMidi(f);
    let target = Math.round(midiF);
    if (T.target >= 0) target = G.getTuning()[T.target];
    const cents = Math.max(-50, Math.min(50, (midiF - target) * 100));
    const ok = Math.abs(cents) < 4;
    $('#tNote').textContent = M.pcName(target) + (Math.floor(target / 12) - 1);
    $('#tNote').classList.toggle('ok', ok);
    $('#tNeedle').style.left = 50 + cents + '%';
    $('#tCents').textContent = ok ? '✓ juste' : (cents > 0 ? '+' : '') + Math.round(cents) + ' cents ' + (cents > 0 ? '(trop haut)' : '(trop bas)');
    $('#tFreq').textContent = f.toFixed(1) + ' Hz';
  }
  $('#tunerBtn').addEventListener('click', async () => {
    if (T.on) { stopTuner(); $('#tunerBtn').textContent = '▶ Démarrer'; return; }
    try { await startTuner(); $('#tunerBtn').textContent = '■ Arrêter'; } catch (e) { App.toast('Micro indisponible : ' + (e.message || e), 4000); }
  });
  $('#tStrings').addEventListener('click', (e) => {
    const b = e.target.closest('button');
    if (!b) return;
    const i = +b.dataset.i;
    T.target = T.target === i ? -1 : i;
    renderStrings();
    Synth.note(G.getTuning()[i], 0, 0.8, 2.5);
  });

  // =================== Métronome ===================
  const Mt = { on: false, bpm: 90, beats: 4, next: 0, n: 0, timer: 0, taps: [] };
  function setBpm(v) { Mt.bpm = Math.max(40, Math.min(240, Math.round(v))); $('#mBpm').textContent = Mt.bpm; $('#mRange').value = Mt.bpm; }
  function dots() { $('#mDots').innerHTML = Array.from({ length: Mt.beats }, (_, i) => `<i data-i="${i}"></i>`).join(''); }
  function sched() {
    const now = Synth.now();
    while (Mt.next < now + 0.12) {
      const b = Mt.n % Mt.beats;
      Synth.click(Mt.next - now, b === 0);
      const delay = Math.max(0, (Mt.next - now) * 1000);
      setTimeout(() => $$('#mDots i').forEach((d) => { const on = +d.dataset.i === b; d.classList.toggle('on', on); d.classList.toggle('acc', on && b === 0); }), delay);
      Mt.next += (60 / Mt.bpm) * (Mt.beats === 6 ? 0.5 : 1);
      Mt.n++;
    }
    Mt.timer = setTimeout(sched, 25);
  }
  $('#mStart').addEventListener('click', () => {
    if (Mt.on) { Mt.on = false; clearTimeout(Mt.timer); $('#mStart').textContent = '▶ Démarrer'; App.keepScreenOn(false); return; }
    Mt.on = true; Mt.n = 0; Mt.next = Synth.now() + 0.05; sched();
    $('#mStart').textContent = '■ Arrêter';
    App.keepScreenOn(true);
  });
  $('#mRange').addEventListener('input', (e) => setBpm(+e.target.value));
  $('#mMinus').addEventListener('click', () => setBpm(Mt.bpm - 1));
  $('#mPlus').addEventListener('click', () => setBpm(Mt.bpm + 1));
  $('#mBeats').addEventListener('change', (e) => { Mt.beats = +e.target.value; dots(); });
  $('#mTap').addEventListener('click', () => {
    const t = performance.now();
    Mt.taps = Mt.taps.filter((x) => t - x < 2500);
    Mt.taps.push(t);
    if (Mt.taps.length >= 2) {
      const d = (Mt.taps[Mt.taps.length - 1] - Mt.taps[0]) / (Mt.taps.length - 1);
      setBpm(60000 / d);
    }
  });

  // =================== Réglages ===================
  const st = App.settings;
  $('#setTuning').innerHTML = Object.entries(G.TUNINGS).map(([k, v]) => `<option value="${k}">${v.name}</option>`).join('');
  $('#setNotation').value = st.notation;
  $('#setTuning').value = st.tuning;
  $('#setLabels').value = st.labels;
  $('#setNotation').addEventListener('change', (e) => { st.notation = e.target.value; App.applySettings(); });
  $('#setTuning').addEventListener('change', (e) => { st.tuning = e.target.value; App.applySettings(); });
  $('#setLabels').addEventListener('change', (e) => { st.labels = e.target.value; App.applySettings(); });
  App.onSettings(renderStrings);

  renderStrings(); dots(); setBpm(90);
  App.onTab('tools', () => {});
  return { stopTuner };
})();
