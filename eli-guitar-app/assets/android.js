// Pont entre la page Eli Guitar Chords et Android (injecté par l'application au chargement).
// - liens de téléchargement (blob:/data:) : envoyés à Android (Enregistrer / Partager)
// - window.print() : impression Android (Enregistrer en PDF)
(function () {
  var A = window.EliAndroid;
  if (!A) return;
  function send(blob, name) {
    var r = new FileReader();
    r.onload = function () {
      var s = String(r.result);
      A.saveFile(name || 'fichier', blob.type || 'application/octet-stream', s.slice(s.indexOf(',') + 1));
    };
    r.onerror = function () { A.toast('Export impossible.'); };
    r.readAsDataURL(blob);
  }
  // la page interdit fetch(blob:) (règle de sécurité) : on garde chaque Blob au moment où son adresse est créée
  var blobs = {}, mk = URL.createObjectURL;
  URL.createObjectURL = function (o) { var u = mk.apply(URL, arguments); if (o instanceof Blob) blobs[u] = o; return u; };
  function dataBlob(h) {
    var i = h.indexOf(','), meta = h.slice(5, i), raw = h.slice(i + 1), bin;
    bin = meta.indexOf(';base64') >= 0 ? atob(raw) : decodeURIComponent(raw);
    var u = new Uint8Array(bin.length);
    for (var k = 0; k < bin.length; k++) u[k] = bin.charCodeAt(k) & 255;
    return new Blob([u], { type: meta.split(';')[0] });
  }
  function grab(a) {
    var h = a.href || '';
    if (!a.hasAttribute('download') || !(h.indexOf('blob:') === 0 || h.indexOf('data:') === 0)) return false;
    var n = a.getAttribute('download'), b = h.indexOf('blob:') === 0 ? blobs[h] : dataBlob(h);
    if (b) send(b, n); else A.toast('Export impossible.');
    return true;
  }
  var click = HTMLAnchorElement.prototype.click;
  HTMLAnchorElement.prototype.click = function () {
    if (grab(this)) return;
    return click.apply(this, arguments);
  };
  document.addEventListener('click', function (e) {
    var a = e.target && e.target.closest ? e.target.closest('a[download]') : null;
    if (a && grab(a)) e.preventDefault();
  }, true);
  window.print = function () { A.print(document.title || 'Eli Guitar Chords'); };
})();
