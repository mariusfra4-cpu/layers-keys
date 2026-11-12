package com.jbpraise.layersremote;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.net.SocketException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/**
 * MIDI par Wi-Fi au format « réseau MIDI d'Apple » (RTP-MIDI / AppleMIDI, RFC 6295).
 *
 * Le Mac le comprend sans rien installer (Configuration audio et MIDI > Fenêtre > Afficher le studio MIDI >
 * Réseau : la télécommande apparaît, on clique « Se connecter »). Sous Windows : le logiciel gratuit rtpMIDI.
 *
 * La télécommande attend qu'un ordinateur l'appelle (elle est « session ») :
 *   - port de contrôle P et port de données P+1 (5004 / 5005 si libres) ;
 *   - l'ordinateur envoie une invitation (IN) sur chaque port, on répond OK ;
 *   - il synchronise les horloges (CK), on répond ;
 *   - ensuite chaque commande part dans un paquet RTP vers le port de données de l'ordinateur.
 * Pas de journal de récupération (bit J = 0) : sur un Wi-Fi local, les paquets perdus sont très rares et chaque
 * fader renvoie de toute façon sa position au mouvement suivant.
 *
 * Aucune dépendance Android : cette classe est testée sur ordinateur (test/RemoteTest.java).
 */
public final class RtpMidi {
    public interface Listener {
        void onPeers(List<String> names); // liste des ordinateurs connectés (appelée depuis un fil réseau)
    }

    static final byte[] SIG = {(byte) 0xFF, (byte) 0xFF};

    static final class Peer {
        String name;
        int ssrc;
        SocketAddress control, data;
        long lastSeen;
    }

    private final String name;
    private final int ssrc;
    private final long t0 = System.nanoTime();
    private DatagramSocket ctl, dat;
    private Thread tc, td;
    private volatile boolean running;
    private final List<Peer> peers = new ArrayList<>();
    private int seq;
    private Listener listener;

    public RtpMidi(String name) {
        this.name = name;
        this.ssrc = new Random().nextInt();
    }

    public void setListener(Listener l) { listener = l; }

    /** démarre sur le premier couple de ports libres à partir de basePort ; renvoie le port de contrôle */
    public synchronized int start(int basePort) throws SocketException {
        SocketException last = null;
        for (int p = basePort; p < basePort + 20; p += 2) {
            DatagramSocket a = null;
            try {
                a = new DatagramSocket(null);
                a.setReuseAddress(true);
                a.bind(new InetSocketAddress(p));
                DatagramSocket b = new DatagramSocket(null);
                b.setReuseAddress(true);
                try {
                    b.bind(new InetSocketAddress(p + 1));
                } catch (SocketException e) {
                    b.close();
                    throw e;
                }
                ctl = a;
                dat = b;
                break;
            } catch (SocketException e) {
                last = e;
                if (a != null) a.close();
            }
        }
        if (ctl == null) throw last != null ? last : new SocketException("aucun port libre");
        running = true;
        tc = loop(ctl, false);
        td = loop(dat, true);
        return ctl.getLocalPort();
    }

    public int port() { return ctl == null ? -1 : ctl.getLocalPort(); }

    public synchronized void stop() {
        running = false;
        List<Peer> ps;
        synchronized (peers) { ps = new ArrayList<>(peers); peers.clear(); }
        for (Peer p : ps) { // on prévient l'ordinateur
            try { sendTo(ctl, p.control, bye(p)); } catch (Exception ignored) {}
        }
        if (ctl != null) ctl.close();
        if (dat != null) dat.close();
        ctl = dat = null;
        notifyPeers();
    }

    public List<String> peerNames() {
        List<String> out = new ArrayList<>();
        synchronized (peers) { for (Peer p : peers) out.add(p.name); }
        return out;
    }

