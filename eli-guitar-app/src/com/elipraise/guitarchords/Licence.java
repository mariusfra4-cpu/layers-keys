package com.elipraise.guitarchords;

import android.content.Context;
import android.content.SharedPreferences;
import android.provider.Settings;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TimeZone;

/**
 * Licence contrôlée à distance.
 *
 * Chaque téléphone a son code (ELI-XXXX-XXXX-XXXX-XXXX), calculé à partir de son identifiant Android.
 * L'appli lit une liste publique (fichier texte sur GitHub, modifiable seulement par le propriétaire) :
 *
 *   ELI-AB12-CD34-EF56-GH78                 téléphone activé (sans limite)
 *   ELI-AB12-CD34-EF56-GH78  2027-06-30     téléphone activé jusqu'au 30 juin 2027 inclus
 *   bloque ELI-AB12-CD34-EF56-GH78          téléphone bloqué
 *   anciens-codes 2027-01-31                anciens codes d'accès (ELI-XXXX-XXXX) acceptés jusqu'au 31 janvier 2027
 *   bloque-ancien ELI-XXXX-XXXX             un ancien code bloqué
 *   tout-bloquer                            l'appli est suspendue pour tout le monde
 *   message Texte…                          message affiché à tous sur l'écran de licence
 *   # …                                     commentaire
 *
 * Sans Internet, l'appli reste ouverte 7 jours après la dernière vérification réussie.
 */
final class Licence {
    static final String LIST_URL = "https://raw.githubusercontent.com/mariusfra4-cpu/eli-licences/main/licences.txt";
    static final long GRACE_MS = 7L * 24 * 3600 * 1000;
    static final long RECHECK_MS = 3L * 3600 * 1000;
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

    static String norm(String s) {
        return s == null ? "" : s.trim().toUpperCase(Locale.ROOT).replaceAll("[\\s]", "");
    }

    static byte[] sha256(String s) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(s.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    static String hex(byte[] b) {
        StringBuilder s = new StringBuilder();
        for (byte x : b) s.append(String.format(Locale.ROOT, "%02x", x & 0xff));
        return s.toString();
    }

    /** Empreinte d'un ancien code d'accès, calculée comme dans la page (sha256(SALT + code), 16 premiers caractères). */
    static String oldHash(String salt, String code) {
        return hex(sha256(salt + norm(code))).substring(0, 16);
    }

    // ---------------------------------------------------------------- liste distante

    static final class Liste {
        boolean toutBloquer;
        final Map<String, String> actifs = new HashMap<>(); // code -> date de fin ("" = sans limite)
        final Set<String> bloques = new HashSet<>();
        final Set<String> anciensBloques = new HashSet<>(); // codes en clair, normalisés
        String anciensJusqua; // null = anciens codes refusés
        String message = "";
    }

    static Liste parse(String text) {
        Liste l = new Liste();
        for (String raw : text.split("\n")) {
            String line = raw;
            int h = line.indexOf('#');
            if (h >= 0) line = line.substring(0, h);
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] w = line.split("\\s+");
            String k = w[0].toLowerCase(Locale.ROOT);
            if (k.equals("message")) { l.message = raw.trim().substring(7).trim(); continue; }
            if (k.equals("tout-bloquer")) { l.toutBloquer = true; continue; }
            if (k.equals("anciens-codes")) { l.anciensJusqua = w.length > 1 ? date(w[1]) : null; continue; }
            if (k.equals("bloque") && w.length > 1) { l.bloques.add(norm(w[1])); continue; }
            if (k.equals("bloque-ancien") && w.length > 1) { l.anciensBloques.add(norm(w[1])); continue; }
            if (w[0].toUpperCase(Locale.ROOT).startsWith("ELI-")) {
                String d = w.length > 1 ? date(w[1]) : "";
                l.actifs.put(norm(w[0]), d == null ? "0000-00-00" : d);
            }
        }
        return l;
    }

    /** date AAAA-MM-JJ (ou JJ/MM/AAAA) -> AAAA-MM-JJ ; null si illisible */
    static String date(String s) {
        s = s.trim();
        if (s.matches("\\d{4}-\\d{2}-\\d{2}")) return s;
        if (s.matches("\\d{2}/\\d{2}/\\d{4}")) return s.substring(6) + "-" + s.substring(3, 5) + "-" + s.substring(0, 2);
        return null;
    }

    static String today(long now) {
        SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd", Locale.ROOT);
        f.setTimeZone(TimeZone.getDefault());
        return f.format(new Date(now));
    }

