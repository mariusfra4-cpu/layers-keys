//go:build au

/* Colle Audio Unit (AUv2) pour Mac : un instrument « aumu » qui appelle le moteur écrit en Go (fonctions lk*).
   Aucun framework C++ : on implémente directement l'interface AudioComponentPlugInInterface. */

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "_cgo_export.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdatomic.h>
#include <CoreMIDI/CoreMIDI.h>

/* Une bibliothèque Go ne peut pas être déchargée : si le logiciel hôte tente un dlclose, tout planterait.
   On demande donc au système de la garder en mémoire (RTLD_NODELETE). */
#include <dlfcn.h>
__attribute__((constructor)) static void lk_pin_library(void) {
    Dl_info info;
    if (dladdr((void *)&lk_pin_library, &info) && info.dli_fname) dlopen(info.dli_fname, RTLD_LAZY | RTLD_NODELETE);
}

/* journal de diagnostic : actif seulement si la variable d'environnement LK_TRACE est définie */
static void tr(const char *fmt, ...) {
    if (!getenv("LK_TRACE")) return;
    FILE *f = fopen("/tmp/lk_au_trace.txt", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

#define LK_EXPORT __attribute__((visibility("default")))
#define BUNDLE_ID "com.jbpraise.layerskeys.component"
#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define LK_TYPE FOURCC('a', 'u', 'm', 'u')
#define LK_SUBTYPE FOURCC('L', 'k', 'y', 's')
#define LK_MANUF FOURCC('J', 'B', 'P', 'r')

#define LK_MQ 1024        /* taille de la file MIDI (puissance de 2) */
#define LK_GRAIN 16       /* les notes sont placées au 16e d'échantillon près (0,4 ms) : assez précis, peu de découpage */
#define LK_TAIL_SEC 10.0  /* le son continue jusqu'à 10 s après la dernière note (relâchement, réverbération, delay) */

typedef struct LKAU {
    AudioComponentPlugInInterface iface; /* doit rester en premier */
    AudioComponentInstance ci;
    uintptr_t h;      /* instance Go */
    Float64 rate;
    UInt32 maxFrames;
    int initialized;
    float *sl, *sr;   /* tampons de secours si l'hôte ne fournit pas les siens */
    UInt32 sn;
    struct { AudioUnitPropertyID id; AudioUnitPropertyListenerProc proc; void *ud; } ls[32];
    int nls;
    /* file des notes MIDI reçues avant le prochain bloc de son, avec leur position dans le bloc
       (Logic envoie les notes en avance avec un décalage : on les joue à l'échantillon près) */
    struct { uint8_t st, d1, d2; UInt32 off; } mq[LK_MQ];
    _Atomic uint32_t mqw, mqr;
} LKAU;

/* ---- table instance système -> instance Go (pour la fenêtre de l'éditeur) */
#define MAXI 256
static LKAU *g_inst[MAXI];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

uintptr_t lkAUHandleFor(void *ci) {
    uintptr_t h = 0;
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < MAXI; i++)
        if (g_inst[i] && g_inst[i]->ci == (AudioComponentInstance)ci) { h = g_inst[i]->h; break; }
    pthread_mutex_unlock(&g_mu);
    return h;
}

static void reg(LKAU *a, int on) {
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < MAXI; i++) {
        if (on && !g_inst[i]) { g_inst[i] = a; break; }
        if (!on && g_inst[i] == a) { g_inst[i] = NULL; break; }
    }
    pthread_mutex_unlock(&g_mu);
}

/* ---- ouverture / fermeture */
static OSStatus au_open(void *self, AudioComponentInstance ci) {
    LKAU *a = (LKAU *)self;
    tr("open ci=%p", (void *)ci);
    a->ci = ci;
    a->h = lkNew();
    tr("open h=%lu", (unsigned long)a->h);
    if (!a->h) return kAudioUnitErr_FailedInitialization;
    a->rate = 44100.0;
    a->maxFrames = 4096;
    reg(a, 1);
    return noErr;
}

static OSStatus au_close(void *self) {
    LKAU *a = (LKAU *)self;
    tr("close h=%lu", (unsigned long)a->h);
    reg(a, 0);
    if (a->h) lkRelease(a->h);
    free(a->sl);
    free(a->sr);
    free(a);
    tr("close done");
    return noErr;
}