    private Thread loop(final DatagramSocket s, final boolean isData) {
        Thread t = new Thread(new Runnable() {
            @Override public void run() {
                byte[] buf = new byte[1500];
                while (running) {
                    DatagramPacket pk = new DatagramPacket(buf, buf.length);
                    try {
                        s.receive(pk);
                    } catch (Exception e) {
                        if (!running) return;
                        continue;
                    }
                    try {
                        handle(s, isData, pk.getData(), pk.getLength(), pk.getSocketAddress());
                    } catch (Exception ignored) {
                        // paquet abîmé : ignoré
                    }
                }
            }
        }, isData ? "rtpmidi-data" : "rtpmidi-ctl");
        t.setDaemon(true);
        t.start();
        return t;
    }

    // 100 µs depuis le démarrage (horloge des paquets AppleMIDI)
    long now100us() { return (System.nanoTime() - t0) / 100000L; }

    void handle(DatagramSocket s, boolean isData, byte[] b, int n, SocketAddress from) throws Exception {
        if (n < 4 || b[0] != (byte) 0xFF || b[1] != (byte) 0xFF) return; // paquets MIDI reçus : non utilisés
        String cmd = new String(b, 2, 2, StandardCharsets.US_ASCII);
        if (cmd.equals("IN") && n >= 16) {
            int token = rd32(b, 8), rssrc = rd32(b, 12);
            String rname = n > 16 ? cstr(b, 16, n) : "Ordinateur";
            Peer p = find(rssrc);
            if (p == null) {
                p = new Peer();
                p.ssrc = rssrc;
                synchronized (peers) {
                    if (peers.size() >= 4) peers.remove(0);
                    peers.add(p);
                }
            }
            p.name = rname;
            p.lastSeen = System.currentTimeMillis();
            if (isData) p.data = from; else p.control = from;
            sendTo(s, from, ok(token));
            notifyPeers();
        } else if (cmd.equals("CK") && n >= 36) {
            int rssrc = rd32(b, 4);
            int count = b[8] & 0xFF;
            Peer p = find(rssrc);
            if (p != null) p.lastSeen = System.currentTimeMillis();
            if (count == 0) { // l'ordinateur demande l'heure : on répond (compte 1)
                byte[] r = new byte[36];
                r[0] = r[1] = (byte) 0xFF;
                r[2] = 'C'; r[3] = 'K';
                wr32(r, 4, ssrc);
                r[8] = 1;
                System.arraycopy(b, 12, r, 12, 8);
                wr64(r, 20, now100us());
                sendTo(s, from, r);
            } else if (count == 1) { // c'est nous qui avions demandé : on termine (compte 2)
                byte[] r = new byte[36];
                System.arraycopy(b, 0, r, 0, 36);
                wr32(r, 4, ssrc);
                r[8] = 2;
                wr64(r, 28, now100us());
                sendTo(s, from, r);
            }
        } else if (cmd.equals("BY") && n >= 16) {
            int rssrc = rd32(b, 12);
            synchronized (peers) {
                for (int i = 0; i < peers.size(); i++) if (peers.get(i).ssrc == rssrc) { peers.remove(i); break; }
            }
            notifyPeers();
        }
        // « RS » (accusé de réception) : rien à faire
    }

    private Peer find(int rssrc) {
        synchronized (peers) {
            for (Peer p : peers) if (p.ssrc == rssrc) return p;
        }
        return null;
    }

    private void notifyPeers() {
        Listener l = listener;
        if (l != null) l.onPeers(peerNames());
    }

    byte[] ok(int token) {
        byte[] nm = name.getBytes(StandardCharsets.UTF_8);
        byte[] r = new byte[16 + nm.length + 1];
        r[0] = r[1] = (byte) 0xFF;
        r[2] = 'O'; r[3] = 'K';
        wr32(r, 4, 2); // version du protocole
        wr32(r, 8, token);
        wr32(r, 12, ssrc);
        System.arraycopy(nm, 0, r, 16, nm.length);
        return r;
    }

    private byte[] bye(Peer p) {
        byte[] r = new byte[16];
        r[0] = r[1] = (byte) 0xFF;
        r[2] = 'B'; r[3] = 'Y';
        wr32(r, 4, 2);
        wr32(r, 12, ssrc);
        return r;
    }

