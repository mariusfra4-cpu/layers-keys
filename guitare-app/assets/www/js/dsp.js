// Traitement du signal : FFT, chromagramme, reconnaissance d'accords, tonalité, tempo, accordeur.
// Fonctions pures (utilisables aussi dans Node pour les tests).
'use strict';
const DSP = (() => {
  const Mu = typeof M !== 'undefined' ? M : require('./music.js');
  const mod = (n) => ((n % 12) + 12) % 12;

  // ---------- FFT radix-2 ----------
  class FFT {
    constructor(n) {
      this.n = n;
      this.rev = new Uint32Array(n);
      const bits = Math.log2(n);
      for (let i = 0; i < n; i++) {
        let r = 0;
        for (let b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b);
        this.rev[i] = r;
      }
      this.cos = new Float64Array(n / 2);
      this.sin = new Float64Array(n / 2);
      for (let i = 0; i < n / 2; i++) { this.cos[i] = Math.cos((2 * Math.PI * i) / n); this.sin[i] = -Math.sin((2 * Math.PI * i) / n); }
      this.re = new Float64Array(n);
      this.im = new Float64Array(n);
      this.win = new Float64Array(n);
      for (let i = 0; i < n; i++) this.win[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (n - 1));
    }
    // Spectre d'amplitude (n/2 bins) d'un signal fenêtré (Hann)
    magnitude(input, offset, out) {
      const n = this.n, re = this.re, im = this.im, rev = this.rev, w = this.win;
      for (let i = 0; i < n; i++) {
        const j = rev[i], k = offset + i;
        re[j] = (k >= 0 && k < input.length ? input[k] : 0) * w[i];
        im[j] = 0;
      }
      for (let size = 2; size <= n; size <<= 1) {
        const half = size >> 1, step = n / size;
        for (let i = 0; i < n; i += size) {
          for (let j = i, k = 0; j < i + half; j++, k += step) {
            const l = j + half;
            const tre = re[l] * this.cos[k] - im[l] * this.sin[k];
            const tim = re[l] * this.sin[k] + im[l] * this.cos[k];
            re[l] = re[j] - tre; im[l] = im[j] - tim;
            re[j] += tre; im[j] += tim;
          }
        }
      }
      out = out || new Float32Array(n / 2);
      for (let i = 0; i < n / 2; i++) out[i] = Math.sqrt(re[i] * re[i] + im[i] * im[i]);
      return out;
    }
  }

  // ---------- Chromagramme à partir d'un spectre ----------
  // mag : amplitudes linéaires ; binHz : résolution ; tuning : décalage en demi-tons (ex. -0.2)
  // Renvoie { chroma[12], bass[12], bassPc, energy, devs[] (écarts d'accordage des pics) }
  function chromaFromSpectrum(mag, binHz, tuning = 0, opts = {}) {
    const fMin = opts.fMin || 65, fMax = opts.fMax || 3500, bMin = opts.bMin || 40, bMax = opts.bMax || 260;
    const chroma = new Float64Array(12), bass = new Float64Array(12);
    const kMin = Math.max(2, Math.floor(bMin / binHz)), kMax = Math.min(mag.length - 2, Math.ceil(fMax / binHz));
    let maxMag = 1e-12, energy = 0;
    for (let k = kMin; k <= kMax; k++) { if (mag[k] > maxMag) maxMag = mag[k]; energy += mag[k] * mag[k]; }
    const thr = maxMag * (opts.peakThr || 0.02);
    const devs = [];
    let lowestBass = -1, lowestBassMag = 0, bassMax = 0;
    const bassPeaks = [];
    const peaks = [];
    for (let k = kMin; k <= kMax; k++) {
      const m = mag[k];
      if (m < thr || m < mag[k - 1] || m < mag[k + 1]) continue;
      // interpolation parabolique de la position du pic
      const a = mag[k - 1], b = m, c = mag[k + 1];
      const den = a - 2 * b + c;
      const p = den !== 0 ? (0.5 * (a - c)) / den : 0;
      const f = (k + p) * binHz;
      if (f > 0) peaks.push({ f, m, w: 1 });
    }
    // atténuation des harmoniques 3, 5, 6, 7 (qui ajoutent de fausses quintes, tierces, septièmes)
    const comp = opts.compress || 20;
    for (let j = 0; j < peaks.length; j++) {
      const pj = peaks[j];
      for (let i = 0; i < j; i++) {
        const pi = peaks[i];
        const r = pj.f / pi.f;
        const h = Math.round(r);
        if (h !== 3 && h !== 5 && h !== 6 && h !== 7) continue;
        if (Math.abs(1200 * Math.log2(r / h)) > 30) continue;
        if (pj.m < pi.m * (h === 3 ? 0.9 : 0.6)) pj.w *= h === 3 || h === 6 ? 0.45 : 0.3;
      }
    }
    for (const pk of peaks) {
      const { f, m } = pk;
      const midi = 69 + 12 * Math.log2(f / 440) - tuning;
      const near = Math.round(midi);
      const dev = midi - near;
      const w = pk.w * Math.log(1 + (comp * m) / maxMag) * Math.cos(Math.PI * dev) ** 2;
      if (f >= fMin && f <= fMax) {
        chroma[mod(near)] += w;
        if (m > maxMag * 0.08) devs.push(dev + tuning);
      }
      if (f >= bMin && f <= bMax) {
        bassPeaks.push({ f, m, pc: mod(near) });
        bass[mod(near)] += w;
        if (m > bassMax) bassMax = m;
      }
    }
    // basse = pic le plus grave parmi les pics forts de la zone grave
    for (const bp of bassPeaks) {
      if (bp.m >= bassMax * 0.35 && (lowestBass < 0 || bp.f < lowestBass)) { lowestBass = bp.f; lowestBassMag = bp.m; }
    }
    let bassPc = -1;
    if (lowestBass > 0) bassPc = mod(Math.round(69 + 12 * Math.log2(lowestBass / 440) - tuning));
    return { chroma, bass, bassPc, bassStrength: lowestBassMag / maxMag, energy: Math.sqrt(energy), devs };
  }

  function normalize(v) {
    let s = 0;
    for (let i = 0; i < v.length; i++) s += v[i] * v[i];
    s = Math.sqrt(s) || 1;
    const o = new Float64Array(v.length);
    for (let i = 0; i < v.length; i++) o[i] = v[i] / s;
    return o;
  }

  // ---------- Gabarits d'accords ----------
  function ivWeight(i, iv) {
    i = mod(i);
    if (i === 0) return 1.0;
    if (i === 3 || i === 4) return iv.includes(3) && iv.includes(4) ? (i === 4 ? 1.0 : 0.7) : 1.0;
    if (i === 7) return 0.75;
    if (i === 6 || i === 8) return iv.includes(7) ? 0.65 : 0.9;
    if (i === 10 || i === 11) return 0.85;
    if (i === 9) return iv.includes(10) || iv.includes(11) ? 0.65 : 0.85;
    if (i === 5) return iv.includes(3) || iv.includes(4) ? 0.6 : 0.9;
    return 0.65; // 9e, ♭9
  }
  // Niveau : 1 = accords simples, 2 = + septièmes, 3 = tout (jazz, gospel, R&B)
  const LEVEL_SETS = {
    1: ['maj', 'min'],
    2: ['maj', 'min', 'dim', 'aug', 'sus2', 'sus4', '7', 'maj7', 'm7', 'm7b5', 'dim7', '6', 'm6', '7sus4'],
    3: ['maj', 'min', 'dim', 'aug', 'sus2', 'sus4', '7', 'maj7', 'm7', 'm7b5', 'dim7', '6', 'm6', '7sus4', 'add9', 'madd9', '69',
      '9', 'maj9', 'm9', '9sus4', 'm11', '13', 'maj13', 'm13', '7b9', '7s9', '7s5', 'mmaj7', 'maj7s11'],
  };
  const PENALTY = { maj: 0, min: 0, dim: 0.04, aug: 0.05, sus2: 0.05, sus4: 0.04, '7': 0.012, maj7: 0.015, m7: 0.012, m7b5: 0.03, dim7: 0.035,
    '6': 0.03, m6: 0.035, '7sus4': 0.03, add9: 0.025, madd9: 0.03, '69': 0.04, '9': 0.03, maj9: 0.03, m9: 0.03, '9sus4': 0.035, m11: 0.04,
    '13': 0.045, maj13: 0.05, m13: 0.05, '7b9': 0.05, '7s9': 0.045, '7s5': 0.05, mmaj7: 0.05, maj7s11: 0.05 };

  const templateCache = {};
  function templates(level) {
    if (templateCache[level]) return templateCache[level];
    const list = [];
    for (const id of LEVEL_SETS[level]) {
      const c = Mu.CHORD_BY_ID[id];
      for (let r = 0; r < 12; r++) {
        const t = new Float64Array(12);
        for (const i of c.iv) t[mod(r + i)] = Math.max(t[mod(r + i)], ivWeight(i, c.iv));
        list.push({ root: r, id, t: normalize(t), pen: PENALTY[id] || 0.05, pcs: c.iv.map((i) => mod(r + i)) });
      }
    }
    templateCache[level] = list;
    return list;
  }

  // Scores de tous les accords pour un chroma donné
  function scoreChords(chroma, level, bassPc, key, opts = {}) {
    const cn = normalize(chroma);
    const T = templates(level);
    const out = new Float64Array(T.length);
    const keySet = key ? diatonicSet(key) : null;
    for (let s = 0; s < T.length; s++) {
      const t = T[s].t;
      let d = 0;
      for (let i = 0; i < 12; i++) d += cn[i] * t[i];
      d -= T[s].pen * (opts.complexity === undefined ? 1 : opts.complexity);
      if (bassPc >= 0) {
        if (bassPc === T[s].root) d += opts.bassBonus === undefined ? 0.05 : opts.bassBonus;
        else if (!T[s].pcs.includes(bassPc)) d -= 0.03;
      }
      if (keySet && keySet.has(T[s].root)) d += 0.015;
      out[s] = d;
    }
    return out;
  }
  function diatonicSet(key) {
    const iv = key.mode === 'major' ? [0, 2, 4, 5, 7, 9, 11] : [0, 2, 3, 5, 7, 8, 10, 11];
    return new Set(iv.map((i) => mod(key.tonic + i)));
  }
  function bestChord(chroma, level, bassPc, key, opts) {
    const sc = scoreChords(chroma, level, bassPc, key, opts);
    const T = templates(level);
    let best = 0, second = -1;
    for (let s = 1; s < sc.length; s++) if (sc[s] > sc[best]) best = s;
    for (let s = 0; s < sc.length; s++) if (s !== best && T[s].root !== T[best].root && (second < 0 || sc[s] > sc[second])) second = s;
    const res = { root: T[best].root, id: T[best].id, score: sc[best], margin: sc[best] - (second >= 0 ? sc[second] : 0) };
    if (bassPc >= 0 && bassPc !== res.root && T[best].pcs.includes(bassPc)) res.bass = bassPc;
    // alternatives (pour l'affichage)
    const idx = Array.from(sc.keys()).sort((a, b) => sc[b] - sc[a]).slice(0, 4);
    res.alts = idx.slice(1).map((s) => ({ root: T[s].root, id: T[s].id, score: sc[s] }));
    return res;
  }

  // ---------- Tonalité (Krumhansl-Schmuckler) ----------
  const KMAJ = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
  const KMIN = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
  function corr(a, b) {
    const n = a.length;
    let ma = 0, mb = 0;
    for (let i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    let num = 0, da = 0, db = 0;
    for (let i = 0; i < n; i++) { const x = a[i] - ma, y = b[i] - mb; num += x * y; da += x * x; db += y * y; }
    return num / (Math.sqrt(da * db) || 1);
  }
  function detectKey(chroma) {
    let best = null;
    const all = [];
    for (let t = 0; t < 12; t++) {
      const rot = Array.from({ length: 12 }, (_, i) => chroma[mod(i + t)]);
      const cM = corr(rot, KMAJ), cm = corr(rot, KMIN);
      all.push({ tonic: t, mode: 'major', score: cM }, { tonic: t, mode: 'minor', score: cm });
    }
    all.sort((a, b) => b.score - a.score);
    best = all[0];
    best.confidence = all[0].score - all[1].score;
    return best;
  }

  // ---------- Accordage global (écart moyen des pics par rapport au La 440) ----------
  function estimateTuning(devs) {
    if (devs.length < 20) return 0;
    // moyenne circulaire
    let s = 0, c = 0;
    for (const d of devs) { s += Math.sin(2 * Math.PI * d); c += Math.cos(2 * Math.PI * d); }
    return Math.atan2(s, c) / (2 * Math.PI);
  }

  // ---------- Tempo et temps (Ellis 2007, simplifié) ----------
  function onsetEnvelope(x, sr, hop = 256, n = 1024) {
    const fft = new FFT(n);
    const frames = Math.max(0, Math.floor((x.length - n) / hop) + 1);
    const env = new Float32Array(frames);
    let prev = new Float32Array(n / 2), cur = new Float32Array(n / 2);
    for (let f = 0; f < frames; f++) {
      fft.magnitude(x, f * hop, cur);
      let flux = 0;
      for (let k = 1; k < n / 2; k++) {
        const v = Math.log(1 + 1000 * cur[k]);
        const d = v - prev[k];
        if (d > 0) flux += d;
        prev[k] = v;
      }
      env[f] = flux;
    }
    // retire la moyenne locale (≈ 1 s) et normalise
    const w = Math.round(sr / hop);
    const out = new Float32Array(frames);
    let acc = 0;
    const q = [];
    for (let f = 0; f < frames; f++) {
      acc += env[f]; q.push(env[f]);
      if (q.length > w) acc -= q.shift();
      out[f] = Math.max(0, env[f] - acc / q.length);
    }
    let sd = 0;
    for (const v of out) sd += v * v;
    sd = Math.sqrt(sd / (frames || 1)) || 1;
    for (let f = 0; f < frames; f++) out[f] /= sd;
    return { env: out, fps: sr / hop };
  }
  function estimateTempo(env, fps) {
    const minLag = Math.round((fps * 60) / 200), maxLag = Math.round((fps * 60) / 55);
    let best = 0, bestLag = Math.round((fps * 60) / 100);
    const n = env.length;
    for (let lag = minLag; lag <= maxLag; lag++) {
      let s = 0;
      for (let i = lag; i < n; i++) s += env[i] * env[i - lag];
      s /= n - lag;
      const bpm = (60 * fps) / lag;
      const w = Math.exp(-0.5 * Math.pow(Math.log2(bpm / 110) / 0.9, 2)); // préférence autour de 110 BPM
      if (s * w > best) { best = s * w; bestLag = lag; }
    }
    return { bpm: (60 * fps) / bestLag, period: bestLag };
  }
  function trackBeats(env, period, tightness = 100) {
    const n = env.length;
    const score = new Float32Array(n), back = new Int32Array(n).fill(-1);
    for (let i = 0; i < n; i++) {
      let best = 0, bi = -1;
      const lo = Math.max(0, i - Math.round(2 * period)), hi = i - Math.round(period / 2);
      for (let j = lo; j <= hi; j++) {
        const r = Math.log((i - j) / period);
        const v = score[j] - tightness * r * r;
        if (bi < 0 || v > best) { best = v; bi = j; }
      }
      score[i] = env[i] + (bi >= 0 ? Math.max(0, best) : 0);
      back[i] = bi >= 0 && best > 0 ? bi : -1;
    }
    // point de départ : meilleur score parmi la dernière période
    let end = n - 1, bv = -1e9;
    for (let i = Math.max(0, n - Math.round(period)); i < n; i++) if (score[i] > bv) { bv = score[i]; end = i; }
    const beats = [];
    for (let i = end; i >= 0; i = back[i]) { beats.push(i); if (back[i] < 0) break; }
    beats.reverse();
    // complète vers le début si le suivi s'est arrêté
    while (beats.length && beats[0] - period > 0) beats.unshift(Math.round(beats[0] - period));
    return beats;
  }

  // ---------- Viterbi (lissage de la suite d'accords) ----------
  function viterbi(emissions, switchPenalty) {
    const T = emissions.length;
    if (!T) return [];
    const S = emissions[0].length;
    let prev = Float64Array.from(emissions[0]);
    const back = [];
    for (let t = 1; t < T; t++) {
      let bestPrev = 0;
      for (let s = 1; s < S; s++) if (prev[s] > prev[bestPrev]) bestPrev = s;
      const cur = new Float64Array(S), bk = new Int32Array(S);
      const e = emissions[t];
      for (let s = 0; s < S; s++) {
        const stay = prev[s], move = prev[bestPrev] - switchPenalty;
        if (stay >= move) { cur[s] = stay + e[s]; bk[s] = s; } else { cur[s] = move + e[s]; bk[s] = bestPrev; }
      }
      back.push(bk);
      prev = cur;
    }
    let s = 0;
    for (let i = 1; i < S; i++) if (prev[i] > prev[s]) s = i;
    const path = new Array(T);
    path[T - 1] = s;
    for (let t = T - 1; t > 0; t--) { s = back[t - 1][s]; path[t - 1] = s; }
    return path;
  }

  // ---------- Analyse complète d'un fichier (signal mono déjà rééchantillonné) ----------
  // onProgress(0..1) optionnel. Renvoie { segments, key, bpm, tuning, beats, duration }
  function analyzeSignal(x, sr, opts = {}) {
    const level = opts.level || 3;
    const onProgress = opts.onProgress || (() => {});
    const N = opts.n || 4096, hop = opts.hop || 512;
    const fft = new FFT(N);
    const binHz = sr / N;
    const frames = Math.max(1, Math.floor((x.length - N) / hop) + 1);
    const mag = new Float32Array(N / 2);
    // 1re passe : accordage
    const devs = [];
    const step = Math.max(1, Math.floor(frames / 200));
    for (let f = 0; f < frames; f += step) {
      fft.magnitude(x, f * hop, mag);
      const r = chromaFromSpectrum(mag, binHz, 0);
      for (const d of r.devs) devs.push(d);
    }
    const tuning = estimateTuning(devs);
    onProgress(0.1);
    // 2e passe : chroma par trame
    const chromas = new Array(frames), basses = new Int8Array(frames), energies = new Float32Array(frames);
    const melMidi = new Int16Array(frames), melSal = new Float32Array(frames);
    for (let f = 0; f < frames; f++) {
      fft.magnitude(x, f * hop, mag);
      const mm = frameMelody(mag, binHz, tuning);
      melMidi[f] = mm.midi; melSal[f] = mm.sal;
      const r = chromaFromSpectrum(mag, binHz, tuning);
      chromas[f] = r.chroma; basses[f] = r.bassStrength > 0.05 ? r.bassPc : -1; energies[f] = r.energy;
      if (f % 200 === 0) onProgress(0.1 + 0.6 * (f / frames));
    }
    // énergie : seuil de silence
    const sortedE = Float32Array.from(energies).sort();
    const eRef = sortedE[Math.floor(sortedE.length * 0.9)] || 1;
    // tonalité globale
    const total = new Float64Array(12);
    for (let f = 0; f < frames; f++) { const c = normalize(chromas[f]); const w = energies[f] / eRef; for (let i = 0; i < 12; i++) total[i] += c[i] * Math.min(1, w); }
    const key = detectKey(total);
    onProgress(0.75);
    // temps
    const oe = onsetEnvelope(x, sr);
    const tempo = estimateTempo(oe.env, oe.fps);
    let beatFrames = trackBeats(oe.env, tempo.period);
    let beatTimes = beatFrames.map((b) => b / oe.fps);
    const duration = x.length / sr;
    if (beatTimes.length < 4) { beatTimes = []; for (let t = 0; t < duration; t += 0.5) beatTimes.push(t); }
    if (beatTimes[0] > 0.05) beatTimes.unshift(0);
    beatTimes.push(duration);
    onProgress(0.85);
    // chroma synchronisé sur les temps
    const frameTime = (f) => (f * hop + N / 2) / sr;
    const units = [];
    let f = 0;
    for (let b = 0; b < beatTimes.length - 1; b++) {
      const t0 = beatTimes[b], t1 = beatTimes[b + 1];
      const c = new Float64Array(12);
      const bassCount = new Float64Array(12);
      let e = 0, cnt = 0;
      while (f < frames && frameTime(f) < t0) f++;
      let g = f;
      while (g < frames && frameTime(g) < t1) {
        const cn = chromas[g];
        for (let i = 0; i < 12; i++) c[i] += cn[i];
        if (basses[g] >= 0) bassCount[basses[g]] += 1;
        e += energies[g]; cnt++; g++;
      }
      let bp = -1, bc = 0;
      for (let i = 0; i < 12; i++) if (bassCount[i] > bc) { bc = bassCount[i]; bp = i; }
      if (cnt && bc < cnt * 0.4) bp = -1;
      units.push({ t0, t1, chroma: c, bass: bp, energy: cnt ? e / cnt : 0 });
    }
    // émissions
    const T = templates(level);
    const S = T.length + 1; // + état « pas d'accord »
    const em = units.map((u) => {
      const sc = scoreChords(u.chroma, level, u.bass, key, opts);
      const out = new Float64Array(S);
      out.set(sc);
      out[S - 1] = u.energy < eRef * 0.08 ? 1.2 : 0.45;
      return out;
    });
    const path = viterbi(em, opts.switchPenalty || 0.12);
    onProgress(0.95);
    // segments
    const segments = [];
    for (let i = 0; i < units.length; i++) {
      const s = path[i];
      const u = units[i];
      const isN = s === S - 1;
      const last = segments[segments.length - 1];
      if (last && last.state === s) { last.end = u.t1; last.bassVotes.push(u.bass); continue; }
      segments.push({ state: s, start: u.t0, end: u.t1, root: isN ? -1 : T[s].root, id: isN ? null : T[s].id, bassVotes: [u.bass], conf: isN ? 0 : em[i][s] });
    }
    for (const sg of segments) {
      if (sg.root >= 0) {
        const cnt = {};
        for (const b of sg.bassVotes) if (b >= 0) cnt[b] = (cnt[b] || 0) + 1;
        let bb = -1, bc = 0;
        for (const k in cnt) if (cnt[k] > bc) { bc = cnt[k]; bb = +k; }
        const pcs = Mu.CHORD_BY_ID[sg.id].iv.map((i) => mod(sg.root + i));
        if (bb >= 0 && bb !== sg.root && pcs.includes(bb) && bc >= sg.bassVotes.length * 0.6) sg.bass = bb;
      }
      delete sg.bassVotes; delete sg.state;
    }
    const notes = melodyNotes(melMidi, melSal, energies, eRef, hop / sr, N / 2 / sr);
    onProgress(1);
    return { segments, key, bpm: tempo.bpm, tuning, beats: beatTimes.slice(0, -1), duration, notes };
  }


  // ---------- Mélodie principale (saillance harmonique) ----------
  // Pour chaque note candidate, somme pondérée des amplitudes (log) de ses 5 premières harmoniques.
  function frameMelody(mag, binHz, tuning = 0, lo = 52, hi = 88) {
    let maxMag = 1e-12;
    const kMax = Math.min(mag.length - 2, Math.ceil(5000 / binHz));
    for (let k = 2; k < kMax; k++) if (mag[k] > maxMag) maxMag = mag[k];
    const L = (f) => {
      const b = f / binHz;
      const k = Math.round(b);
      if (k < 1 || k >= mag.length - 1) return 0;
      const m = Math.max(mag[k - 1], mag[k], mag[k + 1]);
      return Math.log(1 + (30 * m) / maxMag);
    };
    let best = -1, bestS = 0;
    const sal = new Float32Array(hi + 1);
    for (let n = lo - 12; n <= hi; n++) {
      const f0 = 440 * Math.pow(2, (n + tuning - 69) / 12);
      let s = 0, w = 1;
      for (let h = 1; h <= 5; h++) { s += w * L(f0 * h); w *= 0.75; }
      sal[n] = s;
    }
    for (let n = lo; n <= hi; n++) {
      // un pic doit dépasser ses voisins d'un demi-ton
      if (sal[n] < sal[n - 1] || sal[n] < sal[n + 1]) continue;
      if (sal[n] > bestS) { bestS = sal[n]; best = n; }
    }
    // erreur d'octave : la note une octave plus bas explique presque aussi bien le spectre
    if (best - 12 >= lo && sal[best - 12] > 0.97 * bestS) best -= 12;
    return { midi: best, sal: bestS };
  }
  // Découpe en notes : lissage médian, seuil de voisement, durée minimale
  function melodyNotes(midi, sal, energy, eRef, dt, offset) {
    const n = midi.length;
    const sorted = Float32Array.from(sal).sort();
    const sRef = sorted[Math.floor(n * 0.9)] || 1;
    const v = new Int16Array(n);
    for (let i = 0; i < n; i++) v[i] = sal[i] > sRef * 0.45 && energy[i] > eRef * 0.06 ? midi[i] : -1;
    const med = new Int16Array(n);
    for (let i = 0; i < n; i++) {
      const w = [];
      for (let k = i - 2; k <= i + 2; k++) w.push(v[Math.max(0, Math.min(n - 1, k))]);
      w.sort((a, b) => a - b);
      med[i] = w[2];
    }
    const notes = [];
    let i = 0;
    while (i < n) {
      if (med[i] < 0) { i++; continue; }
      let j = i;
      while (j + 1 < n && (med[j + 1] === med[i] || (med[j + 1] < 0 && j + 2 < n && med[j + 2] === med[i]))) j++;
      if (j - i + 1 >= 3) notes.push({ t: i * dt + offset - dt, d: (j - i + 1) * dt, midi: med[i] });
      i = j + 1;
    }
    return notes;
  }

  // ---------- Hauteur (accordeur) : YIN ----------
  function yin(buf, sr, threshold = 0.12, fMin = 55, fMax = 1400) {
    const n = buf.length;
    const tauMax = Math.min(Math.floor(sr / fMin), Math.floor(n / 2));
    const tauMin = Math.max(2, Math.floor(sr / fMax));
    let rms = 0;
    for (let i = 0; i < n; i++) rms += buf[i] * buf[i];
    rms = Math.sqrt(rms / n);
    if (rms < 0.004) return { freq: 0, rms, clarity: 0 };
    const d = new Float32Array(tauMax + 1);
    const W = n - tauMax;
    for (let tau = 1; tau <= tauMax; tau++) {
      let s = 0;
      for (let i = 0; i < W; i++) { const v = buf[i] - buf[i + tau]; s += v * v; }
      d[tau] = s;
    }
    // différence normalisée cumulée
    const cmnd = new Float32Array(tauMax + 1);
    cmnd[0] = 1;
    let run = 0;
    for (let tau = 1; tau <= tauMax; tau++) { run += d[tau]; cmnd[tau] = (d[tau] * tau) / (run || 1); }
    let tau = -1;
    for (let t = tauMin; t <= tauMax; t++) {
      if (cmnd[t] < threshold) {
        while (t + 1 <= tauMax && cmnd[t + 1] < cmnd[t]) t++;
        tau = t; break;
      }
    }
    if (tau < 0) return { freq: 0, rms, clarity: 0 };
    const x0 = tau > 1 ? cmnd[tau - 1] : cmnd[tau], x2 = tau + 1 <= tauMax ? cmnd[tau + 1] : cmnd[tau];
    const den = x0 + x2 - 2 * cmnd[tau];
    const better = den !== 0 ? tau + (x0 - x2) / (2 * den) : tau;
    return { freq: sr / better, rms, clarity: 1 - cmnd[tau] };
  }

  return { FFT, chromaFromSpectrum, normalize, templates, scoreChords, bestChord, detectKey, estimateTuning, onsetEnvelope, estimateTempo, trackBeats, viterbi, analyzeSignal, yin, frameMelody, LEVEL_SETS };
})();
if (typeof module !== 'undefined') module.exports = DSP;