/* ---- initialisation */
static OSStatus au_initialize(void *self) {
    LKAU *a = (LKAU *)self;
    tr("initialize rate=%g max=%u", a->rate, (unsigned)a->maxFrames);
    lkSetup(a->h, a->rate, (int)a->maxFrames);
    a->initialized = 1;
    return noErr;
}
static OSStatus au_uninitialize(void *self) { tr("uninitialize"); ((LKAU *)self)->initialized = 0; return noErr; }

static OSStatus au_reset(void *self, AudioUnitScope scope, AudioUnitElement elem) {
    LKAU *a = (LKAU *)self;
    tr("reset");
    (void)scope; (void)elem;
    lkMidi(a->h, 0xB0, 64, 0);   /* relâche la pédale */
    lkMidi(a->h, 0xB0, 120, 0);  /* tout son coupé */
    lkMidi(a->h, 0xB0, 123, 0);  /* toutes notes éteintes */
    return noErr;
}

/* ---- propriétés */
static void fire_prop(LKAU *a, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem) {
    for (int i = 0; i < a->nls; i++)
        if (a->ls[i].id == id && a->ls[i].proc) a->ls[i].proc(a->ls[i].ud, a->ci, id, scope, elem);
}
static OSStatus prop_info(LKAU *a, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem, UInt32 *size, Boolean *w) {
    (void)a;
    UInt32 dsz = 0; Boolean dw = 0;
    if (!size) size = &dsz;
    if (!w) w = &dw;
    *w = 0;
    switch (id) {
    case kAudioUnitProperty_ClassInfo:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        *size = sizeof(CFPropertyListRef); *w = 1; return noErr;
    case kAudioUnitProperty_StreamFormat:
        if (scope != kAudioUnitScope_Output) return kAudioUnitErr_InvalidScope;
        if (elem != 0) return kAudioUnitErr_InvalidElement;
        *size = sizeof(AudioStreamBasicDescription); *w = 1; return noErr;
    case kAudioUnitProperty_ElementCount:
        *size = sizeof(UInt32); *w = 0; return noErr;
    case kAudioUnitProperty_SampleRate:
        if (scope != kAudioUnitScope_Output) return kAudioUnitErr_InvalidScope;
        *size = sizeof(Float64); *w = 1; return noErr;
    case kAudioUnitProperty_Latency:
    case kAudioUnitProperty_TailTime:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        *size = sizeof(Float64); return noErr;
    case kAudioUnitProperty_SupportedNumChannels:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        *size = sizeof(AUChannelInfo); return noErr;
    case kAudioUnitProperty_MaximumFramesPerSlice:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        *size = sizeof(UInt32); *w = 1; return noErr;
    case kAudioUnitProperty_ParameterList:
        *size = 0; return noErr;
    case kAudioUnitProperty_CocoaUI:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        *size = sizeof(AudioUnitCocoaViewInfo); return noErr;
    case kAudioUnitProperty_PresentPreset:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        *size = sizeof(AUPreset); *w = 1; return noErr;
    case kAudioUnitProperty_LastRenderError:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        *size = sizeof(OSStatus); return noErr;
    case kAudioUnitProperty_RenderQuality:
    case kAudioUnitProperty_OfflineRender:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        *size = sizeof(UInt32); *w = 1; return noErr;
    case kAudioUnitProperty_HostCallbacks:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        *size = sizeof(HostCallbackInfo); *w = 1; return noErr;
    default:
        return kAudioUnitErr_InvalidProperty;
    }
}

static OSStatus au_getpropinfo(void *self, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem, UInt32 *size, Boolean *w) {
    tr("getpropinfo %u scope=%u elem=%u", (unsigned)id, (unsigned)scope, (unsigned)elem);
    return prop_info((LKAU *)self, id, scope, elem, size, w);
}

