package com.jbpraise.layersremote;

import android.content.Context;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.os.Handler;
import android.os.Looper;

import java.util.ArrayList;
import java.util.List;

/**
 * MIDI par câble USB : le téléphone devient un appareil MIDI pour l'ordinateur (Mac ou Windows, sans rien installer).
 * Sur le téléphone : brancher le câble, puis dans la notification « USB » choisir « MIDI ».
 * Android crée alors un port « USB » : on l'ouvre et on y écrit les messages (c'est le plus rapide, quelques ms).
 *
 * Fonctionne aussi avec tout autre appareil MIDI vu par Android (interface MIDI USB branchée au téléphone…).
 */
public final class UsbMidi {
    public interface Listener { void onUsb(String status); }

    private final MidiManager mm;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final List<MidiDevice> devices = new ArrayList<>();
    private final List<MidiInputPort> ports = new ArrayList<>();
    private final List<String> names = new ArrayList<>();
    private Listener listener;

    public UsbMidi(Context ctx) {
        mm = (MidiManager) ctx.getSystemService(Context.MIDI_SERVICE);
    }

    public boolean available() { return mm != null; }

    public void setListener(Listener l) { listener = l; }

    public void start() {
        if (mm == null) { report(); return; }
        mm.registerDeviceCallback(new MidiManager.DeviceCallback() {
            @Override public void onDeviceAdded(MidiDeviceInfo info) { open(info); }
            @Override public void onDeviceRemoved(MidiDeviceInfo info) { rescan(); }
        }, main);
        rescan();
    }

    /** ferme tout puis ouvre chaque appareil qui accepte des messages */
    public synchronized void rescan() {
        closeAll();
        if (mm == null) { report(); return; }
        for (MidiDeviceInfo info : mm.getDevices()) open(info);
        report();
    }

    private void open(final MidiDeviceInfo info) {
        if (mm == null || info.getInputPortCount() == 0) return;
        // on n'ouvre pas notre propre service ni les synthétiseurs virtuels d'autres applis (« virtual ») sauf USB / Bluetooth
        int t = info.getType();
        if (t != MidiDeviceInfo.TYPE_USB && t != MidiDeviceInfo.TYPE_BLUETOOTH) return;
        mm.openDevice(info, new MidiManager.OnDeviceOpenedListener() {
            @Override public void onDeviceOpened(MidiDevice d) {
                if (d == null) return;
                MidiInputPort p = d.openInputPort(0);
                if (p == null) { try { d.close(); } catch (Exception ignored) {} return; }
                synchronized (UsbMidi.this) {
                    devices.add(d);
                    ports.add(p);
                    String n = info.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME);
                    names.add(n == null || n.isEmpty() ? "USB" : n);
                }
                report();
            }
        }, main);
    }

    private synchronized void closeAll() {
        for (MidiInputPort p : ports) { try { p.close(); } catch (Exception ignored) {} }
        for (MidiDevice d : devices) { try { d.close(); } catch (Exception ignored) {} }
        ports.clear();
        devices.clear();
        names.clear();
    }

    public void stop() { closeAll(); }

    public synchronized boolean connected() { return !ports.isEmpty(); }

    public synchronized String status() {
        if (mm == null) return "MIDI indisponible sur ce téléphone";
        if (ports.isEmpty()) return "USB : non branché";
        return "USB : " + String.join(", ", names);
    }

    private void report() {
        final Listener l = listener;
        if (l == null) return;
        final String s = status();
        main.post(new Runnable() { @Override public void run() { l.onUsb(s); } });
    }

    /** envoie tout de suite (n'importe quel fil) */
    public void send(byte[] msg) {
        MidiInputPort[] ps;
        synchronized (this) { ps = ports.toArray(new MidiInputPort[0]); }
        for (MidiInputPort p : ps) {
            try { p.send(msg, 0, msg.length); } catch (Exception ignored) {}
        }
    }
}
