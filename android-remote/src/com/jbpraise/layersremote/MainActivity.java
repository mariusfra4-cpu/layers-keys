package com.jbpraise.layersremote;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputFilter;
import android.view.View;
import android.view.WindowManager;
import android.widget.EditText;

import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * Layers Remote : télécommande MIDI 8 pistes (volume, Muet, Solo, Hold) + live, pour Layers Keys et tous les DAW.
 * Sorties : câble USB (le téléphone devient un appareil MIDI) et Wi-Fi (réseau MIDI d'Apple / rtpMIDI).
 */
public class MainActivity extends Activity implements RemoteView.Host {
    private static final String PREFS = "remote", KEY = "state";

    private RemoteState st;
    private RemoteView view;
    private UsbMidi usb;
    private RtpMidi rtp;
    private NsdManager nsd;
    private NsdManager.RegistrationListener nsdReg;
    private WifiManager.MulticastLock mlock;
    private String usbStatus = "USB : non branché";
    private volatile List<String> wifiPeers = new java.util.ArrayList<>();
    private final Handler main = new Handler(Looper.getMainLooper());

    // un seul fil d'envoi : les messages partent dans l'ordre, sans jamais bloquer l'écran
    private final LinkedBlockingQueue<byte[]> queue = new LinkedBlockingQueue<>();
    private Thread sender;
    private volatile boolean running;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON); // l'écran ne s'éteint pas en plein culte
        st = new RemoteState();
        st.load(getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY, null));
        st.panicFlash = false;
        st.sustain = false;
        st.setOut(new RemoteState.Out() {
            @Override public void send(byte[] msg) { queue.offer(msg); }
        });
        view = new RemoteView(this, st, this);
        setContentView(view);
        immersive();

        usb = new UsbMidi(this);
        usb.setListener(new UsbMidi.Listener() {
            @Override public void onUsb(String s) { usbStatus = s; view.invalidate(); }
        });
        usb.start();

        startWifi();
        running = true;
        sender = new Thread(new Runnable() {
            @Override public void run() {
                while (running) {
                    byte[] m;
                    try { m = queue.take(); } catch (InterruptedException e) { return; }
                    if (m.length == 0) continue;
                    usb.send(m);
                    if (rtp != null) rtp.send(m);
                }
            }
        }, "envoi-midi");
        sender.setDaemon(true);
        sender.start();
    }

    private void immersive() {
        if (Build.VERSION.SDK_INT >= 19) {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    @Override public void onWindowFocusChanged(boolean f) {
        super.onWindowFocusChanged(f);
        if (f) immersive();
    }

    private void startWifi() {
        new Thread(new Runnable() {
            @Override public void run() {
                try {
                    String model = Build.MODEL == null ? "" : Build.MODEL.trim();
                    RtpMidi r = new RtpMidi(model.isEmpty() ? "Layers Remote" : "Layers Remote (" + model + ")");
                    r.setListener(new RtpMidi.Listener() {
                        @Override public void onPeers(List<String> names) {
                            wifiPeers = names;
                            main.post(new Runnable() { @Override public void run() { view.invalidate(); } });
                        }
                    });
                    final int port = r.start(5004);
                    rtp = r;
                    main.post(new Runnable() { @Override public void run() { announce(port); view.invalidate(); } });
                } catch (Exception e) {
                    rtp = null;
                }
            }
        }, "wifi-midi").start();
    }

    /** annonce la télécommande sur le réseau (Bonjour) : le Mac et rtpMIDI la voient dans leur liste */
    private void announce(int port) {
        try {
            WifiManager wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm != null) {
                mlock = wm.createMulticastLock("layersremote");
                mlock.setReferenceCounted(false);
                mlock.acquire();
            }
            nsd = (NsdManager) getSystemService(Context.NSD_SERVICE);
            NsdServiceInfo info = new NsdServiceInfo();
            String model = Build.MODEL == null ? "" : Build.MODEL.trim();
            info.setServiceName(model.isEmpty() ? "Layers Remote" : "Layers Remote (" + model + ")");
            info.setServiceType("_apple-midi._udp");
            info.setPort(port);
            nsdReg = new NsdManager.RegistrationListener() {
                @Override public void onServiceRegistered(NsdServiceInfo i) {}
                @Override public void onRegistrationFailed(NsdServiceInfo i, int err) {}
                @Override public void onServiceUnregistered(NsdServiceInfo i) {}
                @Override public void onUnregistrationFailed(NsdServiceInfo i, int err) {}
            };
            nsd.registerService(info, NsdManager.PROTOCOL_DNS_SD, nsdReg);
        } catch (Exception ignored) {}
    }

    @Override protected void onPause() {
        super.onPause();
        save();
    }

    private void save() {
        SharedPreferences.Editor e = getSharedPreferences(PREFS, MODE_PRIVATE).edit();
        e.putString(KEY, st.save());
        e.apply();
        st.changed = false;
    }

    @Override protected void onDestroy() {
        running = false;
        if (sender != null) sender.interrupt();
        try { if (nsd != null && nsdReg != null) nsd.unregisterService(nsdReg); } catch (Exception ignored) {}
        if (mlock != null && mlock.isHeld()) mlock.release();
        if (rtp != null) rtp.stop();
        usb.stop();
        super.onDestroy();
    }

    // ---------------------------------------------------------------- RemoteView.Host

    @Override public void send(byte[] msg) { queue.offer(msg); }

    @Override public void rename(final int track) {
        final EditText ed = new EditText(this);
        ed.setSingleLine(true);
        ed.setFilters(new InputFilter[]{new InputFilter.LengthFilter(16)});
        ed.setText(st.names[track]);
        ed.setSelectAllOnFocus(true);
        new AlertDialog.Builder(this)
            .setTitle("Nom de la piste " + (track + 1))
            .setView(ed)
            .setPositiveButton("OK", new DialogInterface.OnClickListener() {
                @Override public void onClick(DialogInterface d, int w) {
                    String n = ed.getText().toString().replace('|', '/').trim();
                    st.names[track] = n.isEmpty() ? "Piste " + (track + 1) : n;
                    save();
                    view.invalidate();
                    immersive();
                }
            })
            .setNegativeButton("Annuler", new DialogInterface.OnClickListener() {
                @Override public void onClick(DialogInterface d, int w) { immersive(); }
            })
            .show();
    }

    @Override public String usbStatus() { return usbStatus; }

    @Override public String wifiStatus() {
        RtpMidi r = rtp;
        if (r == null) return "Wi-Fi : en attente (active le Wi-Fi, même réseau que l'ordinateur)";
        String ip = RtpMidi.localAddress();
        List<String> ps = wifiPeers;
        String who = ps.isEmpty() ? "aucun ordinateur connecté" : "connecté à : " + String.join(", ", ps);
        return "Wi-Fi : " + (ip.isEmpty() ? "" : ip + " port " + r.port() + " · ") + who;
    }

    @Override public boolean connected() {
        RtpMidi r = rtp;
        return usb.connected() || (r != null && r.connected());
    }

    @Override public void rescanUsb() { usb.rescan(); }
}