static CFDictionaryRef make_state(LKAU *a) {
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    SInt32 v;
    CFNumberRef n;
    v = 0;         n = CFNumberCreate(NULL, kCFNumberSInt32Type, &v); CFDictionarySetValue(d, CFSTR(kAUPresetVersionKey), n); CFRelease(n);
    v = (SInt32)LK_TYPE;    n = CFNumberCreate(NULL, kCFNumberSInt32Type, &v); CFDictionarySetValue(d, CFSTR(kAUPresetTypeKey), n); CFRelease(n);
    v = (SInt32)LK_SUBTYPE; n = CFNumberCreate(NULL, kCFNumberSInt32Type, &v); CFDictionarySetValue(d, CFSTR(kAUPresetSubtypeKey), n); CFRelease(n);
    v = (SInt32)LK_MANUF;   n = CFNumberCreate(NULL, kCFNumberSInt32Type, &v); CFDictionarySetValue(d, CFSTR(kAUPresetManufacturerKey), n); CFRelease(n);
    CFDictionarySetValue(d, CFSTR(kAUPresetNameKey), CFSTR("Layers Keys"));
    int len = 0;
    tr("make_state: avant lkGetState");
    void *p = lkGetState(a->h, &len);
    tr("make_state: lkGetState len=%d p=%p", len, p);
    if (p && len > 0) {
        CFDataRef data = CFDataCreate(NULL, (const UInt8 *)p, len);
        tr("make_state: data=%p", (const void *)data);
        CFDictionarySetValue(d, CFSTR(kAUPresetDataKey), data);
        CFRelease(data);
    }
    free(p);
    tr("make_state: fini");
    return d;
}

static OSStatus au_getprop(void *self, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem, void *out, UInt32 *ioSize) {
    LKAU *a = (LKAU *)self;
    tr("getprop %u scope=%u elem=%u", (unsigned)id, (unsigned)scope, (unsigned)elem);
    UInt32 need = 0;
    Boolean w = 0;
    OSStatus st = prop_info(a, id, scope, elem, &need, &w);
    if (st != noErr) return st;
    if (id == kAudioUnitProperty_ParameterList) { *ioSize = 0; return noErr; }
    if (!out || *ioSize < need) return kAudioUnitErr_InvalidPropertyValue;
    *ioSize = need;
    switch (id) {
    case kAudioUnitProperty_ClassInfo:
        *(CFPropertyListRef *)out = make_state(a);
        return noErr;
    case kAudioUnitProperty_StreamFormat: {
        AudioStreamBasicDescription *f = (AudioStreamBasicDescription *)out;
        memset(f, 0, sizeof(*f));
        f->mSampleRate = a->rate;
        f->mFormatID = kAudioFormatLinearPCM;
        f->mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f->mBytesPerPacket = 4;
        f->mFramesPerPacket = 1;
        f->mBytesPerFrame = 4;
        f->mChannelsPerFrame = 2;
        f->mBitsPerChannel = 32;
        return noErr;
    }
    case kAudioUnitProperty_ElementCount:
        *(UInt32 *)out = (scope == kAudioUnitScope_Output || scope == kAudioUnitScope_Global) ? 1 : 0;
        return noErr;
    case kAudioUnitProperty_SampleRate:
        *(Float64 *)out = a->rate;
        return noErr;
    case kAudioUnitProperty_Latency:
        *(Float64 *)out = 0.0;
        return noErr;
    case kAudioUnitProperty_TailTime:
        *(Float64 *)out = LK_TAIL_SEC;
        return noErr;
    case kAudioUnitProperty_SupportedNumChannels: {
        AUChannelInfo *ci = (AUChannelInfo *)out;
        ci->inChannels = 0;
        ci->outChannels = 2;
        return noErr;
    }
    case kAudioUnitProperty_MaximumFramesPerSlice:
        *(UInt32 *)out = a->maxFrames;
        return noErr;
    case kAudioUnitProperty_CocoaUI: {
        lkAULog((char *)"AU : le logiciel hôte demande les informations d'interface (CocoaUI)");
        lkAUPrepareUI(a->h); /* prépare Cocoa ; la classe de la vue est dans au_view_darwin.m */
        AudioUnitCocoaViewInfo *info = (AudioUnitCocoaViewInfo *)out;
        CFURLRef url = NULL;
        /* 1) le paquet connu par son identifiant ; 2) le chemin réel de cette bibliothèque (certains hôtes, dont Logic,
              chargent le plugin sans l'enregistrer comme paquet) ; 3) l'emplacement d'installation habituel */
        CFBundleRef b = CFBundleGetBundleWithIdentifier(CFSTR(BUNDLE_ID));
        if (b) url = CFBundleCopyBundleURL(b);
        if (!url) {
            Dl_info di;
            if (dladdr((void *)&lk_pin_library, &di) && di.dli_fname) {
                char path[1024];
                strncpy(path, di.dli_fname, sizeof(path) - 1);
                path[sizeof(path) - 1] = 0;
                for (int i = 0; i < 3; i++) { /* .../X.component/Contents/MacOS/X -> .../X.component */
                    char *s = strrchr(path, '/');
                    if (!s) break;
                    *s = 0;
                }
                url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), true);
                lkAULog((char *)"AU : paquet retrouvé par le chemin de la bibliothèque");
            }
        }
        if (!url) {
            const char *p = "/Library/Audio/Plug-Ins/Components/LayersKeys.component";
            url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)p, (CFIndex)strlen(p), true);
            lkAULog((char *)"AU : paquet supposé à l'emplacement habituel");
        }
        info->mCocoaAUViewBundleLocation = url;
        info->mCocoaAUViewClass[0] = CFStringCreateCopy(NULL, CFSTR("LKAUViewFactory"));
        return noErr;
    }
    case kAudioUnitProperty_PresentPreset: {
        AUPreset *pr = (AUPreset *)out;
        pr->presetNumber = -1;
        pr->presetName = CFSTR("Layers Keys");
        return noErr;
    }
    case kAudioUnitProperty_LastRenderError:
        *(OSStatus *)out = noErr;
        return noErr;
    case kAudioUnitProperty_RenderQuality:
        *(UInt32 *)out = 127;
        return noErr;
    case kAudioUnitProperty_OfflineRender:
        *(UInt32 *)out = 0;
        return noErr;
    case kAudioUnitProperty_HostCallbacks:
        memset(out, 0, sizeof(HostCallbackInfo));
        return noErr;
    default:
        return kAudioUnitErr_InvalidProperty;
    }
}