    /** Verdict pour ce téléphone : null = autorisé, sinon la raison du refus. */
    static String verdict(Liste l, String code, String oldCode, String salt, String today) {
        if (l.toutBloquer) return "L’application est momentanément suspendue.";
        String c = norm(code);
        if (l.bloques.contains(c)) return "La licence de ce téléphone a été désactivée.";
        String fin = l.actifs.get(c);
        if (fin != null) {
            if (fin.isEmpty() || today.compareTo(fin) <= 0) return null;
            return "La licence de ce téléphone a expiré (" + fr(fin) + ").";
        }
        if (oldCode != null && !oldCode.isEmpty()) {
            if (l.anciensBloques.contains(norm(oldCode))) return "Ce code d’accès a été désactivé.";
            if (l.anciensJusqua != null && today.compareTo(l.anciensJusqua) <= 0) return null;
            return "Les anciens codes d’accès ne sont plus acceptés : envoie le code de ce téléphone pour l’activer.";
        }
        return "Ce téléphone n’est pas encore activé.";
    }

    static String fr(String d) {
        return d.length() == 10 ? d.substring(8) + "/" + d.substring(5, 7) + "/" + d.substring(0, 4) : d;
    }

    // ---------------------------------------------------------------- état mémorisé

    /** L'appli peut-elle s'ouvrir maintenant (dernière vérification réussie depuis moins de 7 jours) ? */
    static boolean isOpen(Context c, long now) {
        SharedPreferences p = prefs(c);
        long ok = p.getLong("ok", 0), seen = p.getLong("seen", 0);
        if (ok <= 0) return false;
        if (now + 3600_000L < seen) return false;          // horloge remise en arrière
        return now - ok < GRACE_MS;
    }

    static long lastOk(Context c) { return prefs(c).getLong("ok", 0); }
    static long lastCheck(Context c) { return prefs(c).getLong("check", 0); }
    static String reason(Context c) { return prefs(c).getString("why", ""); }
    static String message(Context c) { return prefs(c).getString("msg", ""); }
    static String oldCode(Context c) { return prefs(c).getString("old", ""); }
    static void setOldCode(Context c, String code) { prefs(c).edit().putString("old", norm(code)).apply(); }

    static void touch(Context c, long now) {
        SharedPreferences p = prefs(c);
        if (now > p.getLong("seen", 0)) p.edit().putLong("seen", now).apply();
    }

    /** Résultat d'une vérification. */
    static final class Result {
        boolean reached;  // la liste a été lue
        boolean open;     // l'appli peut s'ouvrir
        String why = "";  // raison (refus, ou problème de connexion)
    }

    /** Lit la liste et met à jour l'état. À appeler hors du fil principal. */
    static Result check(Context c, String salt) {
        long now = System.currentTimeMillis();
        Result r = new Result();
        SharedPreferences p = prefs(c);
        String text = null;
        try {
            text = fetch(LIST_URL + "?t=" + (now / 60000));
        } catch (Exception e) {
            text = null;
        }
        if (text == null) {
            r.reached = false;
            r.open = isOpen(c, now);
            long ok = p.getLong("ok", 0);
            r.why = ok > 0 && r.open
                    ? "Pas de connexion : l’appli reste ouverte jusqu’au " + fr(today(ok + GRACE_MS)) + "."
                    : "Impossible de vérifier la licence : connecte-toi à Internet puis réessaie.";
            p.edit().putString("why", r.why).apply();
            return r;
        }
        Liste l = parse(text);
        String v = verdict(l, deviceCode(c), p.getString("old", ""), salt, today(now));
        r.reached = true;
        r.open = v == null;
        r.why = v == null ? "" : v;
        SharedPreferences.Editor e = p.edit()
                .putLong("check", now)
                .putLong("ok", r.open ? now : 0)
                .putString("why", r.why)
                .putString("msg", l.message);
        if (now > p.getLong("seen", 0)) e.putLong("seen", now);
        e.apply();
        return r;
    }

    private static String fetch(String u) throws Exception {
        HttpURLConnection h = (HttpURLConnection) new URL(u).openConnection();
        h.setConnectTimeout(10000);
        h.setReadTimeout(10000);
        h.setUseCaches(false);
        h.setRequestProperty("Cache-Control", "no-cache");
        try {
            if (h.getResponseCode() != 200) return null;
            InputStream in = h.getInputStream();
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] b = new byte[8192];
            int n;
            while ((n = in.read(b)) > 0 && o.size() < 2_000_000) o.write(b, 0, n);
            return new String(o.toByteArray(), StandardCharsets.UTF_8);
        } finally {
            h.disconnect();
        }
    }
}
