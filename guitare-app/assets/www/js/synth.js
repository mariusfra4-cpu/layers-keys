// Son de guitare synthétique (Karplus-Strong) pour écouter accords, intervalles, gammes et progressions.
'use strict';
const Synth = (() => {
  let ctx = null, master = null, comp = null;
  const cache = new Map();
  function ac() {
    if (!ctx) {
      ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
      comp = ctx.createDynamicsCompressor();
      comp.threshold.value = -14; comp.ratio.value = 4;
      master = ctx.createGain(); master.gain.value = 0.8;
      master.connect(comp).connect(ctx.destination);
    }
    if (ctx.state === 'suspended') ctx.resume();
    return ctx;
  }
  function pluckBuffer(midi, dur = 3) {
    const key = midi + ':' + dur;
    if (cache.has(key)) return cache.get(key);
    const c = ac();
    const sr = c.sampleRate;
    const f = 440 * Math.pow(2, (midi - 69) / 12);
    const n = Math.floor(sr * dur);
    const buf = c.createBuffer(1, n, sr);
    const d = buf.getChannelData(0);
    const period = sr / f;
    const N = Math.floor(period);
    const frac = period - N;
    const line = new Float32Array(N + 2);
    // excitation : bruit filtré (attaque de médiator douce)
    let lp = 0;
    for (let i = 0; i < line.length; i++) { lp = lp * 0.5 + (Math.random() * 2 - 1) * 0.5; line[i] = lp; }
    const decay = 0.996 - Math.max(0, (midi - 40)) * 0.00004;
    let idx = 0, prev = 0;
    for (let i = 0; i < n; i++) {
      const a = line[idx], b = line[(idx + 1) % N];
      const v = a + (b - a) * frac;
      const out = decay * 0.5 * (v + prev);
      prev = v;
      line[idx] = out;
      d[i] = out;
      idx = (idx + 1) % N;
    }
    // corps : légère montée + fondu de fin
    const fade = Math.floor(sr * 0.05);
    for (let i = 0; i < fade; i++) d[n - 1 - i] *= i / fade;
    cache.set(key, buf);
    return buf;
  }
  function note(midi, when = 0, vel = 0.7, dur = 2.5) {
    const c = ac();
    const src = c.createBufferSource();
    src.buffer = pluckBuffer(Math.round(midi));
    const g = c.createGain();
    const t = c.currentTime + Math.max(0, when);
    g.gain.setValueAtTime(vel, t);
    g.gain.setTargetAtTime(0, t + dur, 0.08);
    const tone = c.createBiquadFilter();
    tone.type = 'lowpass'; tone.frequency.value = 3500;
    src.connect(tone).connect(g).connect(master);
    src.start(t);
    src.stop(t + dur + 0.6);
    return src;
  }
  // Grattage : notes MIDI de grave à aigu
  function strum(midis, when = 0, opts = {}) {
    const gap = opts.gap === undefined ? 0.022 : opts.gap;
    const list = opts.up ? midis.slice().reverse() : midis;
    list.forEach((m, i) => note(m, when + i * gap, (opts.vel || 0.55) * (i === 0 ? 1.1 : 1), opts.dur || 2.2));
  }
  function arpeggio(midis, when = 0, step = 0.28, dur) {
    midis.forEach((m, i) => note(m, when + i * step, 0.65, dur || 1.4));
  }
  // Clic de métronome
  function click(when = 0, accent = false) {
    const c = ac();
    const t = c.currentTime + Math.max(0, when);
    const o = c.createOscillator(), g = c.createGain();
    o.frequency.value = accent ? 1600 : 1000;
    g.gain.setValueAtTime(0.0001, t);
    g.gain.exponentialRampToValueAtTime(accent ? 0.6 : 0.35, t + 0.002);
    g.gain.exponentialRampToValueAtTime(0.0001, t + 0.05);
    o.connect(g).connect(master);
    o.start(t); o.stop(t + 0.06);
  }
  function now() { return ac().currentTime; }
  return { ac, note, strum, arpeggio, click, now };
})();
