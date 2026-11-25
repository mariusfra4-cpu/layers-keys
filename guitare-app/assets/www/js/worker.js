// Analyse d'un fichier complet en arrière-plan (ne bloque pas l'interface).
'use strict';
importScripts('music.js', 'dsp.js');
self.onmessage = (e) => {
  const { x, sr, level } = e.data;
  try {
    const t0 = Date.now();
    const result = DSP.analyzeSignal(x, sr, { level, onProgress: (p) => self.postMessage({ progress: p }) });
    result.ms = Date.now() - t0;
    self.postMessage({ result });
  } catch (err) {
    self.postMessage({ error: String(err && err.message || err) });
  }
};
