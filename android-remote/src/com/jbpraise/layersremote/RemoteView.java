package com.jbpraise.layersremote;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

/**
 * Toute l'interface de la télécommande, dessinée directement (rapide, aucune mise en page Android) :
 *   - MIXEUR : 8 pistes (nom, M / S / H, fader de volume) + fader du volume général ;
 *   - LIVE : 8 presets, Leslie lent / rapide, sustain, tout arrêter, réverbération ;
 *   - RÉGLAGES : connexion USB / Wi-Fi, canal MIDI, type de boutons, renvoyer tout.
 * Plusieurs doigts à la fois (deux faders en même temps). Les faders bougent « en relatif » : poser le doigt ne fait
 * jamais sauter le volume, seul le mouvement compte (indispensable en live).
 */
public final class RemoteView extends View {
    public interface Host {
        void send(byte[] msg);
        void rename(int track);
        String usbStatus();
        String wifiStatus();
        boolean connected();
        void rescanUsb();
    }

    static final int PAGE_MIX = 0, PAGE_LIVE = 1, PAGE_SET = 2;

    // couleurs (mêmes que Layers Keys)
    static final int BG = 0xff070b12, PANEL = 0xff111a2b, PANEL2 = 0xff0b1320, EDGE = 0xff2b3b57,
        GOLD = 0xfff5c21f, GOLD_DIM = 0xff8a6d12, TEXT = 0xffeef2f8, TEXT2 = 0xff8b97ad,
        RED = 0xffe5484d, BLUE = 0xff38a1ff, GREEN = 0xff35c26b, DARK = 0xff1a1400;

    final RemoteState st;
    final Host host;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rf = new RectF();
    private float W, H, top;

    // ce que chaque doigt tient
    private static final int MAXP = 10;
    private final int[] holdKind = new int[MAXP], holdIdx = new int[MAXP];
    private final float[] holdY = new float[MAXP], holdV = new float[MAXP];
    static final int H_NONE = 0, H_FADER = 1, H_BTN = 2, H_HFADER = 3;

    public RemoteView(Context c, RemoteState st, Host host) {
        super(c);
        this.st = st;
        this.host = host;
        setHapticFeedbackEnabled(true);
        p.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
    }

    // ---------------------------------------------------------------- dessin

    @Override protected void onDraw(Canvas c) {
        W = getWidth();
        H = getHeight();
        top = Math.max(H * 0.12f, dp(52));
        c.drawColor(BG);
        drawTop(c);
        switch (st.page) {
            case PAGE_MIX: drawMixer(c); break;
            case PAGE_LIVE: drawLive(c); break;
            default: drawSettings(c);
        }
    }

    float dp(float v) { return v * getResources().getDisplayMetrics().density; }

    private void text(Canvas c, String s, float cx, float cy, float size, int col, boolean center) {
        p.setTextSize(size);
        p.setColor(col);
        p.setShader(null);
        p.setTextAlign(center ? Paint.Align.CENTER : Paint.Align.LEFT);
        Paint.FontMetrics fm = p.getFontMetrics();
        c.drawText(s, cx, cy - (fm.ascent + fm.descent) / 2, p);
    }

    // texte qui tient dans une largeur (réduit au besoin)
    private void textFit(Canvas c, String s, float cx, float cy, float size, float maxW, int col) {
        p.setTextSize(size);
        float w = p.measureText(s);
        if (w > maxW && w > 0) size = Math.max(size * maxW / w, dp(8));
        text(c, s, cx, cy, size, col, true);
    }

    private void box(Canvas c, float x, float y, float w, float h, float r, int fill, int edge) {
        rf.set(x, y, x + w, y + h);
        p.setShader(null);
        p.setStyle(Paint.Style.FILL);
        p.setColor(fill);
        c.drawRoundRect(rf, r, r, p);
        if (edge != 0) {
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(dp(1));
            p.setColor(edge);
            c.drawRoundRect(rf, r, r, p);
            p.setStyle(Paint.Style.FILL);
        }
    }

