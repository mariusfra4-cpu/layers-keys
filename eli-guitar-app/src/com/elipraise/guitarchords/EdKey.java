package com.elipraise.guitarchords;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;

/**
 * Vérification des clés de licence (signature Ed25519, RFC 8032), en Java pur.
 * La clé est fabriquée par « Eli Clés » (générateur d'Eli Praise) ; l'application ne contient que la clé PUBLIQUE :
 * elle peut vérifier une clé, jamais en fabriquer une.
 */
final class EdKey {
    private EdKey() {}

    static final String MSG_PREFIX = "EliGuitarChords|";
    static final byte[] PUBLIC = {(byte)0x2e,(byte)0x09,(byte)0xf4,(byte)0x28,(byte)0x65,(byte)0x0d,(byte)0xb4,(byte)0xbf,(byte)0xa1,(byte)0x68,(byte)0x64,(byte)0xc9,(byte)0xec,(byte)0x3f,(byte)0xc8,(byte)0xf4,(byte)0x8f,(byte)0x4f,(byte)0x65,(byte)0xc9,(byte)0x0f,(byte)0x44,(byte)0x80,(byte)0xb8,(byte)0xb1,(byte)0xd2,(byte)0x3c,(byte)0x32,(byte)0x43,(byte)0x31,(byte)0x8b,(byte)0x30};

    private static final BigInteger TWO = BigInteger.valueOf(2);
    private static final BigInteger P = BigInteger.ONE.shiftLeft(255).subtract(BigInteger.valueOf(19));
    private static final BigInteger L = BigInteger.ONE.shiftLeft(252).add(new BigInteger("27742317777372353535851937790883648493"));
    private static final BigInteger D = BigInteger.valueOf(-121665).multiply(BigInteger.valueOf(121666).modInverse(P)).mod(P);
    private static final BigInteger SQRT_M1 = TWO.modPow(P.subtract(BigInteger.ONE).shiftRight(2), P);
    private static final BigInteger[] BASE = basePoint();

    /** x à partir de y ; null si le point n'est pas sur la courbe */
    private static BigInteger recoverX(BigInteger y, boolean odd) {
        if (y.compareTo(P) >= 0) return null;
        BigInteger y2 = y.multiply(y).mod(P);
        BigInteger x2 = y2.subtract(BigInteger.ONE).multiply(D.multiply(y2).add(BigInteger.ONE).modInverse(P)).mod(P);
        if (x2.signum() == 0) return odd ? null : BigInteger.ZERO;
        BigInteger x = x2.modPow(P.add(BigInteger.valueOf(3)).shiftRight(3), P);
        if (!x.multiply(x).subtract(x2).mod(P).equals(BigInteger.ZERO)) x = x.multiply(SQRT_M1).mod(P);
        if (!x.multiply(x).subtract(x2).mod(P).equals(BigInteger.ZERO)) return null;
        if (x.testBit(0) != odd) x = P.subtract(x);
        return x;
    }

    private static BigInteger[] basePoint() {
        BigInteger y = BigInteger.valueOf(4).multiply(BigInteger.valueOf(5).modInverse(P)).mod(P);
        BigInteger x = recoverX(y, false);
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
        for (int i = 0; i < be.length && i < n; i++) out[i] = be[be.length - 1 - i];
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

    private static BigInteger[] decode(byte[] b, int off) {
        byte[] c = new byte[32];
        System.arraycopy(b, off, c, 0, 32);
        boolean odd = (c[31] & 0x80) != 0;
        c[31] &= 0x7f;
        BigInteger y = fromLe(c, 0, 32);
        BigInteger x = recoverX(y, odd);
        if (x == null) return null;
        return new BigInteger[] {x, y, BigInteger.ONE, x.multiply(y).mod(P)};
    }

    private static byte[] sha512(byte[]... parts) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-512");
        for (byte[] p : parts) md.update(p);
        return md.digest();
    }

    /** Vérifie une signature Ed25519 (64 octets) de msg avec la clé publique pub (32 octets). */
    static boolean verify(byte[] pub, byte[] msg, byte[] sig) {
        try {
            if (pub == null || pub.length != 32 || sig == null || sig.length != 64) return false;
            BigInteger[] A = decode(pub, 0), R = decode(sig, 0);
            if (A == null || R == null) return false;
            BigInteger s = fromLe(sig, 32, 32);
            if (s.compareTo(L) >= 0) return false;
            byte[] r = new byte[32];
            System.arraycopy(sig, 0, r, 0, 32);
            BigInteger k = fromLe(sha512(r, pub, msg), 0, 64).mod(L);
            byte[] left = encode(mul(s, BASE));
            byte[] right = encode(add(R, mul(k, A)));
            return MessageDigest.isEqual(left, right);
        } catch (Exception e) {
            return false;
        }
    }

    private static final String B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    /** Clé telle que collée par le client (tirets, espaces, texte autour…) -> 64 octets, ou null. */
    static byte[] parseKey(String key) {
        if (key == null) return null;
        String k = key.toUpperCase().replaceAll("[^A-Z2-7]", "");
        // le message du générateur contient du texte : on garde la plus longue suite qui fait 103 caractères
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("([A-Z2-7]{8}-){12}[A-Z2-7]{7}").matcher(key.toUpperCase().replaceAll("\\s", ""));
        if (m.find()) k = m.group(0).replace("-", "");
        if (k.length() != 103) return null;
        byte[] out = new byte[64];
        int buf = 0, bits = 0, n = 0;
        for (int i = 0; i < k.length(); i++) {
            buf = (buf << 5) | B32.indexOf(k.charAt(i));
            bits += 5;
            if (bits >= 8) {
                if (n < 64) out[n++] = (byte) (buf >> (bits - 8));
                bits -= 8;
                buf &= (1 << bits) - 1;
            }
        }
        return n == 64 ? out : null;
    }

    /** La clé est-elle valable pour ce code de téléphone ? */
    static boolean check(String code, String key) {
        byte[] sig = parseKey(key);
        return sig != null && verify(PUBLIC, (MSG_PREFIX + code).getBytes(StandardCharsets.UTF_8), sig);
    }
}
