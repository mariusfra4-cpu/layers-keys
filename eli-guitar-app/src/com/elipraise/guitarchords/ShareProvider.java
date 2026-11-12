package com.elipraise.guitarchords;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/** Donne accès en lecture aux fichiers exportés (MIDI…) pour les partager avec une autre appli. */
public class ShareProvider extends ContentProvider {
    static final String AUTH = "com.elipraise.guitarchords.partage";

    static File dir(android.content.Context c) {
        File d = new File(c.getCacheDir(), "partage");
        d.mkdirs();
        return d;
    }

    private File file(Uri uri) throws FileNotFoundException {
        String n = uri.getLastPathSegment();
        if (n == null || n.contains("/") || n.startsWith(".")) throw new FileNotFoundException();
        File f = new File(dir(getContext()), n);
        if (!f.isFile()) throw new FileNotFoundException(n);
        return f;
    }

    @Override public boolean onCreate() { return true; }

    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(file(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override public String getType(Uri uri) { return MainActivity.mimeOf(uri.getLastPathSegment(), null); }

    @Override public Cursor query(Uri uri, String[] proj, String sel, String[] args, String sort) {
        File f;
        try { f = file(uri); } catch (FileNotFoundException e) { return null; }
        MatrixCursor c = new MatrixCursor(new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
        c.addRow(new Object[]{f.getName(), f.length()});
        return c;
    }

    @Override public Uri insert(Uri uri, ContentValues v) { return null; }
    @Override public int delete(Uri uri, String s, String[] a) { return 0; }
    @Override public int update(Uri uri, ContentValues v, String s, String[] a) { return 0; }
}