    private void button(Canvas c, float x, float y, float w, float h, String label, boolean on, int onCol, float ts) {
        if (on) {
            rf.set(x, y, x + w, y + h);
            p.setStyle(Paint.Style.FILL);
            p.setShader(new LinearGradient(0, y, 0, y + h, lighter(onCol), onCol, Shader.TileMode.CLAMP));
            c.drawRoundRect(rf, dp(8), dp(8), p);
            p.setShader(null);
        } else {
            box(c, x, y, w, h, dp(8), 0xff1c2639, 0xff3a4c6b);
        }
        textFit(c, label, x + w / 2, y + h / 2, ts, w - dp(6), on ? DARK : TEXT);
    }

    static int lighter(int col) {
        int r = (col >> 16) & 255, g = (col >> 8) & 255, b = col & 255;
        r = r + (255 - r) / 4; g = g + (255 - g) / 4; b = b + (255 - b) / 4;
        return 0xff000000 | (r << 16) | (g << 8) | b;
    }

    // onglets : x de départ et largeur
    private float tabX(int i) { return W * 0.30f + i * (W * 0.15f + dp(8)); }
    private float tabW() { return W * 0.15f; }

    private void drawTop(Canvas c) {
        box(c, 0, 0, W, top, 0, PANEL2, 0);
        text(c, "LAYERS REMOTE", dp(14), top * 0.40f, top * 0.30f, GOLD, false);
        text(c, "by JB Praise", dp(14), top * 0.78f, top * 0.18f, TEXT2, false);
        String[] tabs = {"MIXEUR", "LIVE", "RÉGLAGES"};
        for (int i = 0; i < 3; i++) {
            button(c, tabX(i), top * 0.16f, tabW(), top * 0.68f, tabs[i], st.page == i, GOLD, top * 0.26f);
        }
        boolean ok = host.connected();
        float sx = W * 0.79f, sw = W * 0.21f - dp(10);
        box(c, sx, top * 0.2f, sw, top * 0.6f, top * 0.3f, ok ? 0xff0f2a1a : 0xff2a1214, ok ? GREEN : RED);
        p.setColor(ok ? GREEN : RED);
        c.drawCircle(sx + top * 0.3f, top * 0.5f, top * 0.09f, p);
        textFit(c, ok ? "CONNECTÉ" : "NON CONNECTÉ", sx + sw / 2 + top * 0.12f, top * 0.5f, top * 0.22f, sw - top * 0.6f, ok ? GREEN : RED);
    }

    // ---- MIXEUR

    private float stripW() { return (W - dp(10)) / 9f; }
    private float stripX(int i) { return dp(5) + i * stripW(); }
    private float btnTop() { return top + dp(36); }
    private float btnH() { return Math.max(dp(40), H * 0.10f); }
    private float fadTop() { return btnTop() + btnH() + dp(12); }
    private float fadBot() { return H - dp(34); }

    private void drawMixer(Canvas c) {
        for (int i = 0; i < 9; i++) {
            float x = stripX(i), w = stripW() - dp(6);
            boolean master = i == 8;
            box(c, x, top + dp(6), w, H - top - dp(10), dp(10), master ? 0xff151d10 : PANEL, master ? GOLD_DIM : EDGE);
            String name = master ? "GÉNÉRAL" : st.names[i];
            int nameCol = master ? GOLD : (st.mute[i] ? TEXT2 : TEXT);
            textFit(c, master ? name : (i + 1) + "  " + name, x + w / 2, top + dp(22), dp(13), w - dp(8), nameCol);
            if (!master) {
                float bw = (w - dp(16)) / 3f;
                float by = btnTop(), bh = btnH();
                button(c, x + dp(4), by, bw, bh, "M", st.mute[i], RED, bh * 0.42f);
                button(c, x + dp(8) + bw, by, bw, bh, "S", st.solo[i], GOLD, bh * 0.42f);
                button(c, x + dp(12) + 2 * bw, by, bw, bh, "H", st.hold[i], BLUE, bh * 0.42f);
            } else {
                textFit(c, "volume", x + w / 2, btnTop() + btnH() / 2, dp(12), w - dp(8), TEXT2);
            }
            float v = master ? st.master : st.vol[i];
            drawFader(c, x + w / 2, fadTop(), fadBot(), w, v, master ? GOLD : (st.mute[i] ? 0xff6b7486 : GOLD));
            textFit(c, Math.round(v * 100) + " %", x + w / 2, H - dp(20), dp(13), w - dp(8), master ? GOLD : TEXT2);
        }
    }

