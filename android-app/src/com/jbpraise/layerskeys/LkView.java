package com.jbpraise.layerskeys;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/** Affiche l'image dessinée par le moteur et lui envoie les doigts et le texte tapé. */
public class LkView extends View {
    private Bitmap bmp;
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final float[] lay = new float[3];
    private boolean wantText;
    int frames;

    public LkView(Context c) {
        super(c);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setBackgroundColor(Color.BLACK);
        int sz = Native.size();
        bmp = Bitmap.createBitmap(sz >> 16, sz & 0xffff, Bitmap.Config.ARGB_8888);
    }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        Native.setView(w, h);
    }

    /** à chaque image de l'écran : récupère le nouveau dessin du moteur s'il y en a un */
    public void tick() {
        int r = Native.frame(bmp);
        if (r == 2) { // la taille de l'image a changé (piano tactile)
            int sz = Native.size();
            bmp = Bitmap.createBitmap(sz >> 16, sz & 0xffff, Bitmap.Config.ARGB_8888);
            r = Native.frame(bmp);
        }
        if (r == 1) {
            frames++;
            invalidate();
        }
    }

    @Override protected void onDraw(Canvas cv) {
        cv.drawColor(Color.BLACK);
        Native.layout(lay);
        cv.save();
        cv.translate(lay[0], lay[1]);
        cv.scale(lay[2], lay[2]);
        cv.drawBitmap(bmp, 0, 0, paint);
        cv.restore();
    }

    // ---------------------------------------------------------------- doigts

    @Override public boolean onTouchEvent(MotionEvent e) {
        int a = e.getActionMasked();
        switch (a) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int i = e.getActionIndex();
                Native.touch(0, e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++)
                    Native.touch(1, e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int i = e.getActionIndex();
                Native.touch(2, e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < e.getPointerCount(); i++)
                    Native.touch(2, e.getPointerId(i), e.getX(i), e.getY(i));
                break;
        }
        return true;
    }

    // ---------------------------------------------------------------- clavier

    public void showText(boolean on) {
        wantText = on;
        InputMethodManager im = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if (on) {
            requestFocus();
            im.restartInput(this);
            im.showSoftInput(this, 0);
        } else {
            im.hideSoftInputFromWindow(getWindowToken(), 0);
        }
    }

    @Override public boolean onCheckIsTextEditor() { return wantText; }

    @Override public InputConnection onCreateInputConnection(EditorInfo out) {
        out.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
        out.imeOptions = EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
        return new Conn(this);
    }

    private static void typeKey(int vk) { Native.key(vk, 1); Native.key(vk, 0); }

    private static void typeText(CharSequence s) {
        for (int i = 0; i < s.length(); ) {
            int cp = Character.codePointAt(s, i);
            i += Character.charCount(cp);
            if (cp == '\n') typeKey(13); else Native.chr(cp);
        }
    }

    /** Le moteur lit le texte lettre par lettre : on traduit ce que le clavier d'Android envoie. */
    private static class Conn extends BaseInputConnection {
        private String composing = "";

        Conn(View v) { super(v, false); }

        private void setComposing(String s) { // envoie seulement la différence avec le texte en cours de composition
            int k = 0;
            while (k < composing.length() && k < s.length() && composing.charAt(k) == s.charAt(k)) k++;
            for (int i = composing.length(); i > k; i--) typeKey(8);
            typeText(s.substring(k));
            composing = s;
        }

        @Override public boolean setComposingText(CharSequence t, int pos) { setComposing(t.toString()); return true; }
        @Override public boolean commitText(CharSequence t, int pos) { setComposing(t.toString()); composing = ""; return true; }
        @Override public boolean finishComposingText() { composing = ""; return true; }
        @Override public boolean deleteSurroundingText(int before, int after) {
            for (int i = 0; i < before; i++) typeKey(8);
            return true;
        }
        @Override public boolean performEditorAction(int code) { typeKey(13); return true; }
        @Override public boolean sendKeyEvent(KeyEvent e) {
            if (e.getAction() == KeyEvent.ACTION_DOWN) {
                if (e.getKeyCode() == KeyEvent.KEYCODE_DEL) typeKey(8);
                else if (e.getKeyCode() == KeyEvent.KEYCODE_ENTER) typeKey(13);
                else { int u = e.getUnicodeChar(); if (u >= 32) Native.chr(u); }
            }
            return true;
        }
    }

    /** clavier physique (USB, Bluetooth) */
    @Override public boolean onKeyDown(int code, KeyEvent e) {
        int vk = vkOf(code);
        if (vk > 0) { Native.key(vk, 1); if (vk == 32) Native.chr(32); return true; }
        int u = e.getUnicodeChar();
        if (u >= 32 && !e.isCtrlPressed()) { Native.chr(u); return true; }
        return super.onKeyDown(code, e);
    }

    @Override public boolean onKeyUp(int code, KeyEvent e) {
        int vk = vkOf(code);
        if (vk > 0) { Native.key(vk, 0); return true; }
        return super.onKeyUp(code, e);
    }

    private static int vkOf(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: return 13;
            case KeyEvent.KEYCODE_DEL: return 8;
            case KeyEvent.KEYCODE_ESCAPE: return 27;
            case KeyEvent.KEYCODE_SPACE: return 32;
            case KeyEvent.KEYCODE_DPAD_UP: return 38;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 40;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 37;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 39;
        }
        return 0;
    }
}