static OSStatus au_setprop(void *self, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem, const void *in, UInt32 size) {
    LKAU *a = (LKAU *)self;
    tr("setprop %u scope=%u elem=%u size=%u", (unsigned)id, (unsigned)scope, (unsigned)elem, (unsigned)size);
    UInt32 need = 0;
    Boolean w = 0;
    OSStatus st = prop_info(a, id, scope, elem, &need, &w);
    if (st != noErr) return st;
    if (!w) return kAudioUnitErr_PropertyNotWritable;
    if (!in || size < need) return kAudioUnitErr_InvalidPropertyValue;
    switch (id) {
    case kAudioUnitProperty_ClassInfo: {
        CFPropertyListRef pl = *(CFPropertyListRef const *)in;
        if (!pl || CFGetTypeID(pl) != CFDictionaryGetTypeID()) return kAudioUnitErr_InvalidPropertyValue;
        CFTypeRef data = CFDictionaryGetValue((CFDictionaryRef)pl, CFSTR(kAUPresetDataKey));
        tr("setprop ClassInfo: data=%p", (const void *)data);
        if (data && CFGetTypeID(data) == CFDataGetTypeID() && CFDataGetLength((CFDataRef)data) > 0) {
            tr("setprop ClassInfo: lkSetState len=%ld", (long)CFDataGetLength((CFDataRef)data));
            lkSetState(a->h, (void *)CFDataGetBytePtr((CFDataRef)data), (int)CFDataGetLength((CFDataRef)data));
            tr("setprop ClassInfo: lkSetState fini");
        }
        return noErr;
    }
    case kAudioUnitProperty_StreamFormat: {
        const AudioStreamBasicDescription *f = (const AudioStreamBasicDescription *)in;
        if (f->mFormatID != kAudioFormatLinearPCM || f->mChannelsPerFrame != 2 || f->mBitsPerChannel != 32 ||
            !(f->mFormatFlags & kAudioFormatFlagIsFloat) || !(f->mFormatFlags & kAudioFormatFlagIsNonInterleaved))
            return kAudioUnitErr_FormatNotSupported;
        if (f->mSampleRate < 8000.0 || f->mSampleRate > 384000.0) return kAudioUnitErr_FormatNotSupported;
        if (a->initialized && f->mSampleRate != a->rate) return kAudioUnitErr_Initialized;
        a->rate = f->mSampleRate;
        return noErr;
    }
    case kAudioUnitProperty_SampleRate:
        if (a->initialized) return kAudioUnitErr_Initialized;
        a->rate = *(const Float64 *)in;
        return noErr;
    case kAudioUnitProperty_MaximumFramesPerSlice:
        if (a->initialized) return kAudioUnitErr_Initialized;
        a->maxFrames = *(const UInt32 *)in;
        fire_prop(a, id, scope, elem);
        return noErr;
    case kAudioUnitProperty_PresentPreset:
        fire_prop(a, id, scope, elem);
        return noErr;
    default:
        return noErr; /* RenderQuality, OfflineRender, HostCallbacks : acceptés, sans effet */
    }
}

