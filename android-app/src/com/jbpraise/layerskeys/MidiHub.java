package com.jbpraise.layerskeys;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.util.Log;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/** Claviers MIDI branchés en USB ou connectés en Bluetooth : leurs notes arrivent dans le moteur. */
public class MidiHub {
    private static final String TAG = "LKMidi";
    private final Context ctx;
    private final MidiManager mm;
    private final Handler h = new Handler(Looper.getMainLooper());
    private final Map<Integer, MidiDevice> open = new HashMap<>();
    private final Map<Integer, String> names = new HashMap<>();
    private final List<MidiOutputPort> ports = new ArrayList<>();

    public MidiHub(Context c) {
        ctx = c;
        mm = (MidiManager) c.getSystemService(Context.MIDI_SERVICE);
        if (mm == null) { Native.midiStatus(0, "MIDI : non pris en charge par ce téléphone"); return; }
        mm.registerDeviceCallback(new MidiManager.DeviceCallback() {
            @Override public void onDeviceAdded(MidiDeviceInfo info) { connect(info); }
            @Override public void onDeviceRemoved(MidiDeviceInfo info) { drop(info); }
        }, h);
        scan();
    }

    /** (re)cherche les claviers déjà branchés */
    public void scan() {
        if (mm == null) return;
        for (MidiDeviceInfo i : mm.getDevices()) connect(i);
        status();
    }

    private static String nameOf(MidiDeviceInfo info) {
        String n = info.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME);
        if (n == null) n = info.getProperties().getString(MidiDeviceInfo.PROPERTY_PRODUCT);
        return n == null ? "clavier MIDI" : n;
    }

    private void connect(final MidiDeviceInfo info) {
        if (info.getOutputPortCount() == 0 || open.containsKey(info.getId())) return; // rien à écouter
        mm.openDevice(info, new MidiManager.OnDeviceOpenedListener() {
            @Override public void onDeviceOpened(MidiDevice dev) {
                if (dev == null) { Log.w(TAG, "ouverture refusée"); return; }
                open.put(info.getId(), dev);
                names.put(info.getId(), nameOf(info));
                for (int p = 0; p < info.getOutputPortCount(); p++) {
                    MidiOutputPort op = dev.openOutputPort(p);
                    if (op == null) continue;
                    op.connect(new MidiReceiver() {
                        @Override public void onSend(byte[] msg, int off, int count, long ts) {
                            Native.midi(msg, off, count);
                        }
                    });
                    ports.add(op);
                }
                Log.i(TAG, "clavier connecté : " + nameOf(info));
                status();
            }
        }, h);
    }

    private void drop(MidiDeviceInfo info) {
        MidiDevice d = open.remove(info.getId());
        names.remove(info.getId());
        if (d != null) { try { d.close(); } catch (Exception e) { /* déjà fermé */ } }
        status();
    }

    private void status() {
        if (names.isEmpty()) Native.midiStatus(0, "MIDI : aucun clavier");
        else {
            StringBuilder b = new StringBuilder("MIDI connecté : ");
            boolean first = true;
            for (String n : names.values()) { if (!first) b.append(", "); b.append(n); first = false; }
            Native.midiStatus(1, b.toString());
        }
    }

    // ---------------------------------------------------------------- Bluetooth

    public interface BleListener { void onFound(List<BluetoothDevice> devs); void onFail(String why); }

    private ScanCallback scanCb;
    private BluetoothLeScanner scanner;
    private final List<BluetoothDevice> found = new ArrayList<>();
    private static final UUID MIDI_SVC = UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700");

    /** cherche les claviers MIDI Bluetooth à proximité (8 secondes) */
    public void bleScan(final BleListener l) {
        BluetoothManager bm = (BluetoothManager) ctx.getSystemService(Context.BLUETOOTH_SERVICE);
        BluetoothAdapter ad = bm == null ? null : bm.getAdapter();
        if (ad == null || !ad.isEnabled()) { l.onFail("Le Bluetooth est éteint : active-le puis réessaie."); return; }
        scanner = ad.getBluetoothLeScanner();
        if (scanner == null) { l.onFail("Recherche Bluetooth indisponible."); return; }
        found.clear();
        scanCb = new ScanCallback() {
            @Override public void onScanResult(int type, ScanResult r) {
                BluetoothDevice d = r.getDevice();
                for (BluetoothDevice o : found) if (o.getAddress().equals(d.getAddress())) return;
                found.add(d);
            }
            @Override public void onScanFailed(int code) { l.onFail("La recherche Bluetooth a échoué (" + code + ")."); }
        };
        List<ScanFilter> f = new ArrayList<>();
        f.add(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(MIDI_SVC)).build());
        try {
            scanner.startScan(f, new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCb);
        } catch (SecurityException e) { l.onFail("Droit Bluetooth refusé."); return; }
        h.postDelayed(new Runnable() {
            @Override public void run() {
                try { scanner.stopScan(scanCb); } catch (Exception e) { /* arrêté */ }
                l.onFound(new ArrayList<>(found));
            }
        }, 8000);
    }

    public void bleConnect(BluetoothDevice d) {
        mm.openBluetoothDevice(d, new MidiManager.OnDeviceOpenedListener() {
            @Override public void onDeviceOpened(MidiDevice dev) {
                if (dev == null) { Native.midiStatus(0, "MIDI : connexion Bluetooth refusée"); return; }
                connect(dev.getInfo());
            }
        }, h);
    }
}
