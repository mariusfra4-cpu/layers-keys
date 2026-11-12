package com.jbpraise.layersremote;

/**
 * Ce que chaque commande de la télécommande envoie (messages MIDI standard, compris par tous les logiciels et DAW).
 *
 * Numéros choisis parmi ceux que la norme MIDI laisse libres, pour ne gêner ni les claviers ni Layers Keys
 * (0 et 32 = banque, 1 = molette, 7 = volume, 11 = expression, 64-67 = pédales… ne sont jamais utilisés) :
 *
 *   Volume piste 1-8 : CC 20-27      Muet 1-8 : CC 102-109    Solo 1-8 : CC 110-117    Hold 1-8 : CC 70-77
 *   Volume général : CC 28           Réverbération : CC 29    Leslie lent / rapide : CC 30
 *   Sustain : CC 64 (la pédale)      Presets 1-8 : Program Change 0-7
 *   Tout arrêter : CC 120 (tout son coupé) + CC 123 (toutes notes éteintes) sur les 16 canaux
 *
 * Boutons : en mode « momentané » (par défaut) un appui envoie 127 puis 0 au relâchement ; chaque appui fait
 * basculer le réglage dans Layers Keys et dans les DAW (Ableton, FL Studio, Logic…). En mode « bascule », le bouton
 * envoie 127 quand il s'allume et 0 quand il s'éteint.
 */
public final class MidiMap {
    private MidiMap() {}

    public static final int TRACKS = 8;
    public static final int VOL_BASE = 20, MUTE_BASE = 102, SOLO_BASE = 110, HOLD_BASE = 70;
    public static final int MASTER = 28, REVERB = 29, LESLIE = 30, SUSTAIN = 64;

    /** tous les CC des pistes et réglages (sans le sustain), pour vérifier qu'aucun n'est réservé */
    public static int[] allControls() {
        int[] out = new int[TRACKS * 4 + 3];
        int k = 0;
        for (int i = 0; i < TRACKS; i++) {
            out[k++] = VOL_BASE + i; out[k++] = MUTE_BASE + i; out[k++] = SOLO_BASE + i; out[k++] = HOLD_BASE + i;
        }
        out[k++] = MASTER; out[k++] = REVERB; out[k] = LESLIE;
        return out;
    }
    public static final int PRESETS = 8;

    public static int clamp7(int v) { return v < 0 ? 0 : (v > 127 ? 127 : v); }

    /** message CC (canal 0-15) */
    public static byte[] cc(int channel, int cc, int value) {
        return new byte[]{(byte) (0xB0 | (channel & 15)), (byte) (cc & 0x7F), (byte) clamp7(value)};
    }

    /** changement de programme (preset) */
    public static byte[] program(int channel, int prog) {
        return new byte[]{(byte) (0xC0 | (channel & 15)), (byte) (prog & 0x7F)};
    }

    /** « tout arrêter » : son coupé + notes éteintes + pédale relâchée, sur les 16 canaux */
    public static byte[] panic() {
        byte[] out = new byte[16 * 9];
        int k = 0;
        for (int ch = 0; ch < 16; ch++) {
            int st = 0xB0 | ch;
            out[k++] = (byte) st; out[k++] = 64; out[k++] = 0;
            out[k++] = (byte) st; out[k++] = 120; out[k++] = 0;
            out[k++] = (byte) st; out[k++] = 123; out[k++] = 0;
        }
        return out;
    }

    /** position d'un fader (0..1) → valeur MIDI 0..127 */
    public static int faderValue(double pos) {
        if (pos <= 0) return 0;
        if (pos >= 1) return 127;
        return (int) Math.round(pos * 127);
    }
}
