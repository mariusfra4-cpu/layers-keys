import com.jbpraise.keygen.KeyCore;

public class PrintKey {
    public static void main(String[] a) throws Exception {
        byte[] seed = new byte[32];
        for (int i = 0; i < 32; i++) seed[i] = (byte) i;
        System.out.println(KeyCore.makeKey(seed, KeyCore.normalizeCode(a[0])));
    }
}