    private void drawFader(Canvas c, float cx, float y0, float y1, float w, float v, int col) {
        float gw = dp(8);
        box(c, cx - gw / 2, y0, gw, y1 - y0, gw / 2, 0xff03060a, 0xff26364f);
        float capH = Math.max(dp(34), (y1 - y0) * 0.09f), capW = Math.min(w * 0.78f, dp(90));
        float yy = y1 - capH / 2 - v * (y1 - y0 - capH);
        // partie remplie (sous le bouton)
        rf.set(cx - gw / 2, yy, cx + gw / 2, y1);
        p.setColor(col);
        p.setAlpha(170);
        c.drawRoundRect(rf, gw / 2, gw / 2, p);
        p.setAlpha(255);
        // graduations
        p.setColor(0xff26364f);
        for (int k = 0; k <= 10; k++) {
            float ty = y1 - capH / 2 - k / 10f * (y1 - y0 - capH);
            c.drawRect(cx - capW / 2, ty - dp(0.5f), cx - gw, ty + dp(0.5f), p);
        }
        // bouton du fader
        rf.set(cx - capW / 2, yy - capH / 2, cx + capW / 2, yy + capH / 2);
        p.setShader(new LinearGradient(0, rf.top, 0, rf.bottom, 0xff4a5568, 0xff1a202c, Shader.TileMode.CLAMP));
        c.drawRoundRect(rf, dp(6), dp(6), p);
        p.setShader(null);
        p.setColor(col);
        c.drawRect(rf.left + dp(4), yy - dp(1.5f), rf.right - dp(4), yy + dp(1.5f), p);
    }

    // ---- LIVE

    private void drawLive(Canvas c) {
        float y0 = top + dp(12), h = H - y0 - dp(12);
        float lw = W * 0.58f;
        // presets 2 x 4
        text(c, "PRESETS", dp(14), y0 + dp(10), dp(13), GOLD, false);
        float gy = y0 + dp(24), gh = h - dp(24);
        float bw = (lw - dp(14) - dp(30)) / 4f, bh = (gh - dp(10)) / 2f;
        for (int k = 0; k < 8; k++) {
            float x = dp(14) + (k % 4) * (bw + dp(10)), y = gy + (k / 4) * (bh + dp(10));
            button(c, x, y, bw, bh, "PRESET " + (k + 1), st.preset == k, GOLD, Math.min(bh * 0.2f, dp(20)));
        }
        // colonne de droite
        float rx = lw + dp(10), rw = W - rx - dp(14);
        float rh = (h - dp(30)) / 4f;
        button(c, rx, y0, rw, rh, st.leslieFast ? "LESLIE : RAPIDE" : "LESLIE : LENT", st.leslieFast, GOLD, Math.min(rh * 0.3f, dp(22)));
        button(c, rx, y0 + rh + dp(10), rw, rh, "SUSTAIN (tiens)", st.sustain, BLUE, Math.min(rh * 0.3f, dp(22)));
        button(c, rx, y0 + 2 * (rh + dp(10)), rw, rh, "TOUT ARRÊTER", st.panicFlash, RED, Math.min(rh * 0.3f, dp(22)));
        // réverbération (fader horizontal)
        float fy = y0 + 3 * (rh + dp(10));
        box(c, rx, fy, rw, rh, dp(10), PANEL, EDGE);
        text(c, "RÉVERB " + Math.round(st.reverb * 100) + " %", rx + rw / 2, fy + rh * 0.25f, Math.min(rh * 0.22f, dp(16)), TEXT, true);
        float tx0 = rx + dp(20), tx1 = rx + rw - dp(20), ty = fy + rh * 0.65f;
        box(c, tx0, ty - dp(4), tx1 - tx0, dp(8), dp(4), 0xff03060a, 0xff26364f);
        box(c, tx0, ty - dp(4), (tx1 - tx0) * st.reverb, dp(8), dp(4), GOLD, 0);
        p.setColor(GOLD);
        c.drawCircle(tx0 + (tx1 - tx0) * st.reverb, ty, dp(13), p);
    }

    // ---- RÉGLAGES

    private float setRowH() { return Math.max(dp(46), (H - top) / 7.5f); }

