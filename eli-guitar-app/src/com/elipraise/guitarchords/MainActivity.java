package com.elipraise.guitarchords;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.print.PrintAttributes;
import android.print.PrintManager;
import android.util.Base64;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Eli Guitar Chords pour Android : la page index.html (dans assets/) est affichée dans une WebView
 * et servie depuis une adresse https interne à l'appli, pour que le code d'accès (crypto),
 * la mémoire (localStorage) et le micro fonctionnent comme dans le navigateur, sans Internet.
 */
public class MainActivity extends Activity {
    static final String HOST = "eli-guitar-chords.app";
    static final String HOME = "https://" + HOST + "/index.html";
    private static final int REQ_FILE = 11, REQ_SAVE = 12, REQ_MIC = 13;

    private WebView web;
    private ValueCallback<Uri[]> fileCb;
    private PermissionRequest micReq;
    private byte[] pendData;
    private String pendName, pendMime;
    private String salt = "";
    private final List<String> oldHashes = new ArrayList<>();
    private boolean checking;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        makeWeb();
        readOldCodes();
        if (b != null) web.restoreState(b);
        if (web.getUrl() == null) web.loadUrl(HOME);
    }

    /** Crée la WebView (aussi après un plantage du moteur d'affichage). */
    private void makeWeb() {
        web = new WebView(this);
        web.setLayoutParams(new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        boolean night = (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        web.setBackgroundColor(night ? Color.rgb(0x16, 0x14, 0x0f) : Color.rgb(0xfa, 0xf8, 0xf4));
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(true);
        s.setSupportZoom(false);
        s.setTextZoom(100);
        s.setCacheMode(WebSettings.LOAD_NO_CACHE);
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        web.addJavascriptInterface(new Bridge(), "EliAndroid");
        web.setWebViewClient(new Client());
        web.setWebChromeClient(new Chrome());
        setContentView(web);
    }

    @Override protected void onResume() {
        super.onResume();
        long now = System.currentTimeMillis();
        Licence.touch(this, now);
        if (web == null) return;
        if (now - Licence.lastCheck(this) > Licence.RECHECK_MS) checkLicence(null);
        else if (!Licence.isOpen(this, now) && shownOpen) web.reload();
    }

    // ---------- licence ----------

    private volatile boolean shownOpen; // la page affichée est l'appli (et non l'écran de licence)

    /** SALT et empreintes des anciens codes, lus dans index.html (pour accepter les anciens codes pendant la transition). */
    private void readOldCodes() {
        try {
            String h = new String(asset("index.html"), StandardCharsets.UTF_8);
            Matcher m = Pattern.compile("SALT\\s*=\\s*'([^']*)'").matcher(h);
            if (m.find()) salt = m.group(1);
            m = Pattern.compile("HS\\s*=\\s*\\[([^\\]]*)\\]").matcher(h);
            if (m.find()) {
                Matcher q = Pattern.compile("'([0-9a-f]{16})'").matcher(m.group(1));
                while (q.find()) oldHashes.add(q.group(1));
            }
        } catch (IOException e) { /* pas d'anciens codes */ }
    }

    private final List<String> licenceCbs = new ArrayList<>();

    /** Vérifie la licence en arrière-plan ; js = fonction de la page à rappeler avec le résultat (ou null). */
    private void checkLicence(final String js) {
        if (js != null) licenceCbs.add(js);
        if (checking) return;
        checking = true;
        new Thread(() -> {
            final Licence.Result r = Licence.check(getApplicationContext(), salt);
            runOnUiThread(() -> {
                checking = false;
                if (isFinishing() || isDestroyed() || web == null) { licenceCbs.clear(); return; }
                if (!licenceCbs.isEmpty()) {
                    for (String cb : licenceCbs) web.evaluateJavascript(cb + "(" + licenceJson() + ")", null);
                    licenceCbs.clear();
                } else if (r.open != shownOpen) web.reload();
            });
        }).start();
    }

    private static String q(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : (s == null ? "" : s).toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 32 || c == 0x2028 || c == 0x2029) b.append(String.format(Locale.ROOT, "\\u%04x", (int) c));
            else b.append(c);
        }
        return b.append('"').toString();
    }

    private String licenceJson() {
        long now = System.currentTimeMillis();
        boolean open = Licence.isOpen(this, now);
        long ok = Licence.lastOk(this);
        return "{\"open\":" + open + ",\"code\":" + q(Licence.deviceCode(this))
                + ",\"why\":" + q(Licence.reason(this)) + ",\"msg\":" + q(Licence.message(this))
                + ",\"until\":" + q(ok > 0 ? Licence.fr(Licence.today(ok + Licence.GRACE_MS)) : "")
                + ",\"old\":" + q(Licence.oldCode(this)) + "}";
    }

    @Override protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        web.saveState(out);
    }

    @Override public void onBackPressed() {
        if (web.canGoBack()) web.goBack();
        else super.onBackPressed();
    }

    @Override protected void onDestroy() {
        if (web != null) { web.stopLoading(); web.destroy(); web = null; }
        super.onDestroy();
    }

    // ---------- fichiers de l'appli ----------

    static String mimeOf(String name, String def) {
        if (name == null) return def;
        String n = name.toLowerCase();
        if (n.endsWith(".html") || n.endsWith(".htm")) return "text/html";
        if (n.endsWith(".js")) return "application/javascript";
        if (n.endsWith(".css")) return "text/css";
        if (n.endsWith(".json")) return "application/json";
        if (n.endsWith(".png")) return "image/png";
        if (n.endsWith(".jpg") || n.endsWith(".jpeg")) return "image/jpeg";
        if (n.endsWith(".svg")) return "image/svg+xml";
        if (n.endsWith(".mid") || n.endsWith(".midi")) return "audio/midi";
        if (n.endsWith(".pdf")) return "application/pdf";
        if (n.endsWith(".txt")) return "text/plain";
        if (n.endsWith(".mp3")) return "audio/mpeg";
        if (n.endsWith(".wav")) return "audio/wav";
        return def != null ? def : "application/octet-stream";
    }

    private byte[] asset(String path) throws IOException {
        InputStream in = getAssets().open(path);
        try {
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
            return o.toByteArray();
        } finally { in.close(); }
    }

    private WebResourceResponse serve(String path) {
        if (path == null || path.equals("") || path.equals("/")) path = "/index.html";
        String p = path.substring(1);
        if (p.contains("..")) return notFound();
        try {
            byte[] data = asset(p);
            String mime = mimeOf(p, null);
            if (p.equals("index.html") || p.equals("licence.html")) {
                boolean open = Licence.isOpen(this, System.currentTimeMillis());
                if (!open) { p = "licence.html"; data = asset(p); mime = "text/html"; }
                shownOpen = open && p.equals("index.html");
                String html = new String(data, StandardCharsets.UTF_8);
                if (shownOpen) // la licence est gérée par l'appli : l'ancien écran de code et « Verrouiller » sont masqués
                    html = html.replaceFirst("</head>", "<style>#lk,[data-lock]{display:none!important}</style></head>");
                String shim = new String(asset("android.js"), StandardCharsets.UTF_8);
                int i = html.indexOf("<head>");
                String tag = "<script>" + shim + "</script>";
                html = i >= 0 ? html.substring(0, i + 6) + tag + html.substring(i + 6) : tag + html;
                // dans l'appli, les fichiers s'enregistrent directement : ce conseil ne s'applique plus
                html = html.replace(" Si rien ne se télécharge ici, ouvre l’appli depuis ton adresse https (Netlify).", "");
                data = html.getBytes(StandardCharsets.UTF_8);
            }
            Map<String, String> h = new HashMap<>();
            h.put("Cache-Control", "no-store");
            h.put("Access-Control-Allow-Origin", "*");
            return new WebResourceResponse(mime, mime.startsWith("text/") || mime.contains("javascript") ? "utf-8" : null,
                    200, "OK", h, new ByteArrayInputStream(data));
        } catch (IOException e) {
            return notFound();
        }
    }

    private static WebResourceResponse notFound() {
        return new WebResourceResponse("text/plain", "utf-8", 404, "Not Found",
                new HashMap<String, String>(), new ByteArrayInputStream(new byte[0]));
    }

    private class Client extends WebViewClient {
        /** Le moteur d'affichage s'est arrêté (manque de mémoire…) : on recrée la page au lieu de fermer l'appli. */
        @Override public boolean onRenderProcessGone(WebView v, android.webkit.RenderProcessGoneDetail d) {
            if (v != web) return true;
            ViewGroup parent = (ViewGroup) web.getParent();
            if (parent != null) parent.removeView(web);
            web.destroy();
            web = null;
            if (isFinishing() || isDestroyed()) return true;
            makeWeb();
            web.loadUrl(HOME);
            return true;
        }

        @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) {
            Uri u = r.getUrl();
            if (HOST.equals(u.getHost())) return serve(u.getPath());
            String sc = u.getScheme();
            if ("http".equals(sc) || "https".equals(sc)) return notFound(); // l'appli fonctionne hors ligne
            return null;
        }

        @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
            Uri u = r.getUrl();
            if (HOST.equals(u.getHost())) return false;
            try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (ActivityNotFoundException e) { /* rien */ }
            return true;
        }
    }

    // ---------- choix d'un fichier audio / vidéo, micro ----------

    private class Chrome extends WebChromeClient {
        @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
            if (fileCb != null) fileCb.onReceiveValue(null);
            fileCb = cb;
            List<String> types = new ArrayList<>();
            for (String t : p.getAcceptTypes()) {
                if (t == null) continue;
                for (String x : t.split(",")) {
                    x = x.trim();
                    if (x.contains("/")) types.add(x);
                }
            }
            Intent i = new Intent(Intent.ACTION_GET_CONTENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            if (types.size() == 1) i.setType(types.get(0));
            else {
                i.setType("*/*");
                if (!types.isEmpty()) i.putExtra(Intent.EXTRA_MIME_TYPES, types.toArray(new String[0]));
            }
            if (p.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE) i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
            try {
                startActivityForResult(Intent.createChooser(i, "Choisir un morceau"), REQ_FILE);
            } catch (ActivityNotFoundException e) {
                fileCb = null;
                toast("Aucune appli pour choisir un fichier.");
                return false;
            }
            return true;
        }

        @Override public void onPermissionRequest(final PermissionRequest r) {
            runOnUiThread(new Runnable() { public void run() {
                boolean wantsMic = false;
                for (String res : r.getResources())
                    if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(res)) wantsMic = true;
                if (!wantsMic) { r.deny(); return; }
                if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                    r.grant(new String[]{PermissionRequest.RESOURCE_AUDIO_CAPTURE});
                } else {
                    if (micReq != null) micReq.deny();
                    micReq = r;
                    requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, REQ_MIC);
                }
            }});
        }
    }

    @Override public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        if (code != REQ_MIC || micReq == null) return;
        if (res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED)
            micReq.grant(new String[]{PermissionRequest.RESOURCE_AUDIO_CAPTURE});
        else {
            micReq.deny();
            toast("Micro refusé : autorise-le dans les réglages de l'appli.");
        }
        micReq = null;
    }

    @Override protected void onActivityResult(int code, int result, Intent data) {
        if (code == REQ_FILE) {
            Uri[] r = null;
            if (result == RESULT_OK && data != null) {
                ClipData cd = data.getClipData();
                if (cd != null && cd.getItemCount() > 0) {
                    r = new Uri[cd.getItemCount()];
                    for (int i = 0; i < r.length; i++) r[i] = cd.getItemAt(i).getUri();
                } else if (data.getData() != null) r = new Uri[]{data.getData()};
            }
            if (fileCb != null) fileCb.onReceiveValue(r);
            fileCb = null;
        } else if (code == REQ_SAVE) {
            if (result == RESULT_OK && data != null && data.getData() != null && pendData != null) {
                try {
                    OutputStream o = getContentResolver().openOutputStream(data.getData());
                    o.write(pendData);
                    o.close();
                    toast("Fichier enregistré : " + pendName);
                } catch (Exception e) {
                    toast("Enregistrement impossible.");
                }
            }
        } else super.onActivityResult(code, result, data);
    }

    // ---------- export (MIDI) et impression (PDF) ----------

    private void toast(String m) { Toast.makeText(this, m, Toast.LENGTH_LONG).show(); }

    private static String safeName(String n) {
        n = n == null ? "" : n.replaceAll("[\\\\/:*?\"<>|\\x00-\\x1f]", "_").trim();
        if (n.isEmpty() || n.startsWith(".")) n = "fichier" + n;
        return n;
    }

    private void offer(final String name, final String mime, final byte[] data) {
        pendData = data; pendName = name; pendMime = mime;
        new AlertDialog.Builder(this)
                .setTitle(name)
                .setMessage("Que veux-tu faire de ce fichier ?")
                .setPositiveButton("Enregistrer", (d, w) -> save())
                .setNeutralButton("Partager", (d, w) -> share())
                .setNegativeButton("Annuler", null)
                .show();
    }

    private void save() {
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType(pendMime);
        i.putExtra(Intent.EXTRA_TITLE, pendName);
        try { startActivityForResult(i, REQ_SAVE); }
        catch (ActivityNotFoundException e) { share(); }
    }

    private void share() {
        try {
            File f = new File(ShareProvider.dir(this), pendName);
            FileOutputStream o = new FileOutputStream(f);
            o.write(pendData);
            o.close();
            Uri u = Uri.parse("content://" + ShareProvider.AUTH + "/" + Uri.encode(pendName));
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType(pendMime);
            i.putExtra(Intent.EXTRA_STREAM, u);
            i.setClipData(ClipData.newRawUri(pendName, u));
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(Intent.createChooser(i, "Partager " + pendName));
        } catch (Exception e) {
            toast("Partage impossible.");
        }
    }

    private void printPage(String title) {
        PrintManager pm = (PrintManager) getSystemService(PRINT_SERVICE);
        if (pm == null) { toast("Impression indisponible."); return; }
        String job = title == null || title.isEmpty() ? "Eli Guitar Chords" : title;
        PrintAttributes a = new PrintAttributes.Builder().setMediaSize(PrintAttributes.MediaSize.ISO_A4).build();
        pm.print(job, web.createPrintDocumentAdapter(job), a);
    }

    /** Fonctions appelées par la page (voir assets/android.js). */
    private class Bridge {
        @JavascriptInterface public void saveFile(final String name, final String mime, String b64) {
            final byte[] data;
            try { data = Base64.decode(b64, Base64.DEFAULT); }
            catch (IllegalArgumentException e) { return; }
            final String n = safeName(name);
            final String m = mime == null || mime.isEmpty() ? mimeOf(n, null) : mime;
            runOnUiThread(() -> offer(n, m, data));
        }

        @JavascriptInterface public void print(final String title) {
            runOnUiThread(() -> printPage(title));
        }

        @JavascriptInterface public void toast(final String m) {
            runOnUiThread(() -> MainActivity.this.toast(m));
        }

        @JavascriptInterface public boolean licensed() {
            return Licence.isOpen(MainActivity.this, System.currentTimeMillis());
        }

        @JavascriptInterface public String licenceInfo() { return licenceJson(); }

        @JavascriptInterface public void checkLicence(String cb) {
            final String js = cb != null && cb.matches("[A-Za-z_$][A-Za-z0-9_$.]*") ? cb : "void";
            runOnUiThread(() -> MainActivity.this.checkLicence(js));
        }

        /** Ancien code d'accès : 1 = reconnu (vérification lancée), 0 = code inconnu. */
        @JavascriptInterface public int oldCode(String code, String cb) {
            String h = Licence.oldHash(salt, code);
            if (!oldHashes.contains(h)) return 0;
            Licence.setOldCode(MainActivity.this, code);
            checkLicence(cb);
            return 1;
        }

        @JavascriptInterface public void copyCode() {
            runOnUiThread(() -> {
                ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("Code", Licence.deviceCode(MainActivity.this)));
                MainActivity.this.toast("Code copié.");
            });
        }

        @JavascriptInterface public void shareCode() {
            runOnUiThread(() -> {
                Intent i = new Intent(Intent.ACTION_SEND);
                i.setType("text/plain");
                i.putExtra(Intent.EXTRA_TEXT, "Bonjour, voici le code de mon téléphone pour activer Eli Guitar Chords : "
                        + Licence.deviceCode(MainActivity.this));
                try { startActivity(Intent.createChooser(i, "Envoyer le code")); }
                catch (ActivityNotFoundException e) { MainActivity.this.toast("Aucune appli pour envoyer le code."); }
            });
        }

        @JavascriptInterface public void openApp() {
            runOnUiThread(() -> web.loadUrl(HOME));
        }

        @JavascriptInterface public int sdk() { return Build.VERSION.SDK_INT; }
    }
}
