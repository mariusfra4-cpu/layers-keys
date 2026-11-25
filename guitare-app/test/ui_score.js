// Test de la partition + tablature synchronisées : node ui_score.js <www> <wav> <dossier captures>
const { chromium } = require('playwright');
const http = require('http'), fs = require('fs'), path = require('path');
const [www, wav, out] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css' };
const srv = http.createServer((q, r) => { let u = decodeURIComponent(q.url.split('?')[0]); if (u === '/') u = '/index.html'; fs.readFile(path.join(www, u), (e, d) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'Content-Type': types[path.extname(u)] || 'application/octet-stream' }); r.end(d); }); }).listen(8768);
(async () => {
  const b = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
  const p = await b.newPage({ viewport: { width: 393, height: 851 }, deviceScaleFactor: 2 });
  const errs = [];
  p.on('pageerror', (e) => errs.push('pageerror: ' + e.message));
  p.on('console', (m) => { if (m.type() === 'error') errs.push('console: ' + m.text()); });
  await p.goto('http://localhost:8768/'); await p.waitForTimeout(300);
  await p.click('#anaMode button[data-v=file]');
  await p.setInputFiles('#fileInput', wav);
  await p.waitForSelector('#fileResult:not(.hidden)', { timeout: 30000 });
  console.log('notes de mélodie :', await p.evaluate(() => Analyse.F.result.notes.length));
  // lecture : le curseur doit avancer et la partition défiler
  await p.evaluate(() => { const v = document.querySelector('#media'); v.muted = true; v.currentTime = 4; return v.play(); });
  await p.waitForTimeout(2500);
  const st = await p.evaluate(() => ({ t: document.querySelector('#media').currentTime, cur: +document.querySelector('#scoreCursor').getAttribute('x'), scroll: document.querySelector('.sscroll').scrollLeft, now: document.querySelector('#fileNow').textContent }));
  console.log('lecture :', JSON.stringify(st));
  await p.evaluate(() => document.querySelector('#media').pause());
  await p.evaluate(() => window.scrollTo(0, 330)); await p.waitForTimeout(200);
  await p.screenshot({ path: out + '/s1-melodie.png' });
  await p.click('#scoreMode button[data-v=chords]'); await p.waitForTimeout(300);
  await p.evaluate(() => { Analyse.F.score.update(document.querySelector('#media').currentTime); window.scrollTo(0, 330); }); await p.waitForTimeout(200);
  await p.screenshot({ path: out + '/s2-accords.png' });
  // touche la partition : la vidéo doit sauter
  const box = await p.$('.sinner'); const bb = await box.boundingBox();
  await p.mouse.click(bb.x + 120, bb.y + 60); await p.waitForTimeout(200);
  console.log('après toucher : t =', await p.evaluate(() => document.querySelector('#media').currentTime.toFixed(2)));
  await p.click('#passNext'); await p.waitForTimeout(300);
  console.log('passages ouverts :', await p.$$eval('.pline', (e) => e.length));
  console.log('erreurs :', errs.length ? errs.join('\n') : 'aucune');
  await b.close(); srv.close();
})();
