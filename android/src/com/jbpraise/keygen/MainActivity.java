package com.jbpraise.keygen;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import org.json.JSONArray;
import org.json.JSONObject;

/** JB Générateur de clés — Layers Keys. Outil PRIVÉ de JB Praise. */
public class MainActivity extends Activity {
    static final int BG = 0xFF0A101B, PANEL = 0xFF131D31, GOLD = 0xFFF2B705, GOLD_HI = 0xFFFFD23A, TEXT = 0xFFEEF2FA, GREY = 0xFF8D9AB2, RED = 0xFFFF6B5E, GREEN = 0xFF54D18C;

    SharedPreferences prefs;
    byte[] seed;
    String pendingCode; // code reçu par « Partager vers… »
    EditText codeEdit, nameEdit;
    TextView keyView, keyLabel, statusView;
    LinearLayout resultCard, histBox;
    String lastKey = "", lastCode = "";

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        prefs = getSharedPreferences("jbk", MODE_PRIVATE);
        readIncoming(getIntent());
        String hs = prefs.getString("seed", "");
        if (hs.length() == 64) {
            try {
                seed = fromHex(hs);
            } catch (Exception e) {
                seed = null;
            }
        }
        if (seed != null) showMain(); else showUnlock("");
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        readIncoming(i);
        if (seed != null && codeEdit != null && pendingCode != null) {
            codeEdit.setText(pendingCode);
            pendingCode = null;
        }
    }

    void readIncoming(Intent i) {
        if (i != null && Intent.ACTION_SEND.equals(i.getAction())) {
            String t = i.getStringExtra(Intent.EXTRA_TEXT);
            String c = KeyCore.normalizeCode(t);
            if (c != null) pendingCode = c;
        }
    }

    // ---------------------------------------------------------------- petits outils d'interface
    int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    TextView text(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    GradientDrawable box(int fill, int stroke, float radius) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(dp(radius));
        if (stroke != 0) g.setStroke(dp(1.5f), stroke);
        return g;
    }

    Button button(String s, boolean primary) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextSize(15);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setTextColor(primary ? 0xFF1A1300 : TEXT);
        b.setBackground(box(primary ? GOLD : 0xFF223152, primary ? GOLD_HI : 0xFF3A4C6B, 10));
        b.setMinHeight(dp(48));
        b.setPadding(dp(14), dp(8), dp(14), dp(8));
        return b;
    }

    EditText edit(String hint, boolean mono) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setHintTextColor(0xFF5E6B85);
        e.setTextColor(TEXT);
        e.setTextSize(mono ? 17 : 16);
        e.setSingleLine(true);
        if (mono) e.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        e.setBackground(box(0xFF0C121F, 0xFF3A4C6B, 10));
        e.setPadding(dp(14), dp(12), dp(14), dp(12));
        return e;
    }

    LinearLayout.LayoutParams lp(int w, int h, int top) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(w, h);
        p.topMargin = dp(top);
        return p;
    }

    LinearLayout page() {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(18), dp(26), dp(18), dp(26));
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(BG);
        sv.setFillViewport(true);
        sv.addView(col, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(sv);
        return col;
    }

    void header(LinearLayout col) {
        col.addView(text("JB Générateur de clés", 24, GOLD_HI, true));
        col.addView(text("Layers Keys · By JB Praise", 14, GREY, false), lp(-2, -2, 2));
    }

    // ---------------------------------------------------------------- écran de déverrouillage
    void showUnlock(String error) {
        LinearLayout col = page();
        header(col);
        col.addView(text("Entre ton mot de passe pour déverrouiller le générateur. Il n'est demandé qu'une fois sur ce téléphone.", 15, TEXT, false), lp(-1, -2, 28));
        final EditText pass = edit("Mot de passe (XXXX-XXXX-XXXX)", true);
        pass.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        col.addView(pass, lp(-1, -2, 18));
        final CheckBox keep = new CheckBox(this);
        keep.setText("Rester déverrouillé sur ce téléphone");
        keep.setTextColor(TEXT);
        keep.setChecked(true);
        col.addView(keep, lp(-1, -2, 12));
        final TextView err = text(error, 14, RED, false);
        col.addView(err, lp(-1, -2, 8));
        final Button go = button("DÉVERROUILLER", true);
        col.addView(go, lp(-1, -2, 12));
        col.addView(text("Ce générateur est personnel : ne le donne jamais à un client.", 13, GREY, false), lp(-1, -2, 22));
        go.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                final String p = pass.getText().toString();
                go.setEnabled(false);
                err.setTextColor(GREY);
                err.setText("Vérification…");
                new Thread(new Runnable() {
                    public void run() {
                        final byte[] s = KeyCore.unlock(fromHex(Secret.BLOB), p);
                        runOnUiThread(new Runnable() {
                            public void run() {
                                if (s == null) {
                                    go.setEnabled(true);
                                    err.setTextColor(RED);
                                    err.setText("Mot de passe incorrect.");
                                    return;
                                }
                                seed = s;
                                if (keep.isChecked()) prefs.edit().putString("seed", toHex(s)).apply();
                                showMain();
                            }
                        });
                    }
                }).start();
            }
        });
    }

    // ---------------------------------------------------------------- écran principal
    void showMain() {
        LinearLayout col = page();
        header(col);
        col.addView(text("Code du client (Windows ou Mac)", 14, GOLD, true), lp(-1, -2, 24));
        codeEdit = edit("JBK-XXXX-XXXX-XXXX-XXXX", true);
        codeEdit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        col.addView(codeEdit, lp(-1, -2, 6));
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button paste = button("COLLER", false), clear = button("EFFACER", false);
        row.addView(paste, new LinearLayout.LayoutParams(0, -2, 1));
        LinearLayout.LayoutParams cp = new LinearLayout.LayoutParams(0, -2, 1);
        cp.leftMargin = dp(10);
        row.addView(clear, cp);
        col.addView(row, lp(-1, -2, 10));
        nameEdit = edit("Nom du client (facultatif)", false);
        nameEdit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_WORDS);
        col.addView(nameEdit, lp(-1, -2, 14));
        Button gen = button("GÉNÉRER LA CLÉ", true);
        col.addView(gen, lp(-1, dp(56), 16));
        statusView = text("", 14, RED, false);
        col.addView(statusView, lp(-1, -2, 8));

        resultCard = new LinearLayout(this);
        resultCard.setOrientation(LinearLayout.VERTICAL);
        resultCard.setBackground(box(PANEL, 0xFF2B3B57, 14));
        resultCard.setPadding(dp(16), dp(14), dp(16), dp(16));
        resultCard.setVisibility(View.GONE);
        keyLabel = text("CLÉ DE LICENCE", 13, GOLD, true);
        resultCard.addView(keyLabel);
        keyView = text("", 17, TEXT, true);
        keyView.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        keyView.setTextIsSelectable(true);
        resultCard.addView(keyView, lp(-1, -2, 8));
        LinearLayout r2 = new LinearLayout(this);
        Button copy = button("COPIER LA CLÉ", true), send = button("ENVOYER AU CLIENT", false);
        r2.addView(copy, new LinearLayout.LayoutParams(0, -2, 1));
        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(0, -2, 1);
        sp.leftMargin = dp(10);
        r2.addView(send, sp);
        resultCard.addView(r2, lp(-1, -2, 14));
        col.addView(resultCard, lp(-1, -2, 14));

        col.addView(text("Historique", 16, GOLD, true), lp(-1, -2, 28));
        histBox = new LinearLayout(this);
        histBox.setOrientation(LinearLayout.VERTICAL);
        col.addView(histBox, lp(-1, -2, 8));
        Button exp = button("ENVOYER L'HISTORIQUE (CSV)", false);
        col.addView(exp, lp(-1, -2, 12));
        Button lock = button("VERROUILLER", false);
        col.addView(lock, lp(-1, -2, 10));
        col.addView(text("Mêmes clés que le générateur Windows. Aucune connexion Internet n'est utilisée.", 12, GREY, false), lp(-1, -2, 18));

        paste.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                CharSequence t = null;
                if (cm != null && cm.hasPrimaryClip() && cm.getPrimaryClip().getItemCount() > 0) t = cm.getPrimaryClip().getItemAt(0).coerceToText(MainActivity.this);
                String c = KeyCore.normalizeCode(t == null ? null : t.toString());
                if (c == null) toast("Aucun code JBK… dans le presse-papiers.");
                else {
                    codeEdit.setText(c);
                    generate();
                }
            }
        });
        clear.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                codeEdit.setText("");
                nameEdit.setText("");
                statusView.setText("");
                resultCard.setVisibility(View.GONE);
            }
        });
        gen.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                generate();
            }
        });
        copy.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                if (lastKey.length() == 0) return;
                ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                cm.setPrimaryClip(ClipData.newPlainText("clé", lastKey));
                toast("Clé copiée. Colle-la dans ton message au client.");
            }
        });
        send.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                if (lastKey.length() == 0) return;
                share("Voici votre clé de licence Layers Keys :\n\n" + lastKey + "\n\nOuvrez Layers Keys, allez dans l'écran « Autoriser le logiciel », collez la clé et validez.\nMerci pour votre confiance !\n— JB Praise");
            }
        });
        exp.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                share(csv());
            }
        });
        lock.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                prefs.edit().remove("seed").apply();
                seed = null;
                showUnlock("");
            }
        });
        refreshHistory();
        if (pendingCode != null) {
            codeEdit.setText(pendingCode);
            pendingCode = null;
            generate();
        }
    }

    void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_LONG).show();
    }

    void share(String t) {
        Intent i = new Intent(Intent.ACTION_SEND);
        i.setType("text/plain");
        i.putExtra(Intent.EXTRA_TEXT, t);
        startActivity(Intent.createChooser(i, "Envoyer avec…"));
    }

    // ---------------------------------------------------------------- génération
    void generate() {
        String code = KeyCore.normalizeCode(codeEdit.getText().toString());
        if (code == null) {
            statusView.setTextColor(RED);
            statusView.setText("Ce code n'est pas valide. Il doit ressembler à JBK-1A2B-3C4D-5E6F-7A8B.");
            resultCard.setVisibility(View.GONE);
            return;
        }
        try {
            String key = KeyCore.makeKey(seed, code);
            boolean again = findInHistory(code) != null;
            codeEdit.setText(code);
            lastKey = key;
            lastCode = code;
            keyView.setText(key);
            keyLabel.setText("CLÉ DE LICENCE · " + code);
            resultCard.setVisibility(View.VISIBLE);
            statusView.setTextColor(GREEN);
            statusView.setText(again ? "Ce code avait déjà reçu une clé : voici la même." : "Clé générée et enregistrée dans l'historique.");
            if (!again) addHistory(code, nameEdit.getText().toString().trim(), key);
            refreshHistory();
        } catch (Exception e) {
            statusView.setTextColor(RED);
            statusView.setText("Erreur : " + e);
        }
    }

    // ---------------------------------------------------------------- historique
    JSONArray hist() {
        try {
            return new JSONArray(prefs.getString("hist", "[]"));
        } catch (Exception e) {
            return new JSONArray();
        }
    }

    JSONObject findInHistory(String code) {
        JSONArray a = hist();
        for (int i = 0; i < a.length(); i++) {
            JSONObject o = a.optJSONObject(i);
            if (o != null && code.equals(o.optString("code"))) return o;
        }
        return null;
    }

    void addHistory(String code, String name, String key) {
        try {
            JSONArray a = hist();
            JSONObject o = new JSONObject();
            o.put("date", new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.FRANCE).format(new Date()));
            o.put("name", name);
            o.put("code", code);
            o.put("key", key);
            JSONArray n = new JSONArray();
            n.put(o);
            for (int i = 0; i < a.length(); i++) n.put(a.get(i));
            prefs.edit().putString("hist", n.toString()).apply();
        } catch (Exception ignored) {
        }
    }

    void refreshHistory() {
        histBox.removeAllViews();
        final JSONArray a = hist();
        if (a.length() == 0) {
            histBox.addView(text("Aucune clé générée pour l'instant.", 14, GREY, false));
            return;
        }
        for (int i = 0; i < a.length() && i < 60; i++) {
            final JSONObject o = a.optJSONObject(i);
            if (o == null) continue;
            String nm = o.optString("name");
            TextView t = text((nm.length() > 0 ? nm + "\n" : "") + o.optString("code") + "\n" + o.optString("date"), 14, TEXT, false);
            t.setBackground(box(PANEL, 0xFF2B3B57, 10));
            t.setPadding(dp(14), dp(10), dp(14), dp(10));
            t.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    lastKey = o.optString("key");
                    lastCode = o.optString("code");
                    codeEdit.setText(lastCode);
                    nameEdit.setText(o.optString("name"));
                    keyView.setText(lastKey);
                    keyLabel.setText("CLÉ DE LICENCE · " + lastCode);
                    resultCard.setVisibility(View.VISIBLE);
                    statusView.setText("");
                }
            });
            histBox.addView(t, lp(-1, -2, 8));
        }
    }

    String csv() {
        StringBuilder sb = new StringBuilder("date;nom;code;cle\n");
        JSONArray a = hist();
        for (int i = a.length() - 1; i >= 0; i--) {
            JSONObject o = a.optJSONObject(i);
            if (o == null) continue;
            sb.append(o.optString("date")).append(';').append(o.optString("name").replace(';', ',')).append(';').append(o.optString("code")).append(';').append(o.optString("key")).append('\n');
        }
        return sb.toString();
    }

    // ---------------------------------------------------------------- hexadécimal
    static byte[] fromHex(String h) {
        byte[] b = new byte[h.length() / 2];
        for (int i = 0; i < b.length; i++) b[i] = (byte) Integer.parseInt(h.substring(2 * i, 2 * i + 2), 16);
        return b;
    }

    static String toHex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (byte x : b) sb.append(String.format("%02x", x));
        return sb.toString();
    }
}
