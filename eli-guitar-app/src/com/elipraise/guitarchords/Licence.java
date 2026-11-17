package com.elipraise.guitarchords;

import android.content.Context;
import android.content.SharedPreferences;
import android.provider.Settings;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;

/**
 * Licence d'Eli Guitar Chords, comme Layers Keys : AUCUNE connexion Internet.
 *
 *  - Chaque téléphone a son code (ELI-XXXX-XXXX-XXXX-XXXX), calculé à partir de son identifiant Android.
 *  - Le client envoie ce code à Eli Praise, qui fabrique la clé avec « Eli Clés » et la lui renvoie (WhatsApp…).
 *  - La clé est la signature (Ed25519) du code : l'appli la vérifie avec la clé publique (EdKey) ;
 *    elle ne marche que sur le téléphone dont elle porte le code. Une fois collée, l'appli est activée pour toujours.
 */
final class Licence {
    private static final String P = "licence";

    private Licence() {}

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(P, Context.MODE_PRIVATE);
    }

    // ---------------------------------------------------------------- code du téléphone

    private static final String B32 = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // sans I, O, 0, 1 (faciles à confondre)

    static String deviceCode(Context c) {
        String id = Settings.Secure.getString(c.getContentResolver(), Settings.Secure.ANDROID_ID);
        if (id == null || id.isEmpty()) {
            SharedPreferences p = prefs(c);
            id = p.getString("rid", null);
            if (id == null) {
                id = java.util.UUID.randomUUID().toString();
                p.edit().putString("rid", id).apply();
            }
        }
        byte[] h = sha256("ELI1|" + id.toLowerCase(Locale.ROOT));
        StringBuilder s = new StringBuilder("ELI");
        long bits = 0;
        int nb = 0, k = 0, out = 0;
        while (out < 16) {
            while (nb < 5) { bits = (bits << 8) | (h[k++] & 0xff); nb += 8; }
            int v = (int) ((bits >> (nb - 5)) & 31);
            nb -= 5;
            if (out % 4 == 0) s.append('-');
            s.append(B32.charAt(v));
            out++;
        }
        return s.toString();
    }

    static byte[] sha256(String s) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(s.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    // ---------------------------------------------------------------- clé de licence

    private static String keyChecked = null; // dernière clé vérifiée avec succès (la vérification prend un peu de temps)

    static String key(Context c) { return prefs(c).getString("key", ""); }

    /** La clé enregistrée est-elle valable pour ce téléphone ? */
    static synchronized boolean keyOk(Context c) {
        String k = key(c);
        if (k.isEmpty()) return false;
        if (k.equals(keyChecked)) return true;
        if (EdKey.check(deviceCode(c), k)) { keyChecked = k; return true; }
        return false;
    }

    /** Enregistre la clé si elle est valable pour ce téléphone. */
    static boolean setKey(Context c, String key) {
        if (!EdKey.check(deviceCode(c), key)) return false;
        String k = key.trim();
        synchronized (Licence.class) { keyChecked = k; }
        prefs(c).edit().putString("key", k).apply();
        return true;
    }

    /** L'appli peut-elle s'ouvrir ? (clé valable pour ce téléphone) */
    static boolean isOpen(Context c) { return keyOk(c); }
}
