package com.jbpraise.keygen;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import javax.crypto.Cipher;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.SecretKeySpec;

/** Cœur du générateur : signature Ed25519 (RFC 8032), mêmes clés que le générateur Windows. Java pur, sans bibliothèque. */
public final class KeyCore {
    private KeyCore() {}

    static final String MSG_PREFIX = "LayersKeys|";

    // ---------------------------------------------------------------- Ed25519
    private static final BigInteger TWO = BigInteger.valueOf(2);
    private static final BigInteger P = BigInteger.ONE.shiftLeft(255).subtract(BigInteger.valueOf(19));
    private static final BigInteger L = BigInteger.ONE.shiftLeft(252).add(new BigInteger("27742317777372353535851937790883648493"));
    private static final BigInteger D = BigInteger.valueOf(-121665).multiply(BigInteger.valueOf(121666).modInverse(P)).mod(P);
    private static final BigInteger SQRT_M1 = TWO.modPow(P.subtract(BigInteger.ONE).shiftRight(2), P);
    private static final BigInteger[] BASE = basePoint();

    private static BigInteger recoverX(BigInteger y, int sign) {
        BigInteger y2 = y.multiply(y).mod(P);
        BigInteger x2 = y2.subtract(BigInteger.ONE).multiply(D.multiply(y2).add(BigInteger.ONE).modInverse(P)).mod(P);
        BigInteger x = x2.modPow(P.add(BigInteger.valueOf(3)).shiftRight(3), P);
        if (!x.multiply(x).subtract(x2).mod(P).equals(BigInteger.ZERO)) x = x.multiply(SQRT_M1).mod(P);
        if (x.testBit(0) != (sign == 1)) x = P.subtract(x);
        return x;
    }

    private static BigInteger[] basePoint() {
        BigInteger y = BigInteger.valueOf(4).multiply(BigInteger.valueOf(5).modInverse(P)).mod(P);
        BigInteger x = recoverX(y, 0);
        return new BigInteger[] {x, y, BigInteger.ONE, x.multiply(y).mod(P)};
    }

    private static BigInteger[] add(BigInteger[] p, BigInteger[] q) {
        BigInteger a = p[1].subtract(p[0]).multiply(q[1].subtract(q[0])).mod(P);
        BigInteger b = p[1].add(p[0]).multiply(q[1].add(q[0])).mod(P);
        BigInteger c = TWO.multiply(p[3]).multiply(q[3]).multiply(D).mod(P);
        BigInteger d = TWO.multiply(p[2]).multiply(q[2]).mod(P);
        BigInteger e = b.subtract(a), f = d.subtract(c), g = d.add(c), h = b.add(a);
        return new BigInteger[] {e.multiply(f).mod(P), g.multiply(h).mod(P), f.multiply(g).mod(P), e.multiply(h).mod(P)};
    }

    private static BigInteger[] mul(BigInteger s, BigInteger[] p) {
        BigInteger[] q = {BigInteger.ZERO, BigInteger.ONE, BigInteger.ONE, BigInteger.ZERO};
        for (int i = s.bitLength() - 1; i >= 0; i--) {
            q = add(q, q);
            if (s.testBit(i)) q = add(q, p);
        }
        return q;
    }

    private static byte[] le(BigInteger v, int n) {
        byte[] out = new byte[n];
        byte[] be = v.toByteArray();
        for (int i = 0; i < be.length && i < n + 1; i++) {
            int idx = be.length - 1 - i;
            if (i < n) out[i] = be[idx];
        }
        return out;
    }

    private static BigInteger fromLe(byte[] b, int off, int n) {
        byte[] be = new byte[n + 1];
        for (int i = 0; i < n; i++) be[n - i] = b[off + i];
        return new BigInteger(be);
    }

    private static byte[] encode(BigInteger[] p) {
        BigInteger zi = p[2].modInverse(P);
        BigInteger x = p[0].multiply(zi).mod(P), y = p[1].multiply(zi).mod(P);
        byte[] out = le(y, 32);
        if (x.testBit(0)) out[31] |= (byte) 0x80;
        return out;
    }

