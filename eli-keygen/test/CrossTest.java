package com.elipraise.guitarchords;

import com.elipraise.keygen.KeyCore;

/** Le générateur (version de test) et l'appli (version de test) s'entendent : clé faite par l'un, acceptée par l'autre. */
public class CrossTest {
    static int fails = 0;

    static void ck(boolean c, String m) {
        System.out.println((c ? "OK    " : "ECHEC ") + m);
        if (!c) fails++;
    }

    static byte[] hx(String h) {
        byte[] b = new byte[h.length() / 2];
        for (int i = 0; i < b.length; i++) b[i] = (byte) Integer.parseInt(h.substring(2 * i, 2 * i + 2), 16);
        return b;
    }

    public static void main(String[] a) throws Exception {
        byte[] seed = KeyCore.unlock(hx(com.elipraise.keygen.SecretAccess.blob()), "test-test-test-test");
        ck(seed != null, "mot de passe de test accepté");
        ck(KeyCore.unlock(hx(com.elipraise.keygen.SecretAccess.blob()), "MAUVAIS-MOT-DE-PASSE") == null, "mauvais mot de passe refusé");
        ck(java.util.Arrays.equals(KeyCore.publicKey(seed), EdKey.PUBLIC), "clé publique de l'appli de test = générateur de test");
        java.util.Random r = new java.util.Random(7);
        String cs = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        int good = 0, n = 20;
        for (int i = 0; i < n; i++) {
            StringBuilder c = new StringBuilder("ELI");
            for (int j = 0; j < 16; j++) { if (j % 4 == 0) c.append('-'); c.append(cs.charAt(r.nextInt(cs.length()))); }
            String code = c.toString(), key = KeyCore.makeKey(seed, code);
            if (EdKey.check(code, key) && !EdKey.check(code.substring(0, 22) + (code.charAt(22) == 'A' ? 'B' : 'A'), key)) good++;
        }
        ck(good == n, "20 téléphones au hasard : chaque clé marche sur son téléphone et seulement sur lui");
        System.exit(fails);
    }
}
