import com.jbpraise.keygen.KeyCore;
import java.nio.file.*;

/** Fabrique un Secret.java de TEST (graine 00..1f, mot de passe TEST-TEST-TEST) pour le contrôle automatique. */
public class MakeTestSecret {
    public static void main(String[] a) throws Exception {
        byte[] seed = new byte[32];
        for (int i = 0; i < 32; i++) seed[i] = (byte) i;
        byte[] blob = KeyCore.lock(seed, "TEST-TEST-TEST", new byte[16], new byte[12]);
        StringBuilder hx = new StringBuilder();
        for (byte b : blob) hx.append(String.format("%02x", b));
        Files.write(Paths.get(a[0]), ("package com.jbpraise.keygen;\nfinal class Secret {\n    private Secret() {}\n    static final String BLOB = \"" + hx + "\";\n}\n").getBytes());
    }
}