    private static byte[] sha512(byte[]... parts) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-512");
        for (byte[] p : parts) md.update(p);
        return md.digest();
    }

    /** Signature Ed25519 déterministe (64 octets) de msg avec la graine de 32 octets. */
    public static byte[] sign(byte[] seed, byte[] msg) throws Exception {
        byte[] h = sha512(seed);
        byte[] ab = new byte[32];
        System.arraycopy(h, 0, ab, 0, 32);
        ab[0] &= (byte) 248;
        ab[31] &= 127;
        ab[31] |= 64;
        BigInteger a = fromLe(ab, 0, 32);
        byte[] prefix = new byte[32];
        System.arraycopy(h, 32, prefix, 0, 32);
        byte[] A = encode(mul(a, BASE));
        BigInteger r = fromLe(sha512(prefix, msg), 0, 64).mod(L);
        byte[] R = encode(mul(r, BASE));
        BigInteger k = fromLe(sha512(R, A, msg), 0, 64).mod(L);
        BigInteger s = r.add(k.multiply(a)).mod(L);
        byte[] sig = new byte[64];
        System.arraycopy(R, 0, sig, 0, 32);
        System.arraycopy(le(s, 32), 0, sig, 32, 32);
        return sig;
    }

    // ---------------------------------------------------------------- codes et clés (même format que le générateur Windows)
    /** Renvoie le code au format JBK-XXXX-XXXX-XXXX-XXXX, ou null s'il n'est pas valide. */
    public static String normalizeCode(String s) {
        if (s == null) return null;
        s = s.toUpperCase().replaceAll("[\\s-]", "");
        // un message WhatsApp peut contenir du texte autour : on cherche le code dedans
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("JBK([0-9A-F]{16})").matcher(s);
        if (!m.find()) return null;
        String h = m.group(1);
        return "JBK-" + h.substring(0, 4) + "-" + h.substring(4, 8) + "-" + h.substring(8, 12) + "-" + h.substring(12, 16);
    }

    private static final String B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    static String base32(byte[] d) {
        StringBuilder sb = new StringBuilder();
        int buf = 0, bits = 0;
        for (byte b : d) {
            buf = (buf << 8) | (b & 0xff);
            bits += 8;
            while (bits >= 5) {
                sb.append(B32.charAt((buf >> (bits - 5)) & 31));
                bits -= 5;
            }
            buf &= (1 << bits) - 1;
        }
        if (bits > 0) sb.append(B32.charAt((buf << (5 - bits)) & 31));
        return sb.toString();
    }

    /** Clé de licence pour un code client déjà normalisé. */
    public static String makeKey(byte[] seed, String code) throws Exception {
        String s = base32(sign(seed, (MSG_PREFIX + code).getBytes(StandardCharsets.UTF_8)));
        StringBuilder out = new StringBuilder();
        while (s.length() > 8) {
            out.append(s, 0, 8).append('-');
            s = s.substring(8);
        }
        return out.append(s).toString();
    }

    // ---------------------------------------------------------------- clé secrète protégée par mot de passe
    static final int ITER = 300000;

    static byte[] kdf(String pass, byte[] salt) throws Exception {
        String n = pass.toUpperCase().replaceAll("[\\s-]", "");
        SecretKeyFactory f = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256");
        return f.generateSecret(new PBEKeySpec(n.toCharArray(), salt, ITER, 256)).getEncoded();
    }

    /** blob = sel(16) + iv(12) + chiffré AES-256-GCM de la graine. Renvoie null si le mot de passe est faux. */
    public static byte[] unlock(byte[] blob, String pass) {
        try {
            byte[] salt = java.util.Arrays.copyOfRange(blob, 0, 16);
            byte[] iv = java.util.Arrays.copyOfRange(blob, 16, 28);
            byte[] ct = java.util.Arrays.copyOfRange(blob, 28, blob.length);
            Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
            c.init(Cipher.DECRYPT_MODE, new SecretKeySpec(kdf(pass, salt), "AES"), new GCMParameterSpec(128, iv));
            byte[] seed = c.doFinal(ct);
            return seed.length == 32 ? seed : null;
        } catch (Exception e) {
            return null;
        }
    }

    public static byte[] lock(byte[] seed, String pass, byte[] salt, byte[] iv) throws Exception {
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(kdf(pass, salt), "AES"), new GCMParameterSpec(128, iv));
        byte[] ct = c.doFinal(seed);
        byte[] out = new byte[28 + ct.length];
        System.arraycopy(salt, 0, out, 0, 16);
        System.arraycopy(iv, 0, out, 16, 12);
        System.arraycopy(ct, 0, out, 28, ct.length);
        return out;
    }
}