static OSStatus au_addlistener(void *self, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc, void *ud) {
    LKAU *a = (LKAU *)self;
    tr("addlistener %u", (unsigned)id);
    if (a->nls < 32) { a->ls[a->nls].id = id; a->ls[a->nls].proc = proc; a->ls[a->nls].ud = ud; a->nls++; }
    return noErr;
}
static void rem_listener(LKAU *a, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc, void *ud, int useud) {
    for (int i = 0; i < a->nls; i++)
        if (a->ls[i].id == id && a->ls[i].proc == proc && (!useud || a->ls[i].ud == ud)) {
            a->ls[i] = a->ls[a->nls - 1]; a->nls--; i--;
        }
}
static OSStatus au_remlistener(void *self, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc) { rem_listener((LKAU *)self, id, proc, NULL, 0); return noErr; }
static OSStatus au_remlistener2(void *self, AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc, void *ud) { rem_listener((LKAU *)self, id, proc, ud, 1); return noErr; }
static OSStatus au_addnotify(void *self, AURenderCallback cb, void *ref) { tr("addnotify"); (void)self; (void)cb; (void)ref; return noErr; }
static OSStatus au_remnotify(void *self, AURenderCallback cb, void *ref) { tr("remnotify"); (void)self; (void)cb; (void)ref; return noErr; }

/* ---- paramètres : aucun (les potards se règlent avec MIDI Learn dans le logiciel) */
static OSStatus au_getparam(void *self, AudioUnitParameterID id, AudioUnitScope s, AudioUnitElement e, AudioUnitParameterValue *v) { tr("getparam %u", (unsigned)id); (void)self; (void)id; (void)s; (void)e; (void)v; return kAudioUnitErr_InvalidParameter; }
static OSStatus au_setparam(void *self, AudioUnitParameterID id, AudioUnitScope s, AudioUnitElement e, AudioUnitParameterValue v, UInt32 off) { tr("setparam %u", (unsigned)id); (void)self; (void)id; (void)s; (void)e; (void)v; (void)off; return kAudioUnitErr_InvalidParameter; }
static OSStatus au_schedparams(void *self, const AudioUnitParameterEvent *ev, UInt32 n) { tr("schedparams"); (void)self; (void)ev; (void)n; return noErr; }

