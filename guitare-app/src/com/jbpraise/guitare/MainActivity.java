package com.jbpraise.guitare;

import android.Manifest;
import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.AssetFileDescriptor;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.WindowManager;
import android.webkit.ConsoleMessage;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.json.JSONObject;

import java.io.ByteArrayInputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * AccordLive : l'interface est une page web locale (assets/www) servie depuis une adresse https virtuelle,
 * ce qui permet l'accès au micro (getUserMedia). Les fichiers vidéo/audio choisis sont lus et décodés ici (MediaCodec).
 */
public class MainActivity extends Activity {
    static final String TAG = "AccordLive";
    static final String HOST = "appassets.androidplatform.net";
    static final int REQ_MIC = 1, REQ_CHOOSER = 2, REQ_PICK = 3;

    WebView web;
    final Map<String, Uri> media = new ConcurrentHashMap<>();
    int mediaSeq = 0;
    volatile float decodeProgress = -1f;
    PermissionRequest pendingPerm;
    ValueCallback<Uri[]> fileCb;
    String pendingJs;
    boolean pageLoaded;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.rgb(15, 17, 21));
        getWindow().setNavigationBarColor(Color.rgb(18, 21, 27));
        web = new WebView(this);
        web.setBackgroundColor(Color.rgb(15, 17, 21));
        setContentView(web);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false);
        s.setTextZoom(100);
        web.addJavascriptInterface(new Bridge(), "AndroidBridge");
        web.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest req) {
                Uri u = req.getUrl();
                if (!HOST.equals(u.getHost())) return null;
                try {
                    return serve(u, req.getRequestHeaders());
                } catch (Throwable t) {
                    Log.e(TAG, "serve " + u, t);
                    return error(500, "Erreur", String.valueOf(t));
                }
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest req) {
                Uri u = req.getUrl();
                if (HOST.equals(u.getHost())) return false;
                try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (Exception ignored) { }
                return true;
            }

            @Override
            public void onPageFinished(WebView v, String url) {
                pageLoaded = true;
                if (pendingJs != null) { web.evaluateJavascript(pendingJs, null); pendingJs = null; }
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onPermissionRequest(final PermissionRequest r) {
                runOnUiThread(() -> {
                    boolean audio = false;
                    for (String res : r.getResources()) if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(res)) audio = true;
                    if (!audio) { r.deny(); return; }
                    if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                        r.grant(new String[]{PermissionRequest.RESOURCE_AUDIO_CAPTURE});
                    } else {
                        pendingPerm = r;
                        requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, REQ_MIC);
                    }
                });
            }

            @Override
            public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
                if (fileCb != null) fileCb.onReceiveValue(null);
                fileCb = cb;
                try {
                    startActivityForResult(openIntent(), REQ_CHOOSER);
                } catch (Exception e) {
                    fileCb = null;
                    return false;
                }
                return true;
            }

            @Override
            public boolean onConsoleMessage(ConsoleMessage m) {
                Log.println(m.messageLevel() == ConsoleMessage.MessageLevel.ERROR ? Log.ERROR : Log.INFO, TAG,
                        "js " + m.messageLevel() + ": " + m.message() + " (" + m.sourceId() + ":" + m.lineNumber() + ")");
                return true;
            }
        });
        if (state != null) web.restoreState(state);
        else web.loadUrl("https://" + HOST + "/index.html");
        handleIntent(getIntent());
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        web.saveState(out);
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        handleIntent(i);
    }

    @Override
    public void onBackPressed() {
        web.evaluateJavascript("(window.onBack && window.onBack()) ? 1 : 0", (v) -> {
            if (!"1".equals(v)) super.onBackPressed();
        });
    }

    @Override
    protected void onDestroy() {
        web.destroy();
        super.onDestroy();
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        if (code == REQ_MIC && pendingPerm != null) {
            if (res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED)
                pendingPerm.grant(new String[]{PermissionRequest.RESOURCE_AUDIO_CAPTURE});
            else pendingPerm.deny();
            pendingPerm = null;
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        if (req == REQ_CHOOSER) {
            if (fileCb != null) fileCb.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(res, data));
            fileCb = null;
        } else if (req == REQ_PICK && res == RESULT_OK && data != null && data.getData() != null) {
            openMedia(data.getData(), null);
        }
    }

    static Intent openIntent() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"video/*", "audio/*"});
        return i;
    }

    void handleIntent(Intent i) {
        if (i == null) return;
        Uri u = null;
        if (Intent.ACTION_SEND.equals(i.getAction())) u = i.getParcelableExtra(Intent.EXTRA_STREAM);
        else if (Intent.ACTION_VIEW.equals(i.getAction())) u = i.getData();
        if (u != null) openMedia(u, i.getType());
    }

    /** Enregistre un média et prévient la page (immédiatement ou quand elle sera chargée). */
    void openMedia(Uri u, String typeHint) {
        try {
            getContentResolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) { }
        String id = "m" + (++mediaSeq);
        media.put(id, u);
        String name = displayName(u);
        String mime = getContentResolver().getType(u);
        if (mime == null) mime = typeHint != null ? typeHint : "video/*";
        String js = "window.onMediaPicked && window.onMediaPicked(" + JSONObject.quote(id) + "," + JSONObject.quote(name) + "," + JSONObject.quote(mime) + ")";
        if (pageLoaded) web.evaluateJavascript(js, null);
        else pendingJs = js;
    }

    String displayName(Uri u) {
        try (Cursor c = getContentResolver().query(u, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getString(0);
        } catch (Exception ignored) { }
        String p = u.getLastPathSegment();
        return p != null ? p : "média";
    }

    // ------------------------------------------------------------------ serveur local
    WebResourceResponse serve(Uri u, Map<String, String> headers) throws IOException {
        String path = u.getPath();
        if (path == null || path.equals("/")) path = "/index.html";
        if (path.startsWith("/media/")) {
            Uri m = media.get(path.substring(7));
            if (m == null) return error(404, "Not Found", "média inconnu");
            return serveMedia(m, headers);
        }
        if (path.startsWith("/decode/")) {
            Uri m = media.get(path.substring(8));
            if (m == null) return error(404, "Not Found", "média inconnu");
            return serveDecoded(m);
        }
        if (path.contains("..")) return error(403, "Forbidden", "");
        InputStream in;
        try {
            in = getAssets().open("www" + path);
        } catch (IOException e) {
            return error(404, "Not Found", path);
        }
        WebResourceResponse r = new WebResourceResponse(mimeOf(path), "utf-8", in);
        Map<String, String> h = new HashMap<>();
        h.put("Cache-Control", "no-cache");
        r.setResponseHeaders(h);
        return r;
    }

    static String mimeOf(String p) {
        if (p.endsWith(".html")) return "text/html";
        if (p.endsWith(".js")) return "text/javascript";
        if (p.endsWith(".css")) return "text/css";
        if (p.endsWith(".png")) return "image/png";
        if (p.endsWith(".svg")) return "image/svg+xml";
        if (p.endsWith(".json")) return "application/json";
        return "application/octet-stream";
    }

    static WebResourceResponse error(int code, String reason, String msg) {
        return new WebResourceResponse("text/plain", "utf-8", code, reason, new HashMap<String, String>(),
                new ByteArrayInputStream(msg.getBytes()));
    }

    static final Pattern RANGE = Pattern.compile("bytes=(\\d*)-(\\d*)");

    /** Lecture d'un média avec prise en charge des requêtes partielles (avance rapide dans la vidéo). */
    WebResourceResponse serveMedia(Uri u, Map<String, String> headers) throws IOException {
        ContentResolver cr = getContentResolver();
        String mime = cr.getType(u);
        if (mime == null) mime = "video/mp4";
        AssetFileDescriptor afd = cr.openAssetFileDescriptor(u, "r");
        if (afd == null) return error(404, "Not Found", "");
        long len = afd.getLength();
        if (len < 0) {
            try (Cursor c = cr.query(u, new String[]{OpenableColumns.SIZE}, null, null, null)) {
                if (c != null && c.moveToFirst() && !c.isNull(0)) len = c.getLong(0);
            } catch (Exception ignored) { }
        }
        InputStream in = afd.createInputStream();
        String range = null;
        for (Map.Entry<String, String> e : headers.entrySet()) if ("range".equalsIgnoreCase(e.getKey())) range = e.getValue();
        Map<String, String> h = new HashMap<>();
        h.put("Accept-Ranges", "bytes");
        h.put("Cache-Control", "no-cache");
        Matcher m = range != null ? RANGE.matcher(range) : null;
        if (m != null && m.matches() && len > 0) {
            long start = m.group(1).isEmpty() ? 0 : Long.parseLong(m.group(1));
            long end = m.group(2).isEmpty() ? len - 1 : Math.min(len - 1, Long.parseLong(m.group(2)));
            if (m.group(1).isEmpty() && !m.group(2).isEmpty()) { start = Math.max(0, len - Long.parseLong(m.group(2))); end = len - 1; }
            if (start >= len) { in.close(); h.put("Content-Range", "bytes */" + len); return new WebResourceResponse(mime, null, 416, "Range Not Satisfiable", h, new ByteArrayInputStream(new byte[0])); }
            long skipped = 0;
            while (skipped < start) {
                long k = in.skip(start - skipped);
                if (k <= 0) { if (in.read() < 0) break; k = 1; }
                skipped += k;
            }
            long n = end - start + 1;
            h.put("Content-Range", "bytes " + start + "-" + end + "/" + len);
            h.put("Content-Length", String.valueOf(n));
            return new WebResourceResponse(mime, null, 206, "Partial Content", h, new Limited(in, n));
        }
        if (len > 0) h.put("Content-Length", String.valueOf(len));
        return new WebResourceResponse(mime, null, 200, "OK", h, in);
    }

    static class Limited extends FilterInputStream {
        long left;
        Limited(InputStream in, long n) { super(in); left = n; }
        @Override public int read() throws IOException { if (left <= 0) return -1; int b = super.read(); if (b >= 0) left--; return b; }
        @Override public int read(byte[] b, int o, int l) throws IOException {
            if (left <= 0) return -1;
            int r = super.read(b, o, (int) Math.min(l, left));
            if (r > 0) left -= r;
            return r;
        }
        @Override public int available() throws IOException { return (int) Math.min(super.available(), left); }
    }

    /** Piste audio décodée, mono, 11 025 Hz, en float32 petit-boutiste. */
    WebResourceResponse serveDecoded(Uri u) {
        decodeProgress = 0f;
        try {
            Decoder.Result r = Decoder.decode(this, u, 11025, (p) -> decodeProgress = p);
            Map<String, String> h = new HashMap<>();
            h.put("X-Sample-Rate", String.valueOf(r.sampleRate));
            h.put("Access-Control-Expose-Headers", "X-Sample-Rate");
            h.put("Content-Length", String.valueOf(r.bytes.length));
            return new WebResourceResponse("application/octet-stream", null, 200, "OK", h, new ByteArrayInputStream(r.bytes));
        } catch (Throwable t) {
            Log.e(TAG, "décodage", t);
            return error(500, "Decode Error", String.valueOf(t.getMessage()));
        } finally {
            decodeProgress = -1f;
        }
    }

    // ------------------------------------------------------------------ pont JavaScript
    class Bridge {
        @JavascriptInterface
        public void keepScreenOn(final boolean on) {
            runOnUiThread(() -> {
                if (on) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            });
        }

        @JavascriptInterface
        public void pickMedia() {
            runOnUiThread(() -> {
                try { startActivityForResult(openIntent(), REQ_PICK); } catch (Exception e) { Log.e(TAG, "sélecteur", e); }
            });
        }

        @JavascriptInterface
        public String decodeProgress() {
            return String.valueOf(decodeProgress);
        }

        @JavascriptInterface
        public void shareText(final String text) {
            runOnUiThread(() -> {
                Intent i = new Intent(Intent.ACTION_SEND);
                i.setType("text/plain");
                i.putExtra(Intent.EXTRA_TEXT, text);
                startActivity(Intent.createChooser(i, "Partager la grille d’accords"));
            });
        }
    }
}
