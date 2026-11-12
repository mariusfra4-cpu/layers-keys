import com.jbpraise.layersremote.MidiMap;
import com.jbpraise.layersremote.RtpMidi;

import java.lang.reflect.Method;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * Tests de la télécommande sur ordinateur (sans Android) :
 *   - messages MIDI (CC, presets, « tout arrêter », valeurs des faders) ;
 *   - le Wi-Fi « réseau MIDI d'Apple » : on joue le rôle du Mac (invitation, synchronisation d'horloge),
 *     puis on vérifie que les commandes arrivent bien dans des paquets RTP-MIDI corrects.
 * Lancer : javac -d /tmp/rt src/.../MidiMap.java src/.../RtpMidi.java test/RemoteTest.java && java -cp /tmp/rt RemoteTest
 */
public class RemoteTest {
    static int fails = 0;

    static void check(boolean ok, String what) {
        System.out.println((ok ? "OK    " : "ÉCHEC ") + what);
        if (!ok) fails++;
    }

    static byte[] in(String cmd, int token, int ssrc, String name) {
        byte[] nm = name.getBytes(StandardCharsets.UTF_8);
        byte[] b = new byte[16 + nm.length + 1];
        b[0] = b[1] = (byte) 0xFF;
        b[2] = (byte) cmd.charAt(0);
        b[3] = (byte) cmd.charAt(1);
        w32(b, 4, 2);
        w32(b, 8, token);
        w32(b, 12, ssrc);
        System.arraycopy(nm, 0, b, 16, nm.length);
        return b;
    }

    static void w32(byte[] b, int o, int v) {
        b[o] = (byte) (v >> 24); b[o + 1] = (byte) (v >> 16); b[o + 2] = (byte) (v >> 8); b[o + 3] = (byte) v;
    }

    static int r32(byte[] b, int o) {
        return ((b[o] & 0xFF) << 24) | ((b[o + 1] & 0xFF) << 16) | ((b[o + 2] & 0xFF) << 8) | (b[o + 3] & 0xFF);
    }

    static byte[] recv(DatagramSocket s) throws Exception {
        byte[] buf = new byte[1500];
        DatagramPacket p = new DatagramPacket(buf, buf.length);
        s.receive(p);
        return Arrays.copyOf(p.getData(), p.getLength());
    }