/* ---- son */
static OSStatus au_render(void *self, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus, UInt32 n, AudioBufferList *io) {
    UInt32 nframes = n;
    LKAU *a = (LKAU *)self;
    (void)flags; (void)ts;
    { static int cnt = 0; if (cnt < 3) { cnt++; tr("render bus=%u frames=%u nbuf=%u", (unsigned)bus, (unsigned)nframes, io ? (unsigned)io->mNumberBuffers : 0u); } }
    if (!a->initialized) return kAudioUnitErr_Uninitialized;
    if (bus != 0) return kAudioUnitErr_InvalidElement;
    if (a->maxFrames && n > a->maxFrames) return kAudioUnitErr_TooManyFramesToProcess;
    if (!io || io->mNumberBuffers < 2) return kAudioUnitErr_FormatNotSupported;
    if (a->sn < n) {
        free(a->sl); free(a->sr);
        a->sn = n < 4096 ? 4096 : n;
        a->sl = (float *)calloc(a->sn, sizeof(float));
        a->sr = (float *)calloc(a->sn, sizeof(float));
        if (!a->sl || !a->sr) { a->sn = 0; return kAudio_MemFullError; }
    }
    for (int i = 0; i < 2; i++) {
        if (!io->mBuffers[i].mData) io->mBuffers[i].mData = i ? a->sr : a->sl;
        io->mBuffers[i].mDataByteSize = n * sizeof(float);
        io->mBuffers[i].mNumberChannels = 1;
    }
    float *L = (float *)io->mBuffers[0].mData, *R = (float *)io->mBuffers[1].mData;
    /* les notes en attente découpent le bloc : son jusqu'à la position de la note, la note, puis la suite */
    uint32_t w = atomic_load_explicit(&a->mqw, memory_order_acquire);
    uint32_t r = atomic_load_explicit(&a->mqr, memory_order_relaxed);
    UInt32 pos = 0;
    for (; r != w; r++) {
        UInt32 off = a->mq[r & (LK_MQ - 1)].off;
        if (off >= n) off = n ? n - 1 : 0;
        off -= off % LK_GRAIN;
        if (off > pos) {
            lkProcess(a->h, L + pos, R + pos, (int)(off - pos));
            pos = off;
        }
        lkMidi(a->h, a->mq[r & (LK_MQ - 1)].st, a->mq[r & (LK_MQ - 1)].d1, a->mq[r & (LK_MQ - 1)].d2);
    }
    atomic_store_explicit(&a->mqr, r, memory_order_release);
    if (pos < n) lkProcess(a->h, L + pos, R + pos, (int)(n - pos));
    return noErr;
}

/* ---- MIDI (instrument) */
static OSStatus au_midi(void *self, UInt32 status, UInt32 d1, UInt32 d2, UInt32 offset) {
    LKAU *a = (LKAU *)self;
    tr("midi %x %u %u off=%u", (unsigned)status, (unsigned)d1, (unsigned)d2, (unsigned)offset);
    uint32_t w = atomic_load_explicit(&a->mqw, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&a->mqr, memory_order_acquire);
    if (!a->initialized || w - r >= LK_MQ) { /* pas de son en cours ou file pleine : la note est jouée tout de suite */
        lkMidi(a->h, (int)(status & 0xFF), (int)(d1 & 0x7F), (int)(d2 & 0x7F));
        return noErr;
    }
    a->mq[w & (LK_MQ - 1)].st = (uint8_t)(status & 0xFF);
    a->mq[w & (LK_MQ - 1)].d1 = (uint8_t)(d1 & 0x7F);
    a->mq[w & (LK_MQ - 1)].d2 = (uint8_t)(d2 & 0x7F);
    a->mq[w & (LK_MQ - 1)].off = offset;
    atomic_store_explicit(&a->mqw, w + 1, memory_order_release);
    return noErr;
}
static OSStatus au_sysex(void *self, const UInt8 *data, UInt32 len) { (void)self; (void)data; (void)len; return noErr; }

/* ---- MIDI « nouvelle façon » (listes d'événements UMP, MIDI 1.0 ou MIDI 2.0) : Logic Pro récent envoie les notes
   ainsi. On les convertit en messages MIDI classiques, placés au bon moment dans le bloc (comme au_midi). */
static uint32_t ump_words(uint32_t w0) {
    switch (w0 >> 28) {
    case 0x0: case 0x1: case 0x2: case 0x6: case 0x7: return 1;
    case 0x3: case 0x4: case 0x8: case 0x9: case 0xA: return 2;
    case 0xB: case 0xC: return 3;
    default: return 4;
    }
}

