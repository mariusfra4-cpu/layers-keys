//go:build android

/* Colle Android : fonctions JNI (appelées par Java) + sortie audio AAudio.
   Tout le travail est fait par le moteur Go (fonctions lka*). */

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <aaudio/AAudio.h>
#include "_cgo_export.h"

#define TAG "LayersKeys"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define JF(name) Java_com_jbpraise_layerskeys_Native_##name

/* ---------------------------------------------------------------- audio */

static AAudioStream *g_stream = NULL;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_want = 0;
static int g_rate = 0;

static aaudio_data_callback_result_t data_cb(AAudioStream *s, void *user, void *data, int32_t frames) {
    lkaRender((float *)data, frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static int open_stream(void);

static void *restart_thread(void *arg) {
    usleep(300 * 1000);
    pthread_mutex_lock(&g_mu);
    if (g_want) {
        if (g_stream) { AAudioStream_close(g_stream); g_stream = NULL; }
        open_stream();
    }
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

static void error_cb(AAudioStream *s, void *user, aaudio_result_t err) {
    LOGE("erreur audio %d (%s)", err, AAudio_convertResultToText(err));
    if (err == AAUDIO_ERROR_DISCONNECTED) { /* casque débranché, changement de sortie : on rouvre */
        pthread_t t;
        if (pthread_create(&t, NULL, restart_thread, NULL) == 0) pthread_detach(t);
    }
}

static int open_stream(void) {
    AAudioStreamBuilder *b = NULL;
    aaudio_result_t r = AAudio_createStreamBuilder(&b);
    if (r != AAUDIO_OK) { LOGE("createStreamBuilder: %s", AAudio_convertResultToText(r)); return -1; }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setDataCallback(b, data_cb, NULL);
    AAudioStreamBuilder_setErrorCallback(b, error_cb, NULL);
    AAudioStream *s = NULL;
    r = AAudioStreamBuilder_openStream(b, &s);
    if (r != AAUDIO_OK) { /* certains appareils refusent le mode basse latence : on réessaie en normal */
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_NONE);
        r = AAudioStreamBuilder_openStream(b, &s);
    }
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { LOGE("openStream: %s", AAudio_convertResultToText(r)); return -2; }
    g_rate = AAudioStream_getSampleRate(s);
    int burst = AAudioStream_getFramesPerBurst(s);
    AAudioStream_setBufferSizeInFrames(s, burst * 3); /* assez pour que les sons lourds ne craquent pas */
    lkaSetRate(g_rate);
    r = AAudioStream_requestStart(s);
    if (r != AAUDIO_OK) { LOGE("requestStart: %s", AAudio_convertResultToText(r)); AAudioStream_close(s); return -3; }
    g_stream = s;
    LOGI("audio ouvert : %d Hz, rafale %d, tampon %d", g_rate, burst, AAudioStream_getBufferSizeInFrames(s));
    return 0;
}

JNIEXPORT jint JNICALL JF(audioStart)(JNIEnv *env, jclass c) {
    pthread_mutex_lock(&g_mu);
    g_want = 1;
    int r = g_stream ? 0 : open_stream();
    pthread_mutex_unlock(&g_mu);
    return r;
}

JNIEXPORT void JNICALL JF(audioStop)(JNIEnv *env, jclass c) {
    pthread_mutex_lock(&g_mu);
    g_want = 0;
    if (g_stream) { AAudioStream_requestStop(g_stream); AAudioStream_close(g_stream); g_stream = NULL; }
    pthread_mutex_unlock(&g_mu);
}

JNIEXPORT jint JNICALL JF(audioRate)(JNIEnv *env, jclass c) { return g_rate; }

/* ---------------------------------------------------------------- Java -> Go */

static char *utf(JNIEnv *env, jstring s) { /* copie C d'une chaîne Java (à libérer) */
    if (!s) return strdup("");
    const char *p = (*env)->GetStringUTFChars(env, s, NULL);
    char *r = strdup(p ? p : "");
    if (p) (*env)->ReleaseStringUTFChars(env, s, p);
    return r;
}

JNIEXPORT void JNICALL JF(init)(JNIEnv *env, jclass c, jstring dir, jstring id, jint rate) {
    char *d = utf(env, dir), *i = utf(env, id);
    lkaInit(d, i, rate);
    free(d); free(i);
}

JNIEXPORT jint JNICALL JF(frame)(JNIEnv *env, jclass c, jobject bmp) {
    AndroidBitmapInfo info;
    void *px = NULL;
    if (AndroidBitmap_getInfo(env, bmp, &info) != ANDROID_BITMAP_RESULT_SUCCESS) return -1;
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) return -1;
    if (AndroidBitmap_lockPixels(env, bmp, &px) != ANDROID_BITMAP_RESULT_SUCCESS) return -1;
    int r = lkaFrame((uint32_t *)px, info.width, info.height, info.stride / 4);
    AndroidBitmap_unlockPixels(env, bmp);
    return r;
}

JNIEXPORT jint JNICALL JF(size)(JNIEnv *env, jclass c) { return lkaSize(); }

JNIEXPORT void JNICALL JF(layout)(JNIEnv *env, jclass c, jfloatArray out) {
    float v[3] = {0, 0, 1};
    lkaLayout(v);
    (*env)->SetFloatArrayRegion(env, out, 0, 3, v);
}

JNIEXPORT void JNICALL JF(setView)(JNIEnv *env, jclass c, jint w, jint h) { lkaSetView(w, h); }
JNIEXPORT void JNICALL JF(touch)(JNIEnv *env, jclass c, jint a, jint id, jfloat x, jfloat y) { lkaTouch(a, id, x, y); }
JNIEXPORT void JNICALL JF(key)(JNIEnv *env, jclass c, jint vk, jint down) { lkaKey(vk, down); }
JNIEXPORT void JNICALL JF(chr)(JNIEnv *env, jclass c, jint cp) { lkaChar(cp); }
JNIEXPORT void JNICALL JF(back)(JNIEnv *env, jclass c) { lkaBack(); }
JNIEXPORT void JNICALL JF(stop)(JNIEnv *env, jclass c) { lkaStop(); }

JNIEXPORT void JNICALL JF(midi)(JNIEnv *env, jclass c, jbyteArray a, jint off, jint n) {
    if (n <= 0 || n > 4096) return;
    uint8_t buf[4096];
    (*env)->GetByteArrayRegion(env, a, off, n, (jbyte *)buf);
    lkaMidi(buf, n);
}

JNIEXPORT void JNICALL JF(midiStatus)(JNIEnv *env, jclass c, jint ok, jstring s) {
    char *t = utf(env, s);
    lkaMidiStatus(ok, t);
    free(t);
}

JNIEXPORT jstring JNICALL JF(nextReq)(JNIEnv *env, jclass c) {
    char *r = lkaNextReq();
    if (!r) return NULL;
    jstring j = (*env)->NewStringUTF(env, r);
    free(r);
    return j;
}

JNIEXPORT void JNICALL JF(fileResult)(JNIEnv *env, jclass c, jstring s) {
    char *t = utf(env, s);
    lkaFileResult(t);
    free(t);
}

JNIEXPORT void JNICALL JF(setClip)(JNIEnv *env, jclass c, jstring s) {
    char *t = utf(env, s);
    lkaSetClip(t);
    free(t);
}

JNIEXPORT jstring JNICALL JF(status)(JNIEnv *env, jclass c) {
    char *r = lkaStatus();
    jstring j = (*env)->NewStringUTF(env, r);
    free(r);
    return j;
}
