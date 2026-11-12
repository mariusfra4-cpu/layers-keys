package com.jbpraise.layersremote;

/**
 * État de la télécommande et messages envoyés à chaque geste (sans Android : testé sur ordinateur).
 */
public final class RemoteState {
    public interface Out { void send(byte[] msg); }

    public final float[] vol = new float[MidiMap.TRACKS];
    public final boolean[] mute = new boolean[MidiMap.TRACKS], solo = new boolean[MidiMap.TRACKS], hold = new boolean[MidiMap.TRACKS];
    public final String[] names = new String[MidiMap.TRACKS];
    public float master = 0.8f, reverb = 0.15f;
    public boolean leslieFast, sustain, panicFlash;
    public int preset = -1;
    public int channel = 0;          // 0-15
    public boolean momentary = true; // boutons : 127 à l'appui puis 0 (sinon : 127 allumé / 0 éteint)
    public int page = 0;
    public boolean changed;          // à enregistrer

    private final int[] lastVol = new int[MidiMap.TRACKS + 1];
    private int lastRev = -1;
    private Out out;

    public RemoteState() {
        for (int i = 0; i < MidiMap.TRACKS; i++) {
            vol[i] = 0.8f;
            names[i] = "Piste " + (i + 1);
            lastVol[i] = -1;
        }
        lastVol[MidiMap.TRACKS] = -1;
    }

    public void setOut(Out o) { out = o; }

    private void send(byte[] m) {
        Out o = out;
        if (o != null) o.send(m);
    }

    private void cc(int n, int v) { send(MidiMap.cc(channel, n, v)); }

    /** fader de la piste i (0-7) ou du volume général (8) ; envoie seulement si la valeur MIDI change */
    public void setFader(int i, float v) {
        v = Math.max(0, Math.min(1, v));
        if (i == MidiMap.TRACKS) master = v; else vol[i] = v;
        int mv = MidiMap.faderValue(v);
        if (mv != lastVol[i]) {
            lastVol[i] = mv;
            cc(i == MidiMap.TRACKS ? MidiMap.MASTER : MidiMap.VOL_BASE + i, mv);
        }
        changed = true;
    }

    public void setReverb(float v) {
        reverb = Math.max(0, Math.min(1, v));
        int mv = MidiMap.faderValue(reverb);
        if (mv != lastRev) {
            lastRev = mv;
            cc(MidiMap.REVERB, mv);
        }
        changed = true;
    }

    /** bouton M (0), S (1) ou H (2) de la piste : appui (down) ou relâchement */
    public void pressTrackButton(int track, int b, boolean down) {
        if (track < 0 || track >= MidiMap.TRACKS || b < 0 || b > 2) return;
        boolean[] arr = b == 0 ? mute : (b == 1 ? solo : hold);
        int base = b == 0 ? MidiMap.MUTE_BASE : (b == 1 ? MidiMap.SOLO_BASE : MidiMap.HOLD_BASE);
        if (down) {
            arr[track] = !arr[track];
            changed = true;
            if (momentary) cc(base + track, 127);
            else cc(base + track, arr[track] ? 127 : 0);
        } else if (momentary) {
            cc(base + track, 0);
        }
    }

    public void toggleLeslie() {
        leslieFast = !leslieFast;
        changed = true;
        if (momentary) {
            cc(MidiMap.LESLIE, 127);
            cc(MidiMap.LESLIE, 0);
        } else {
            cc(MidiMap.LESLIE, leslieFast ? 127 : 0);
        }
    }

    public void setSustain(boolean on) {
        sustain = on;
        cc(MidiMap.SUSTAIN, on ? 127 : 0);
    }

    public void choosePreset(int k) {
        if (k < 0 || k >= MidiMap.PRESETS) return;
        preset = k;
        changed = true;
        send(MidiMap.program(channel, k));
    }

    public void panic() {
        panicFlash = true;
        sustain = false;
        send(MidiMap.panic());
    }

    /** renvoie la position de tous les faders (pour remettre un logiciel à jour d'un coup) */
    public void sendAll() {
        for (int i = 0; i <= MidiMap.TRACKS; i++) {
            lastVol[i] = -1;
            setFader(i, i == MidiMap.TRACKS ? master : vol[i]);
        }
        lastRev = -1;
        setReverb(reverb);
    }

    // ---- enregistrement (chaîne simple : pas besoin d'Android pour la tester)

    public String save() {
        StringBuilder b = new StringBuilder("v1");
        for (int i = 0; i < MidiMap.TRACKS; i++) {
            b.append('|').append(vol[i]).append('|').append(mute[i] ? 1 : 0).append(solo[i] ? 1 : 0).append(hold[i] ? 1 : 0)
                .append('|').append(names[i].replace('|', '/'));
        }
        b.append('|').append(master).append('|').append(reverb).append('|').append(channel).append('|').append(momentary ? 1 : 0)
            .append('|').append(leslieFast ? 1 : 0).append('|').append(preset).append('|').append(page);
        return b.toString();
    }

    public boolean load(String s) {
        if (s == null || !s.startsWith("v1|")) return false;
        String[] f = s.split("\\|", -1);
        if (f.length < 1 + 3 * MidiMap.TRACKS + 7) return false;
        try {
            int k = 1;
            for (int i = 0; i < MidiMap.TRACKS; i++) {
                vol[i] = clamp01(Float.parseFloat(f[k++]));
                String fl = f[k++];
                mute[i] = fl.length() > 0 && fl.charAt(0) == '1';
                solo[i] = fl.length() > 1 && fl.charAt(1) == '1';
                hold[i] = fl.length() > 2 && fl.charAt(2) == '1';
                String n = f[k++].trim();
                names[i] = n.isEmpty() ? "Piste " + (i + 1) : (n.length() > 16 ? n.substring(0, 16) : n);
            }
            master = clamp01(Float.parseFloat(f[k++]));
            reverb = clamp01(Float.parseFloat(f[k++]));
            channel = Math.max(0, Math.min(15, Integer.parseInt(f[k++])));
            momentary = !"0".equals(f[k++]);
            leslieFast = "1".equals(f[k++]);
            preset = Math.max(-1, Math.min(7, Integer.parseInt(f[k++])));
            page = Math.max(0, Math.min(2, Integer.parseInt(f[k])));
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    static float clamp01(float v) { return Float.isNaN(v) ? 0 : Math.max(0, Math.min(1, v)); }
}
