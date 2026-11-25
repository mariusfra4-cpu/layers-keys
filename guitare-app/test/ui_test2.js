// Test des écrans « accords de passage », « voicings » et des leçons : node ui_test2.js <www> <dossier captures>
const { chromium } = require('playwright');
const http = require('http'), fs = require('fs'), path = require('path');
const [www, out] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css' };
const srv = http.createServer((q, r) => { let u = decodeURIComponent(q.url.split('?')[0]); if (u === '/') u = '/index.html'; fs.readFile(path.join(www, u), (e, d) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'Content-Type': types[path.extname(u)] || 'application/octet-stream' }); r.end(d); }); }).listen(8767);
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 393, height: 851 }, deviceScaleFactor: 2 });
  const errs = [];
  p.on('pageerror', (e) => errs.push('pageerror: ' + e.message));
  p.on('console', (m) => { if (m.type() === 'error') errs.push('console: ' + m.text()); });
  await p.goto('http://localhost:8767/'); await p.waitForTimeout(300);
  await p.click('#nav button[data-t=progs]');
  await p.click('#progMode button[data-v=pass]'); await p.waitForTimeout(200);
  console.log(await p.textContent('#passCount'));
  console.log('lignes :', await p.$$eval('.pline', (e) => e.length));
  await p.click('.pline [data-play="5"]'); await p.click('.pline [data-d="5"]'); await p.waitForTimeout(300);
  await p.screenshot({ path: out + '/p1-passages.png' });
  await p.click('#passStyles button[data-s="Gospel"]'); await p.selectOption('#passType', 'm7'); await p.selectOption('#passRoot', '9'); await p.waitForTimeout(200);
  console.log('gospel vers Am7 :', await p.$$eval('.pline', (e) => e.length));
  await p.screenshot({ path: out + '/p2-gospel.png' });
  await p.click('#progMode button[data-v=list]'); await p.click('#styleChips button[data-s="Praise & Worship"]'); await p.waitForTimeout(200);
  await p.screenshot({ path: out + '/p3-praise.png' });
  await p.click('#nav button[data-t=chords]');
  for (const f of ['triads', 'color', 'shell', 'drop2', 'drop3', 'rootless', 'quartal', 'ust']) {
    await p.fill('#chordSearch', f === 'ust' ? 'G13' : 'Dm9');
    await p.click(`#voicFam button[data-f="${f}"]`); await p.waitForTimeout(100);
    console.log(f, await p.$$eval('#chordDiags .dcard', (e) => e.length));
    if (f === 'drop2' || f === 'ust') { await p.evaluate(() => document.querySelector('#voicFam').scrollIntoView()); await p.screenshot({ path: `${out}/v-${f}.png` }); }
  }
  await p.click('#nav button[data-t=theory]');
  for (const id of ['tq-triades', 'tq-couleur', 'tq-jazz', 'tq-rootless', 'tq-passages', 'tq-gospel', 'tq-rnb', 'tq-chordmelody']) {
    await p.evaluate((i) => Library.openLesson(i), id); await p.waitForTimeout(80);
  }
  await p.evaluate(() => Library.openLesson('tq-rootless')); await p.waitForTimeout(200);
  await p.screenshot({ path: out + '/l-rootless.png', fullPage: true });
  console.log('erreurs :', errs.length ? errs.join('\n') : 'aucune');
  await b.close(); srv.close();
})();