    private void drawSettings(Canvas c) {
        float x = dp(18), y = top + dp(12), rh = setRowH(), w = W - 2 * x;
        float ts = Math.min(rh * 0.32f, dp(16));
        // USB
        box(c, x, y, w, rh * 1.6f, dp(10), PANEL, EDGE);
        text(c, "CÂBLE USB (le plus rapide)", x + dp(14), y + rh * 0.35f, ts, GOLD, false);
        text(c, host.usbStatus(), x + dp(14), y + rh * 0.8f, ts, TEXT, false);
        text(c, "Branche le câble, puis dans la notification « USB » du téléphone choisis « MIDI ».", x + dp(14), y + rh * 1.25f, ts * 0.85f, TEXT2, false);
        button(c, x + w - dp(150), y + rh * 0.3f, dp(136), rh * 0.75f, "ACTUALISER", false, GOLD, ts * 0.9f);
        y += rh * 1.6f + dp(10);
        // Wi-Fi
        box(c, x, y, w, rh * 2.1f, dp(10), PANEL, EDGE);
        text(c, "WI-FI (réseau MIDI Apple / rtpMIDI)", x + dp(14), y + rh * 0.35f, ts, GOLD, false);
        text(c, host.wifiStatus(), x + dp(14), y + rh * 0.8f, ts, TEXT, false);
        text(c, "Mac : Configuration audio et MIDI > Fenêtre > Afficher le studio MIDI > Réseau > « Layers Remote » > Se connecter.", x + dp(14), y + rh * 1.25f, ts * 0.8f, TEXT2, false);
        text(c, "Windows : installe « rtpMIDI » (gratuit), puis connecte « Layers Remote » dans sa liste.", x + dp(14), y + rh * 1.65f, ts * 0.8f, TEXT2, false);
        y += rh * 2.1f + dp(10);
        // canal MIDI + boutons + renvoyer
        float cw = (w - dp(20)) / 3f;
        box(c, x, y, cw, rh, dp(10), PANEL, EDGE);
        text(c, "CANAL MIDI : " + (st.channel + 1), x + cw / 2, y + rh / 2, ts, TEXT, true);
        button(c, x + dp(6), y + rh * 0.15f, rh * 0.7f, rh * 0.7f, "−", false, GOLD, ts * 1.3f);
        button(c, x + cw - dp(6) - rh * 0.7f, y + rh * 0.15f, rh * 0.7f, rh * 0.7f, "+", false, GOLD, ts * 1.3f);
        button(c, x + cw + dp(10), y, cw, rh, st.momentary ? "BOUTONS : MOMENTANÉS" : "BOUTONS : BASCULE", !st.momentary, BLUE, ts * 0.9f);
        button(c, x + 2 * (cw + dp(10)), y, cw, rh, "RENVOYER TOUT", false, GOLD, ts * 0.9f);
        y += rh + dp(10);
        text(c, "Momentané (conseillé) : chaque appui fait basculer Muet / Solo / Hold dans Layers Keys et les DAW.", x, y + dp(10), ts * 0.8f, TEXT2, false);
        text(c, "Layers Keys : Paramètres > « Associer la télécommande Layers Remote ». DAW : MIDI Learn sur chaque bouton.", x, y + dp(32), ts * 0.8f, TEXT2, false);
        text(c, "Touche le nom d'une piste (page MIXEUR) pour la renommer.", x, y + dp(54), ts * 0.8f, TEXT2, false);
    }

    // ---------------------------------------------------------------- toucher