    public static void main(String[] a) throws Exception {
        // ---- messages MIDI
        check(Arrays.equals(MidiMap.cc(0, MidiMap.VOL_BASE + 2, 100), new byte[]{(byte) 0xB0, 22, 100}), "volume piste 3 = CC 22");
        check(Arrays.equals(MidiMap.cc(15, 30, 300), new byte[]{(byte) 0xBF, 30, 127}), "canal 16, valeur bornée à 127");
        check(Arrays.equals(MidiMap.program(0, 4), new byte[]{(byte) 0xC0, 4}), "preset 5 = Program Change 4");
        byte[] pn = MidiMap.panic();
        check(pn.length == 16 * 9 && (pn[0] & 0xFF) == 0xB0 && pn[4] == 120 && pn[7] == 123 && (pn[pn.length - 3] & 0xFF) == 0xBF,
            "tout arrêter : sustain relâché + CC 120 + CC 123 sur les 16 canaux");
        check(MidiMap.faderValue(0) == 0 && MidiMap.faderValue(1) == 127 && MidiMap.faderValue(0.5) == 64 && MidiMap.faderValue(-1) == 0,
            "position des faders → 0..127");
        // aucun numéro ne tombe sur un CC réservé (banque, molette, volume, expression, pédales, messages de mode)
        // ni sur un CC lié à un autre (33-63 = 2e moitié des CC 1-31 dans certains logiciels), et aucun doublon
        boolean free = true;
        java.util.Set<Integer> seen = new java.util.HashSet<>();
        for (int c : MidiMap.allControls()) {
            if (c == 0 || c == 1 || c == 7 || c == 11 || (c >= 32 && c <= 63) || (c >= 64 && c <= 69) || c >= 120) free = false;
            if (!seen.add(c)) free = false;
        }
        check(free && seen.size() == 35, "35 CC tous libres et différents");

        // ---- Wi-Fi : on joue le Mac
        RtpMidi r = new RtpMidi("Layers Remote (test)");
        final int[] peersSeen = {0};
        r.setListener(names -> peersSeen[0] = names.size());
        int port = r.start(15004);
        check(port >= 15004, "ports ouverts : " + port + " / " + (port + 1));
        InetAddress lo = InetAddress.getLoopbackAddress();
        DatagramSocket macCtl = new DatagramSocket(0, lo), macData = new DatagramSocket(0, lo);
        macCtl.setSoTimeout(2000);
        macData.setSoTimeout(2000);
        int token = 0x1234ABCD, macSSRC = 0x0BADCAFE;
        byte[] inv = in("IN", token, macSSRC, "MacBook");
        macCtl.send(new DatagramPacket(inv, inv.length, lo, port));
        byte[] ok1 = recv(macCtl);
        check(ok1[2] == 'O' && ok1[3] == 'K' && r32(ok1, 8) == token, "invitation (contrôle) acceptée, même jeton");
        String nm = new String(ok1, 16, ok1.length - 17, StandardCharsets.UTF_8);
        check(nm.equals("Layers Remote (test)"), "nom annoncé : " + nm);
        macData.send(new DatagramPacket(inv, inv.length, lo, port + 1));
        byte[] ok2 = recv(macData);
        check(ok2[2] == 'O' && ok2[3] == 'K', "invitation (données) acceptée");
        check(r.connected() && r.peerNames().contains("MacBook") && peersSeen[0] == 1, "ordinateur « MacBook » connecté");
        // synchronisation d'horloge
        byte[] ck = new byte[36];
        ck[0] = ck[1] = (byte) 0xFF; ck[2] = 'C'; ck[3] = 'K';
        w32(ck, 4, macSSRC);
        ck[8] = 0;
        w32(ck, 16, 777);
        macData.send(new DatagramPacket(ck, 36, lo, port + 1));
        byte[] ck1 = recv(macData);
        check(ck1[2] == 'C' && ck1[3] == 'K' && ck1[8] == 1 && r32(ck1, 16) == 777, "synchronisation : réponse « compte 1 » avec l'heure du Mac");
        // une commande : fader de la piste 1
        r.send(MidiMap.cc(0, MidiMap.VOL_BASE, 99));
        byte[] p = recv(macData);
        check((p[0] & 0xFF) == 0x80 && (p[1] & 0x7F) == 97, "paquet RTP version 2, type 97");
        check(p.length == 16 && p[12] == 3 && (p[13] & 0xFF) == 0xB0 && p[14] == 20 && p[15] == 99,
            "contenu MIDI : B0 14 63 (fader 1 à 99) : " + Arrays.toString(p));
        int seq1 = ((p[2] & 0xFF) << 8) | (p[3] & 0xFF);
        // plusieurs commandes d'un coup : délai nul entre chacune
        r.send(new byte[]{(byte) 0xB0, 102, 127, (byte) 0xB0, 102, 0});
        byte[] p2 = recv(macData);
        int seq2 = ((p2[2] & 0xFF) << 8) | (p2[3] & 0xFF);
        check(seq2 == ((seq1 + 1) & 0xFFFF), "numéro de séquence qui avance");
        check(p2[12] == 7 && p2[13] == (byte) 0xB0 && p2[16] == 0 && p2[17] == (byte) 0xB0 && p2[19] == 0,
            "deux commandes dans un paquet (délai 00 entre elles) : " + Arrays.toString(p2));
        // longue liste (tout arrêter) : longueur sur 12 bits
        r.send(MidiMap.panic());
        byte[] p3 = recv(macData);
        int len = ((p3[12] & 0x0F) << 8) | (p3[13] & 0xFF);
        check((p3[12] & 0x80) != 0 && len == 16 * 9 + 47 && p3.length == 14 + len, "longue liste (48 commandes) : en-tête sur 2 octets, longueur " + len);
        // l'ordinateur se déconnecte
        byte[] by = in("BY", token, macSSRC, "");
        macCtl.send(new DatagramPacket(by, 16, lo, port));
        Thread.sleep(200);
        check(!r.connected() && peersSeen[0] == 0, "déconnexion (BY) prise en compte");
        r.stop();
        check(r.port() == -1, "arrêt propre");
        // ---- boutons et faders (ce que la télécommande envoie à chaque geste)
        final java.util.List<byte[]> sent = new java.util.ArrayList<>();
        com.jbpraise.layersremote.RemoteState st = new com.jbpraise.layersremote.RemoteState();
        st.setOut(sent::add);
        st.setFader(0, 0.5f);
        st.setFader(0, 0.501f); // même valeur MIDI : rien n'est renvoyé (pas d'embouteillage)
        check(sent.size() == 1 && Arrays.equals(sent.get(0), MidiMap.cc(0, 20, 64)), "fader 1 au milieu : un seul message CC 20 = 64");
        sent.clear();
        st.setFader(8, 1f);
        check(sent.size() == 1 && sent.get(0)[1] == MidiMap.MASTER && sent.get(0)[2] == 127, "volume général : CC 28 = 127");
        sent.clear();
        st.pressTrackButton(4, 0, true);
        st.pressTrackButton(4, 0, false);
        check(st.mute[4] && sent.size() == 2 && sent.get(0)[1] == 106 && sent.get(0)[2] == 127 && sent.get(1)[2] == 0,
            "M de la piste 5 (momentané) : CC 106 = 127 puis 0, affiché allumé");
        sent.clear();
        st.pressTrackButton(4, 0, true);
        check(!st.mute[4] && sent.get(0)[2] == 127, "deuxième appui : éteint à l'écran, et encore 127 (le logiciel bascule)");
        sent.clear();
        st.momentary = false;
        st.pressTrackButton(2, 2, true);
        st.pressTrackButton(2, 2, false);
        check(st.hold[2] && sent.size() == 1 && sent.get(0)[1] == 72 && sent.get(0)[2] == 127, "H piste 3 en mode bascule : CC 72 = 127 (rien au relâchement)");
        sent.clear();
        st.pressTrackButton(2, 2, true);
        check(sent.get(0)[2] == 0, "mode bascule : éteint = 0");
        st.momentary = true;
        sent.clear();
        st.channel = 3;
        st.choosePreset(2);
        check(Arrays.equals(sent.get(0), new byte[]{(byte) 0xC3, 2}), "preset 3 sur le canal 4 : C3 02");
        sent.clear();
        st.toggleLeslie();
        check(st.leslieFast && sent.size() == 2 && sent.get(0)[1] == 30, "Leslie : CC 30 (appui + relâchement)");
        sent.clear();
        st.setSustain(true);
        st.setSustain(false);
        check(sent.size() == 2 && sent.get(0)[1] == 64 && sent.get(0)[2] == 127 && sent.get(1)[2] == 0, "sustain tenu : CC 64 127 puis 0");
        sent.clear();
        st.sendAll();
        check(sent.size() == 10, "renvoyer tout : 8 pistes + général + réverbération (" + sent.size() + ")");
        // enregistrement
        st.names[1] = "Basse";
        st.vol[6] = 0.25f;
        String saved = st.save();
        com.jbpraise.layersremote.RemoteState st2 = new com.jbpraise.layersremote.RemoteState();
        check(st2.load(saved) && st2.names[1].equals("Basse") && Math.abs(st2.vol[6] - 0.25f) < 1e-6 && st2.leslieFast && st2.channel == 3
            && st2.hold[2] == false && st2.mute[4] == false, "réglages gardés à la prochaine ouverture");
        check(!st2.load("abîmé") && !st2.load(null) && !st2.load("v1|x"), "fichier abîmé : ignoré sans planter");

        System.out.println(fails == 0 ? "\nTÉLÉCOMMANDE : TOUT VA BIEN" : "\nTÉLÉCOMMANDE : " + fails + " ÉCHEC(S)");
        System.exit(fails == 0 ? 0 : 1);
    }
}