static void ump_one(void *self, const uint32_t *w, UInt32 off) {
    uint32_t t = w[0] >> 28;
    uint32_t st = (w[0] >> 16) & 0xFF;     /* octet d'état (type de message + canal) */
    uint32_t kind = st & 0xF0;
    if (t == 0x2) { /* MIDI 1.0 dans un paquet UMP : déjà des messages classiques */
        au_midi(self, st, (w[0] >> 8) & 0x7F, w[0] & 0x7F, off);
        return;
    }
    if (t != 0x4) return; /* (messages système, sysex… : ignorés) */
    uint32_t idx = (w[0] >> 8) & 0x7F;
    switch (kind) {
    case 0x90: { /* note : vitesse sur 16 bits */
        uint32_t v = (w[1] >> 16) >> 9;
        if (v == 0 && (w[1] >> 16) != 0) v = 1;
        au_midi(self, st, idx, v, off);
        break;
    }
    case 0x80:
        au_midi(self, st, idx, 0, off);
        break;
    case 0xA0: case 0xB0: /* pression par touche, contrôleur : valeur sur 32 bits */
        au_midi(self, st, idx, w[1] >> 25, off);
        break;
    case 0xC0: /* changement de programme */
        au_midi(self, st, (w[1] >> 24) & 0x7F, 0, off);
        break;
    case 0xD0: /* pression du canal */
        au_midi(self, st, w[1] >> 25, 0, off);
        break;
    case 0xE0: { /* pitch bend sur 32 bits → 14 bits */
        uint32_t v = w[1] >> 18;
        au_midi(self, st, v & 0x7F, (v >> 7) & 0x7F, off);
        break;
    }
    }
}

static OSStatus au_midilist(void *self, UInt32 offset, const MIDIEventList *list) {
    if (!list) return noErr;
    tr("liste MIDI : %u paquets, protocole %d", (unsigned)list->numPackets, (int)list->protocol);
    const MIDIEventPacket *p = &list->packet[0];
    for (UInt32 i = 0; i < list->numPackets && i < 4096; i++) {
        UInt32 off = offset + (UInt32)p->timeStamp; /* pour une Audio Unit : position dans le bloc */
        UInt32 k = 0;
        while (k < p->wordCount && k < 64) {
            uint32_t n = ump_words(p->words[k]);
            if (k + n > p->wordCount) break;
            ump_one(self, &p->words[k], off);
            k += n;
        }
        p = MIDIEventPacketNext(p);
    }
    return noErr;
}

/* ---- table des fonctions demandées par le système */
static AudioComponentMethod au_lookup(SInt16 sel) {
    tr("lookup %d", (int)sel);
    switch (sel) {
    case kAudioUnitInitializeSelect:                   return (AudioComponentMethod)au_initialize;
    case kAudioUnitUninitializeSelect:                 return (AudioComponentMethod)au_uninitialize;
    case kAudioUnitGetPropertyInfoSelect:              return (AudioComponentMethod)au_getpropinfo;
    case kAudioUnitGetPropertySelect:                  return (AudioComponentMethod)au_getprop;
    case kAudioUnitSetPropertySelect:                  return (AudioComponentMethod)au_setprop;
    case kAudioUnitAddPropertyListenerSelect:          return (AudioComponentMethod)au_addlistener;
    case kAudioUnitRemovePropertyListenerSelect:       return (AudioComponentMethod)au_remlistener;
    case kAudioUnitRemovePropertyListenerWithUserDataSelect: return (AudioComponentMethod)au_remlistener2;
    case kAudioUnitAddRenderNotifySelect:              return (AudioComponentMethod)au_addnotify;
    case kAudioUnitRemoveRenderNotifySelect:           return (AudioComponentMethod)au_remnotify;
    case kAudioUnitGetParameterSelect:                 return (AudioComponentMethod)au_getparam;
    case kAudioUnitSetParameterSelect:                 return (AudioComponentMethod)au_setparam;
    case kAudioUnitScheduleParametersSelect:           return (AudioComponentMethod)au_schedparams;
    case kAudioUnitRenderSelect:                       return (AudioComponentMethod)au_render;
    case kAudioUnitResetSelect:                        return (AudioComponentMethod)au_reset;
    case kMusicDeviceMIDIEventSelect:                  return (AudioComponentMethod)au_midi;
    case kMusicDeviceMIDIEventListSelect:              return (AudioComponentMethod)au_midilist;
    case kMusicDeviceSysExSelect:                      return (AudioComponentMethod)au_sysex;
    default: return NULL;
    }
}

/* point d'entrée déclaré dans Info.plist (factoryFunction) : un nouvel exemplaire à chaque appel */
LK_EXPORT void *LayersKeysAUFactory(const AudioComponentDescription *desc) {
    (void)desc;
    LKAU *a = (LKAU *)calloc(1, sizeof(LKAU));
    if (!a) return NULL;
    a->iface.Open = au_open;
    a->iface.Close = au_close;
    a->iface.Lookup = au_lookup;
    a->iface.reserved = NULL;
    return a;
}
