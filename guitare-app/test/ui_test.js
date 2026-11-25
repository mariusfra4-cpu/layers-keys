// Test de l'interface dans Chromium (format téléphone) : node ui_test.js <dossier www> <wav> <dossier captures>
const { chromium } = require('playwright');
const http = require('http'), fs = require('fs'), path = require('path');
const [www, wav, out] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png' };
const srv = http.createServer((q, r) => { const f = path.join(www, decodeURIComponent(q.url.split('?')[0]) === '/' ? 'index.html' : decodeURIComponent(q.url.split('?')[0])); fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' }); r.end(d); }); }).listen(8765);
(async () => {
  const b = await chromium.launch({ args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required'] });
  const p = await b.newPage({ viewport: { width: 393, height: 851 }, deviceScaleFactor: 2 });
  const errs = [];
  p.on('pageerror', (e) => errs.push('pageerror: ' + e.message));
  p.on('console', (m) => { if (m.type() === 'error') errs.push('console: ' + m.text()); });
  await p.goto('http://localhost:8765/');
  await p.waitForTimeout(500);
  await p.click('#micBtn'); await p.waitForTimeout(2500);
  await p.screenshot({ path: out + '/1-micro.png' });
  console.log('micro :', await p.textContent('#liveChord'));
  await p.click('#micBtn');
  await p.click('#anaMode button[data-v=file]');
  await p.setInputFiles('#fileInput', wav);
  await p.waitForSelector('#fileResult:not(.hidden)', { timeout: 30000 });
  await p.waitForTimeout(500);
  console.log('fichier :', await p.textContent('#fileInfos'));
  console.log('grille :', (await p.$$eval('#fileGrid .bar .bc', (e) => e.map((x) => x.textContent))).join(' | '));
  await p.screenshot({ path: out + '/2-fichier.png', fullPage: true });
  for (const [t, f] of [['chords', '3-accords'], ['progs', '4-progs'], ['theory', '5-theorie'], ['ex', '6-exercices'], ['tools', '7-outils']]) {
    await p.click(`#nav button[data-t=${t}]`); await p.waitForTimeout(300);
    await p.screenshot({ path: `${out}/${f}.png` });
  }
  await p.click('#nav button[data-t=chords]'); await p.fill('#chordSearch', 'G13'); await p.waitForTimeout(200);
  await p.screenshot({ path: out + '/3b-g13.png', fullPage: true });
  await p.click('#chordMode button[data-v=find]');
  for (const [s, f] of [[1, 3], [2, 2], [3, 0], [4, 1], [5, 0]]) { await p.click(`#finderBoard .fhit[data-s="${s}"][data-f="${f}"]`); }
  console.log('identifier :', await p.textContent('#finderResult'));
  await p.click('#nav button[data-t=progs]'); await p.click('.pitem[data-p=gwalk]'); await p.waitForTimeout(300);
  await p.click('#progPlay'); await p.waitForTimeout(1500); await p.click('#progPlay');
  await p.screenshot({ path: out + '/4b-prog.png', fullPage: true });
  await p.click('#nav button[data-t=theory]');
  for (const l of await p.$$eval('.litem', (e) => e.map((x) => x.dataset.l))) {
    await p.evaluate((id) => Library.openLesson(id), l);
    await p.waitForTimeout(50);
  }
  await p.evaluate(() => Library.openLesson('gospel')); await p.waitForTimeout(200);
  await p.screenshot({ path: out + '/5b-lecon.png', fullPage: true });
  await p.click('#nav button[data-t=ex]');
  for (const id of await p.$$eval('.exitem', (e) => e.map((x) => x.dataset.e))) {
    await p.click(`.exitem[data-e="${id}"]`);
    for (let lv = 0; lv < 4; lv++) {
      const lb = await p.$(`.lvls button[data-l="${lv}"]`); if (!lb) break; await lb.click();
      for (let k = 0; k < 6; k++) { await p.click('.choices button'); await p.click('#exNext'); }
    }
    if (id === 'chord-name') await p.screenshot({ path: out + '/6b-exo.png' });
    await p.click('#exBack');
  }
  console.log('erreurs :', errs.length ? errs.join('\n') : 'aucune');
  await b.close(); srv.close();
})();
