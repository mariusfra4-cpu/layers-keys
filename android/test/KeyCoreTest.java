import com.jbpraise.keygen.KeyCore;
import java.util.*;

// usage : java KeyCoreTest <graine hex> <code> <clé attendue (Go)> ...
public class KeyCoreTest {
    public static void main(String[] a) throws Exception {
        byte[] seed = new byte[32];
        for (int i = 0; i < 32; i++) seed[i] = (byte) Integer.parseInt(a[0].substring(2 * i, 2 * i + 2), 16);
        int bad = 0;
        for (int i = 1; i + 1 < a.length; i += 2) {
            String code = KeyCore.normalizeCode(a[i]);
            String k = KeyCore.makeKey(seed, code);
            boolean ok = k.equals(a[i + 1]);
            if (!ok) bad++;
            System.out.println((ok ? "OK  " : "ERREUR ") + code + " " + k);
        }
        // contrôle croisé avec l'Ed25519 du JDK
        java.security.KeyFactory kf = java.security.KeyFactory.getInstance("Ed25519");
        java.security.Signature s = java.security.Signature.getInstance("Ed25519");
        s.initSign(kf.generatePrivate(new java.security.spec.EdECPrivateKeySpec(java.security.spec.NamedParameterSpec.ED25519, seed)));
        byte[] msg = "LayersKeys|JBK-1234-ABCD-0000-FFFF".getBytes();
        s.update(msg);
        boolean same = Arrays.equals(s.sign(), KeyCore.sign(seed, msg));
        System.out.println(same ? "OK  accord avec le JDK" : "ERREUR désaccord avec le JDK");
        if (!same) bad++;
        // normalisation
        String[] t = {"jbk-1a2b-3c4d-5e6f-7a8b", " JBK1A2B3C4D5E6F7A8B ", "Mon code : JBK-1A2B-3C4D-5E6F-7A8B merci", "JBK-ZZZZ"};
        for (String x : t) System.out.println("normalise [" + x + "] → " + KeyCore.normalizeCode(x));
        // mot de passe
        byte[] blob = KeyCore.lock(seed, "ABCD-EFGH-2345", new byte[16], new byte[12]);
        boolean u1 = Arrays.equals(KeyCore.unlock(blob, "abcd efgh 2345"), seed), u2 = KeyCore.unlock(blob, "ABCD-EFGH-2346") == null;
        System.out.println((u1 && u2) ? "OK  mot de passe" : "ERREUR mot de passe");
        if (!(u1 && u2)) bad++;
        System.exit(bad == 0 ? 0 : 1);
    }
}
