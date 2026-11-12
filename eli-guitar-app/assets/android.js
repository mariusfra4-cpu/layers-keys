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
  function grab(a) {
    var h = a.href || '';
    if (!a.hasAttribute('download') || !(h.indexOf('blob:') === 0 || h.indexOf('data:') === 0)) return false;
    var n = a.getAttribute('download');
    fetch(h).then(function (x) { return x.blob(); }).then(function (b) { send(b, n); })
      .catch(function () { A.toast('Export impossible.'); });
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