    /** vrai si au moins un ordinateur est connecté (port de données connu) */
    public boolean connected() {
        synchronized (peers) {
            for (Peer p : peers) if (p.data != null) return true;
        }
        return false;
    }

    /**
     * Envoie des messages MIDI complets (ex. B0 14 64, ou plusieurs à la suite) à tous les ordinateurs connectés.
     * Peut être appelé depuis n'importe quel fil.
     */
    public void send(byte[] midi) {
        DatagramSocket s = dat;
        if (s == null || midi.length == 0) return;
        byte[] pkt = packet(midi);
        List<SocketAddress> to = new ArrayList<>();
        synchronized (peers) { for (Peer p : peers) if (p.data != null) to.add(p.data); }
        for (SocketAddress a : to) {
            try { sendTo(s, a, pkt); } catch (Exception ignored) {}
        }
    }

    /** paquet RTP-MIDI (RFC 6295) : en-tête RTP + liste de commandes (délai 0 entre les commandes, pas de journal) */
    synchronized byte[] packet(byte[] midi) {
        // une commande commence par un octet d'état (>= 0x80) : on place un délai nul (0x00) devant chaque
        // commande sauf la première
        int cmds = 0;
        for (byte v : midi) if ((v & 0x80) != 0) cmds++;
        int len = midi.length + Math.max(0, cmds - 1);
        boolean big = len > 15;
        byte[] r = new byte[12 + (big ? 2 : 1) + len];
        r[0] = (byte) 0x80;
        r[1] = (byte) 0x61; // type 97
        seq = (seq + 1) & 0xFFFF;
        r[2] = (byte) (seq >> 8);
        r[3] = (byte) seq;
        wr32(r, 4, (int) now100us());
        wr32(r, 8, ssrc);
        int k = 12;
        if (big) {
            r[k++] = (byte) (0x80 | ((len >> 8) & 0x0F));
            r[k++] = (byte) len;
        } else {
            r[k++] = (byte) len;
        }
        boolean first = true;
        for (byte v : midi) {
            if ((v & 0x80) != 0) {
                if (!first) r[k++] = 0;
                first = false;
            }
            r[k++] = v;
        }
        return r;
    }

    private static void sendTo(DatagramSocket s, SocketAddress a, byte[] b) throws Exception {
        if (s == null || a == null) return;
        s.send(new DatagramPacket(b, b.length, a));
    }

    static int rd32(byte[] b, int o) {
        return ((b[o] & 0xFF) << 24) | ((b[o + 1] & 0xFF) << 16) | ((b[o + 2] & 0xFF) << 8) | (b[o + 3] & 0xFF);
    }

    static void wr32(byte[] b, int o, int v) {
        b[o] = (byte) (v >> 24); b[o + 1] = (byte) (v >> 16); b[o + 2] = (byte) (v >> 8); b[o + 3] = (byte) v;
    }

    static void wr64(byte[] b, int o, long v) {
        wr32(b, o, (int) (v >> 32));
        wr32(b, o + 4, (int) v);
    }

    static String cstr(byte[] b, int o, int n) {
        int e = o;
        while (e < n && b[e] != 0) e++;
        return new String(b, o, e - o, StandardCharsets.UTF_8);
    }

    /** adresse IP Wi-Fi de la télécommande (pour l'afficher et l'ajouter à la main sous Windows) */
    public static String localAddress() {
        try {
            java.util.Enumeration<java.net.NetworkInterface> it = java.net.NetworkInterface.getNetworkInterfaces();
            while (it != null && it.hasMoreElements()) {
                java.net.NetworkInterface ni = it.nextElement();
                if (!ni.isUp() || ni.isLoopback()) continue;
                java.util.Enumeration<InetAddress> as = ni.getInetAddresses();
                while (as.hasMoreElements()) {
                    InetAddress a = as.nextElement();
                    if (a instanceof java.net.Inet4Address && !a.isLoopbackAddress()) return a.getHostAddress();
                }
            }
        } catch (Exception ignored) {}
        return "";
    }
}
