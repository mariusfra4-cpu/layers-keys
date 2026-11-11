package com.jbpraise.layerskeys;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothDevice;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;
import android.view.Choreographer;
import android.view.View;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;

public class MainActivity extends Activity {
    private static final String TAG = "LayersKeys";
    private static final int REQ_OPEN = 71, REQ_PERM = 72, REQ_TREE = 73;
    private LkView view;
    private MidiHub midi;
    private boolean running;
    private int tick;
    private String lastClip = "";
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        String id = Settings.Secure.getString(getContentResolver(), Settings.Secure.ANDROID_ID);
        if (id == null) id = "inconnu";
        Native.init(getFilesDir().getAbsolutePath(), id, 48000);
        view = new LkView(this);
        setContentView(view);
        view.requestFocus();
        midi = new MidiHub(this);
        immersive();
    }

    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override public void onWindowFocusChanged(boolean f) {
        super.onWindowFocusChanged(f);
        if (f) { immersive(); syncClip(); }
    }

    @Override protected void onResume() {
        super.onResume();
        int r = Native.audioStart();
        if (r != 0) Toast.makeText(this, "Impossible d'ouvrir la sortie audio (" + r + ")", Toast.LENGTH_LONG).show();
        running = true;
        Choreographer.getInstance().postFrameCallback(frame);
        if (midi != null) midi.scan();
    }

    @Override protected void onPause() {
        running = false;
        Native.stop(); // enregistre les réglages et coupe les notes tenues
        Native.audioStop();
        super.onPause();
    }

    private final Choreographer.FrameCallback frame = new Choreographer.FrameCallback() {
        @Override public void doFrame(long t) {
            if (!running) return;
            tick++;
            view.tick();
            pump();
            if (tick % 30 == 0) syncClip();
            if (tick % 180 == 0) Log.i(TAG, "état " + Native.status() + " images=" + view.frames);
            Choreographer.getInstance().postFrameCallback(this);
        }
    };

    @Override public void onBackPressed() { Native.back(); }

    // ---------------------------------------------------------------- demandes du moteur

    private void pump() {
        String r;
        while ((r = Native.nextReq()) != null) {
            if (r.startsWith("open:")) openFile(r.substring(5));
            else if (r.equals("ime:1")) view.showText(true);
            else if (r.equals("ime:0")) view.showText(false);
            else if (r.equals("quit")) finish();
            else if (r.equals("background")) moveTaskToBack(true);
            else if (r.equals("midiscan")) midi.scan();
            else if (r.equals("ble")) bluetooth();
            else if (r.startsWith("clip:")) setClip(r.substring(5));
            else Log.i(TAG, "demande : " + r);
        }
    }

    private void syncClip() {
        try {
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm == null || !cm.hasPrimaryClip() || cm.getPrimaryClip().getItemCount() == 0) return;
            CharSequence t = cm.getPrimaryClip().getItemAt(0).coerceToText(this);
            String s = t == null ? "" : t.toString();
            if (!s.equals(lastClip) && s.length() < 4000) { lastClip = s; Native.setClip(s); }
        } catch (Exception e) { /* presse-papiers indisponible */ }
    }

    private void setClip(String s) {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) { cm.setPrimaryClip(ClipData.newPlainText("Layers Keys", s)); lastClip = s; Native.setClip(s); }
        Toast.makeText(this, "Copié", Toast.LENGTH_SHORT).show();
    }

    // ---------------------------------------------------------------- fichiers (sons SF2 / WAV / MP3, projets)

    private void openFile(String kind) {
        if (kind.equals("folder") || kind.equals("padfolder")) { openTree(); return; }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        try {
            startActivityForResult(i, REQ_OPEN);
        } catch (Exception e) {
            Toast.makeText(this, "Impossible d'ouvrir le sélecteur de fichiers", Toast.LENGTH_LONG).show();
            Native.fileResult("");
        }
    }

    @Override protected void onActivityResult(int req, int res, final Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_TREE) { treeResult(res, data); return; }
        if (req != REQ_OPEN) return;
        if (res != RESULT_OK || data == null || data.getData() == null) { Native.fileResult(""); return; }
        final Uri uri = data.getData();
        Toast.makeText(this, "Import du fichier…", Toast.LENGTH_SHORT).show();
        new Thread(new Runnable() {
            @Override public void run() {
                String p = "";
                try { p = copyIn(uri); } catch (Exception e) { Log.e(TAG, "import", e); }
                Native.fileResult(p);
            }
        }).start();
    }

    // ---------------------------------------------------------------- dossiers (bibliothèque de sons, pads)

    private void openTree() {
        try {
            startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_TREE);
        } catch (Exception e) {
            Toast.makeText(this, "Impossible d'ouvrir le sélecteur de dossiers", Toast.LENGTH_LONG).show();
            Native.fileResult("");
        }
    }

    private void treeResult(int res, Intent data) {
        if (res != RESULT_OK || data == null || data.getData() == null) { Native.fileResult(""); return; }
        final Uri tree = data.getData();
        Toast.makeText(this, "Import du dossier… (les sons sont copiés dans l'appli)", Toast.LENGTH_LONG).show();
        new Thread(new Runnable() {
            @Override public void run() {
                String p = "";
                try {
                    String id = DocumentsContract.getTreeDocumentId(tree);
                    String name = id.contains(":") ? id.substring(id.lastIndexOf(':') + 1) : id;
                    if (name.contains("/")) name = name.substring(name.lastIndexOf('/') + 1);
                    if (name.isEmpty()) name = "Dossier";
                    File dir = new File(new File(getFilesDir(), "dossiers"), name.replaceAll("[\\\\/:*?\"<>|]", "_"));
                    dir.mkdirs();
                    copyTree(tree, id, dir, 0);
                    p = dir.getAbsolutePath();
                } catch (Exception e) { Log.e(TAG, "dossier", e); }
                Native.fileResult(p);
            }
        }).start();
    }

    private static boolean soundFile(String n) {
        String l = n.toLowerCase();
        return l.endsWith(".sf2") || l.endsWith(".sf3") || l.endsWith(".wav") || l.endsWith(".wave")
            || l.endsWith(".mp3") || l.endsWith(".ogg");
    }

    /** copie les fichiers audio du dossier (et de ses sous-dossiers, 6 niveaux au plus) dans dir */
    private void copyTree(Uri tree, String docId, File dir, int depth) throws Exception {
        if (depth > 6) return;
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        Cursor c = getContentResolver().query(kids, new String[]{
            DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE}, null, null, null);
        if (c == null) return;
        try {
            while (c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                long size = c.isNull(3) ? -1 : c.getLong(3);
                if (name == null) continue;
                String safe = name.replaceAll("[\\\\/:*?\"<>|]", "_");
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    File sub = new File(dir, safe);
                    sub.mkdirs();
                    copyTree(tree, id, sub, depth + 1);
                } else if (soundFile(name)) {
                    File f = new File(dir, safe);
                    if (f.exists() && size >= 0 && f.length() == size) continue; // déjà copié
                    InputStream in = getContentResolver().openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, id));
                    OutputStream out = new FileOutputStream(f);
                    try {
                        byte[] buf = new byte[1 << 20];
                        int n;
                        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                    } finally { out.close(); in.close(); }
                }
            }
        } finally { c.close(); }
    }

    /** copie le fichier choisi dans l'appli (le moteur lit des fichiers, pas des adresses Android) */
    private String copyIn(Uri uri) throws Exception {
        String name = "fichier";
        long size = -1;
        Cursor c = getContentResolver().query(uri, null, null, null, null);
        if (c != null) {
            try {
                if (c.moveToFirst()) {
                    int n = c.getColumnIndex(OpenableColumns.DISPLAY_NAME), s = c.getColumnIndex(OpenableColumns.SIZE);
                    if (n >= 0) name = c.getString(n);
                    if (s >= 0 && !c.isNull(s)) size = c.getLong(s);
                }
            } finally { c.close(); }
        }
        name = name.replaceAll("[\\\\/:*?\"<>|]", "_");
        File dir = new File(getFilesDir(), "imports");
        dir.mkdirs();
        File f = new File(dir, name);
        if (f.exists() && size >= 0 && f.length() == size) return f.getAbsolutePath(); // déjà importé
        int k = 1;
        while (f.exists()) { // même nom, autre contenu
            int dot = name.lastIndexOf('.');
            String nn = dot > 0 ? name.substring(0, dot) + " (" + k + ")" + name.substring(dot) : name + " (" + k + ")";
            f = new File(dir, nn);
            if (f.exists() && size >= 0 && f.length() == size) return f.getAbsolutePath();
            k++;
        }
        InputStream in = getContentResolver().openInputStream(uri);
        OutputStream out = new FileOutputStream(f);
        try {
            byte[] buf = new byte[1 << 20];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        } finally { out.close(); in.close(); }
        return f.getAbsolutePath();
    }

    // ---------------------------------------------------------------- Bluetooth MIDI

    private void bluetooth() {
        if (Build.VERSION.SDK_INT >= 31) {
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED
                    || checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT}, REQ_PERM);
                return;
            }
        } else if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, REQ_PERM);
            return;
        }
        Toast.makeText(this, "Recherche du clavier Bluetooth (8 s)… allume-le et mets-le en mode appairage", Toast.LENGTH_LONG).show();
        midi.bleScan(new MidiHub.BleListener() {
            @Override public void onFound(final List<BluetoothDevice> devs) {
                if (devs.isEmpty()) { Toast.makeText(MainActivity.this, "Aucun clavier MIDI Bluetooth trouvé", Toast.LENGTH_LONG).show(); return; }
                String[] n = new String[devs.size()];
                for (int i = 0; i < n.length; i++) {
                    try { n[i] = devs.get(i).getName() + "  " + devs.get(i).getAddress(); } catch (SecurityException e) { n[i] = devs.get(i).getAddress(); }
                }
                new AlertDialog.Builder(MainActivity.this).setTitle("Choisis ton clavier")
                        .setItems(n, new DialogInterface.OnClickListener() {
                            @Override public void onClick(DialogInterface d, int which) { midi.bleConnect(devs.get(which)); }
                        }).show();
            }
            @Override public void onFail(String why) { Toast.makeText(MainActivity.this, why, Toast.LENGTH_LONG).show(); }
        });
    }

    @Override public void onRequestPermissionsResult(int code, String[] p, int[] r) {
        if (code == REQ_PERM) {
            boolean ok = r.length > 0;
            for (int x : r) ok &= x == PackageManager.PERMISSION_GRANTED;
            if (ok) bluetooth();
            else Toast.makeText(this, "Sans ce droit, la recherche Bluetooth est impossible (le MIDI USB marche sans).", Toast.LENGTH_LONG).show();
        }
    }
}
