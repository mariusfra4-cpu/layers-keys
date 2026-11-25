package com.jbpraise.guitare;

import android.content.Context;
import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.ShortBuffer;

/** Décode la piste audio d'une vidéo ou d'un fichier audio en mono rééchantillonné (pour l'analyse des accords). */
final class Decoder {
    interface Progress { void set(float p); }

    static final class Result {
        byte[] bytes;
        int sampleRate;
    }

    static Result decode(Context ctx, Uri uri, int outRate, Progress progress) throws Exception {
        MediaExtractor ex = new MediaExtractor();
        MediaCodec codec = null;
        try {
            ex.setDataSource(ctx, uri, null);
            int track = -1;
            MediaFormat fmt = null;
            for (int i = 0; i < ex.getTrackCount(); i++) {
                MediaFormat f = ex.getTrackFormat(i);
                String mime = f.getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) { track = i; fmt = f; break; }
            }
            if (track < 0) throw new Exception("aucune piste audio dans ce fichier");
            ex.selectTrack(track);
            long durationUs = fmt.containsKey(MediaFormat.KEY_DURATION) ? fmt.getLong(MediaFormat.KEY_DURATION) : 0;
            int sr = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE);
            int ch = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
            int enc = AudioFormat.ENCODING_PCM_16BIT;
            codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME));
            codec.configure(fmt, null, null, 0);
            codec.start();

            Resampler rs = new Resampler(sr, outRate);
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            boolean inDone = false, outDone = false;
            int idle = 0;
            while (!outDone) {
                if (!inDone) {
                    int ii = codec.dequeueInputBuffer(5000);
                    if (ii >= 0) {
                        ByteBuffer ib = codec.getInputBuffer(ii);
                        int n = ex.readSampleData(ib, 0);
                        if (n < 0) {
                            codec.queueInputBuffer(ii, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inDone = true;
                        } else {
                            long t = ex.getSampleTime();
                            codec.queueInputBuffer(ii, 0, n, Math.max(0, t), 0);
                            if (durationUs > 0 && progress != null) progress.set(Math.min(1f, t / (float) durationUs));
                            ex.advance();
                        }
                    }
                }
                int oi = codec.dequeueOutputBuffer(info, 5000);
                if (oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    MediaFormat of = codec.getOutputFormat();
                    if (of.containsKey(MediaFormat.KEY_SAMPLE_RATE)) { sr = of.getInteger(MediaFormat.KEY_SAMPLE_RATE); rs.setInputRate(sr); }
                    if (of.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) ch = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
                    if (of.containsKey(MediaFormat.KEY_PCM_ENCODING)) enc = of.getInteger(MediaFormat.KEY_PCM_ENCODING);
                } else if (oi >= 0) {
                    idle = 0;
                    ByteBuffer ob = codec.getOutputBuffer(oi);
                    if (ob != null && info.size > 0) {
                        ob.position(info.offset);
                        ob.limit(info.offset + info.size);
                        ByteBuffer b = ob.slice().order(ByteOrder.LITTLE_ENDIAN);
                        if (enc == AudioFormat.ENCODING_PCM_FLOAT) {
                            FloatBuffer fb = b.asFloatBuffer();
                            int frames = fb.remaining() / ch;
                            for (int f = 0; f < frames; f++) {
                                float s = 0;
                                for (int c = 0; c < ch; c++) s += fb.get();
                                rs.push(s / ch);
                            }
                        } else {
                            ShortBuffer sb = b.asShortBuffer();
                            int frames = sb.remaining() / ch;
                            for (int f = 0; f < frames; f++) {
                                int s = 0;
                                for (int c = 0; c < ch; c++) s += sb.get();
                                rs.push(s / (32768f * ch));
                            }
                        }
                    }
                    codec.releaseOutputBuffer(oi, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outDone = true;
                } else if (oi == MediaCodec.INFO_TRY_AGAIN_LATER && inDone && ++idle > 400) {
                    outDone = true; // certains décodeurs ne signalent jamais la fin
                }
            }
            Result r = new Result();
            r.sampleRate = outRate;
            r.bytes = rs.toBytes();
            if (progress != null) progress.set(1f);
            return r;
        } finally {
            if (codec != null) { try { codec.stop(); } catch (Exception ignored) { } codec.release(); }
            ex.release();
        }
    }

    /** Passe-bas (2 pôles) puis décimation à taux fractionnaire. */
    static final class Resampler {
        final int outRate;
        double step, pos, a, y1, y2, prev;
        float[] out = new float[1 << 20];
        int n;

        Resampler(int inRate, int outRate) { this.outRate = outRate; setInputRate(inRate); }

        void setInputRate(int inRate) {
            step = outRate / (double) inRate;
            double fc = Math.min(outRate * 0.35, inRate * 0.45);
            a = Math.exp(-2 * Math.PI * fc / inRate);
        }

        void push(float x) {
            y1 = (1 - a) * x + a * y1;
            y2 = (1 - a) * y1 + a * y2;
            double p = pos + step;
            if (p >= 1) {
                // interpolation linéaire entre l'échantillon précédent et le courant
                double frac = (1 - pos) / step;
                add((float) (prev + (y2 - prev) * frac));
                p -= 1;
            }
            pos = p;
            prev = y2;
        }

        void add(float v) {
            if (n == out.length) {
                float[] o = new float[out.length * 2];
                System.arraycopy(out, 0, o, 0, n);
                out = o;
            }
            out[n++] = v;
        }

        byte[] toBytes() {
            ByteBuffer b = ByteBuffer.allocate(n * 4).order(ByteOrder.LITTLE_ENDIAN);
            b.asFloatBuffer().put(out, 0, n);
            return b.array();
        }
    }
}
