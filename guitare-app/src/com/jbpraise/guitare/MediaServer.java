package com.jbpraise.guitare;

import android.content.ContentResolver;
import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Petit serveur HTTP local (127.0.0.1 uniquement) qui sert les vidéos/sons choisis au lecteur de la page.
 * Le lecteur vidéo de WebView a besoin d'un vrai serveur avec requêtes partielles (Range) pour lire et avancer.
 */
final class MediaServer {
    static final String TAG = "AccordLive";
    static final Pattern RANGE = Pattern.compile("bytes=(\\d*)-(\\d*)");
    final Context ctx;
    final Map<String, Uri> media;
    ServerSocket server;
    int port;

    MediaServer(Context ctx, Map<String, Uri> media) { this.ctx = ctx; this.media = media; }

    synchronized int start() {
        if (server != null) return port;
        try {
            server = new ServerSocket(0, 16, InetAddress.getByName("127.0.0.1"));
            port = server.getLocalPort();
            Thread t = new Thread(this::loop, "media-server");
            t.setDaemon(true);
            t.start();
        } catch (IOException e) {
            Log.e(TAG, "serveur média", e);
        }
        return port;
    }

    void loop() {
        while (true) {
            try {
                final Socket s = server.accept();
                Thread t = new Thread(() -> handle(s), "media-client");
                t.setDaemon(true);
                t.start();
            } catch (IOException e) {
                Log.e(TAG, "accept", e);
                return;
            }
        }
    }

    void handle(Socket s) {
        try (Socket sock = s) {
            sock.setSoTimeout(30000);
            BufferedReader in = new BufferedReader(new InputStreamReader(sock.getInputStream(), StandardCharsets.ISO_8859_1));
            String line = in.readLine();
            if (line == null) return;
            String[] req = line.split(" ");
            String range = null;
            for (String h; (h = in.readLine()) != null && !h.isEmpty(); ) {
                int c = h.indexOf(':');
                if (c > 0 && h.substring(0, c).trim().equalsIgnoreCase("range")) range = h.substring(c + 1).trim();
            }
            OutputStream out = sock.getOutputStream();
            if (req.length < 2 || !req[1].startsWith("/media/")) { status(out, 404, "Not Found"); return; }
            Uri u = media.get(req[1].substring(7).split("\\?")[0]);
            if (u == null) { status(out, 404, "Not Found"); return; }
            boolean head = "HEAD".equals(req[0]);
            ContentResolver cr = ctx.getContentResolver();
            String mime = cr.getType(u);
            if (mime == null) mime = "video/mp4";
            try (AssetFileDescriptor afd = cr.openAssetFileDescriptor(u, "r")) {
                if (afd == null) { status(out, 404, "Not Found"); return; }
                long len = afd.getLength();
                if (len < 0) len = sizeOf(cr, u);
                long start = 0, end = len - 1;
                boolean partial = false;
                Matcher m = range != null ? RANGE.matcher(range) : null;
                if (m != null && m.matches() && len > 0) {
                    partial = true;
                    if (m.group(1).isEmpty()) { start = Math.max(0, len - Long.parseLong(m.group(2))); }
                    else { start = Long.parseLong(m.group(1)); if (!m.group(2).isEmpty()) end = Math.min(len - 1, Long.parseLong(m.group(2))); }
                    if (start >= len) {
                        out.write(("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + len + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.ISO_8859_1));
                        return;
                    }
                }
                StringBuilder h = new StringBuilder();
                h.append(partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n");
                h.append("Content-Type: ").append(mime).append("\r\n");
                h.append("Accept-Ranges: bytes\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-cache\r\nConnection: close\r\n");
                if (len > 0) {
                    h.append("Content-Length: ").append(end - start + 1).append("\r\n");
                    if (partial) h.append("Content-Range: bytes ").append(start).append('-').append(end).append('/').append(len).append("\r\n");
                }
                h.append("\r\n");
                out.write(h.toString().getBytes(StandardCharsets.ISO_8859_1));
                if (head) return;
                InputStream data = afd.createInputStream();
                long skipped = 0;
                while (skipped < start) {
                    long k = data.skip(start - skipped);
                    if (k <= 0) { if (data.read() < 0) break; k = 1; }
                    skipped += k;
                }
                long left = len > 0 ? end - start + 1 : Long.MAX_VALUE;
                byte[] buf = new byte[64 * 1024];
                while (left > 0) {
                    int r = data.read(buf, 0, (int) Math.min(buf.length, left));
                    if (r < 0) break;
                    out.write(buf, 0, r);
                    left -= r;
                }
                out.flush();
            }
        } catch (IOException e) {
            // le lecteur ferme souvent la connexion en avançant dans la vidéo : normal
        } catch (Exception e) {
            Log.e(TAG, "média", e);
        }
    }

    static long sizeOf(ContentResolver cr, Uri u) {
        try (Cursor c = cr.query(u, new String[]{OpenableColumns.SIZE}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getLong(0);
        } catch (Exception ignored) { }
        return -1;
    }

    static void status(OutputStream out, int code, String msg) throws IOException {
        out.write(("HTTP/1.1 " + code + " " + msg + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.ISO_8859_1));
    }
}