    private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        int a = e.getActionMasked();
        int i = e.getActionIndex();
        switch (a) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                down(e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int k = 0; k < e.getPointerCount(); k++) move(e.getPointerId(k), e.getY(k), e.getX(k));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                up(e.getPointerId(i));
                break;
            case MotionEvent.ACTION_CANCEL:
                for (int k = 0; k < MAXP; k++) up(k);
                break;
        }
        invalidate();
        return true;
    }

    private void tick() { performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); }

    private void down(int id, float x, float y) {
        if (id < 0 || id >= MAXP) return;
        holdKind[id] = H_NONE;
        // onglets
        if (y < top) {
            for (int t = 0; t < 3; t++) {
                if (in(x, y, tabX(t), 0, tabW(), top)) { st.page = t; tick(); st.changed = true; }
            }
            return;
        }
        if (st.page == PAGE_MIX) {
            for (int s = 0; s < 9; s++) {
                float sx = stripX(s), w = stripW() - dp(6);
                if (x < sx || x > sx + w) continue;
                if (y < btnTop() - dp(4) && s < 8) { host.rename(s); return; }
                if (s < 8 && y >= btnTop() && y <= btnTop() + btnH()) {
                    float bw = (w - dp(16)) / 3f;
                    int b = (int) ((x - sx - dp(4)) / (bw + dp(4)));
                    b = Math.max(0, Math.min(2, b));
                    holdKind[id] = H_BTN;
                    holdIdx[id] = s * 3 + b;
                    st.pressTrackButton(s, b, true);
                    tick();
                    return;
                }
                if (y >= fadTop() - dp(10)) {
                    holdKind[id] = H_FADER;
                    holdIdx[id] = s;
                    holdY[id] = y;
                    holdV[id] = s == 8 ? st.master : st.vol[s];
                }
                return;
            }
        } else if (st.page == PAGE_LIVE) {
            float y0 = top + dp(12), h = H - y0 - dp(12), lw = W * 0.58f;
            float gy = y0 + dp(24), gh = h - dp(24);
            float bw = (lw - dp(14) - dp(30)) / 4f, bh = (gh - dp(10)) / 2f;
            for (int k = 0; k < 8; k++) {
                float bx = dp(14) + (k % 4) * (bw + dp(10)), by = gy + (k / 4) * (bh + dp(10));
                if (in(x, y, bx, by, bw, bh)) { st.choosePreset(k); tick(); return; }
            }
            float rx = lw + dp(10), rw = W - rx - dp(14), rh = (h - dp(30)) / 4f;
            if (in(x, y, rx, y0, rw, rh)) { st.toggleLeslie(); tick(); return; }
            if (in(x, y, rx, y0 + rh + dp(10), rw, rh)) {
                holdKind[id] = H_BTN; holdIdx[id] = -1; st.setSustain(true); tick(); return;
            }
            if (in(x, y, rx, y0 + 2 * (rh + dp(10)), rw, rh)) {
                holdKind[id] = H_BTN; holdIdx[id] = -2; st.panic(); tick(); return;
            }
            float fy = y0 + 3 * (rh + dp(10));
            if (in(x, y, rx, fy, rw, rh)) {
                holdKind[id] = H_HFADER; holdY[id] = x; holdV[id] = st.reverb;
            }
        } else {
            float sx = dp(18), sy = top + dp(12), rh = setRowH(), w = W - 2 * sx;
            if (in(x, y, sx + w - dp(150), sy + rh * 0.3f, dp(136), rh * 0.75f)) { host.rescanUsb(); tick(); return; }
            float cy = sy + rh * 1.6f + dp(10) + rh * 2.1f + dp(10);
            float cw = (w - dp(20)) / 3f;
            if (y >= cy && y <= cy + rh) {
                if (x >= sx && x <= sx + cw / 2) { st.channel = (st.channel + 15) % 16; st.changed = true; tick(); }
                else if (x > sx + cw / 2 && x <= sx + cw) { st.channel = (st.channel + 1) % 16; st.changed = true; tick(); }
                else if (x >= sx + cw + dp(10) && x <= sx + 2 * cw + dp(10)) { st.momentary = !st.momentary; st.changed = true; tick(); }
                else if (x >= sx + 2 * (cw + dp(10))) { st.sendAll(); tick(); }
            }
        }
    }

    private void move(int id, float y, float x) {
        if (id < 0 || id >= MAXP) return;
        if (holdKind[id] == H_FADER) {
            float span = fadBot() - fadTop();
            float v = holdV[id] + (holdY[id] - y) / span;
            v = Math.max(0, Math.min(1, v));
            st.setFader(holdIdx[id], v);
        } else if (holdKind[id] == H_HFADER) {
            float span = W * 0.35f;
            float v = holdV[id] + (x - holdY[id]) / span;
            st.setReverb(Math.max(0, Math.min(1, v)));
        }
    }

    private void up(int id) {
        if (id < 0 || id >= MAXP) return;
        if (holdKind[id] == H_BTN) {
            int k = holdIdx[id];
            if (k >= 0) st.pressTrackButton(k / 3, k % 3, false);
            else if (k == -1) st.setSustain(false);
            else if (k == -2) st.panicFlash = false;
        }
        holdKind[id] = H_NONE;
    }
}
