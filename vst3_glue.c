//go:build vst3

/* Colle VST3 : implémente les interfaces COM du plugin (fabrique, composant, processeur audio, contrôleur, vue)
   et appelle le moteur écrit en Go (fonctions lk*). */

#ifdef _WIN32
#include <windows.h>
#define PLATFORM_VIEW "HWND"
#define LK_EXPORT __declspec(dllexport)
#else
/* Mac (et autres systèmes POSIX) : on remplace les quelques fonctions Windows utilisées */
#include <stdint.h>
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef void *HINSTANCE;
#define InterlockedIncrement(p) __sync_add_and_fetch((p), 1)
#define InterlockedDecrement(p) __sync_sub_and_fetch((p), 1)
#define lstrcpyA strcpy
#include <stdio.h>
#define wsprintfA sprintf
#define PLATFORM_VIEW "NSView"
#define LK_EXPORT __attribute__((visibility("default")))
#endif
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include "vst3inc/vst3_c_api.h"
#include "_cgo_export.h"

#define OK Steinberg_kResultOk
#define NOTOK 1            /* kResultFalse */
#define NOINTERFACE ((Steinberg_tresult)0x80004002L)
#define NOTIMPL ((Steinberg_tresult)0x80004001L)

/* identifiants de classes (uniques à Layers Keys) */
static const Steinberg_TUID CID_COMP = SMTG_INLINE_UID(0x4A42504C, 0x4B455953, 0x4C4B0001, 0x00000001);
static const Steinberg_TUID CID_CTRL = SMTG_INLINE_UID(0x4A42504C, 0x4B455953, 0x4C4B0002, 0x00000001);
/* interface privée : permet au contrôleur de retrouver son composant (même processus) */
static const Steinberg_TUID IID_LK = SMTG_INLINE_UID(0x4A42504C, 0x4B455953, 0x4C4B00FF, 0x00000001);

#define PLUGIN_NAME "Layers Keys"
#define VENDOR "JB Praise"
#define VERSION_STR "2.2.0"

static int same(const char *a, const char *b) { return memcmp(a, b, 16) == 0; }

static void put16(Steinberg_Vst_TChar *dst, int max, const char *src) {
    int i = 0;
    for (; src[i] && i < max - 1; i++) dst[i] = (Steinberg_Vst_TChar)(unsigned char)src[i];
    dst[i] = 0;
}

#ifdef _WIN32
static HINSTANCE module_handle(void) {
    HMODULE h = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&module_handle, &h);
    return (HINSTANCE)h;
}
#else
static HINSTANCE module_handle(void) { return NULL; }
#endif

/* ------------------------------------------------------------------ objet principal */

typedef struct LK {
    Steinberg_Vst_IComponent comp;
    Steinberg_Vst_IAudioProcessor proc;
    Steinberg_Vst_IEditController ctrl;
    Steinberg_Vst_IConnectionPoint conn;
    Steinberg_Vst_IMidiMapping midimap;
    LONG refs;
    int kind;             /* 0 = composant audio, 1 = contrôleur */
    uintptr_t h;          /* instance Go (0 : pas encore liée) */
    double sampleRate;
    double pvals[130];
} LK;

#define FROM(ptr, field) ((LK *)((char *)(ptr) - offsetof(LK, field)))

/* paramètres exposés au DAW pour recevoir molettes et pédales MIDI */
/* tous les CC 0..119 + aftertouch (128) + pitch bend (129) : le MIDI Learn marche donc aussi dans le DAW */
#define NPARAMS 122
static unsigned param_id(int i) { return i < 120 ? (unsigned)i : (unsigned)(128 + (i - 120)); }
static int param_index(unsigned id) { return id < 120 ? (int)id : (id == 128 ? 120 : (id == 129 ? 121 : -1)); }
static void param_name(int i, char *buf) {
    unsigned id = param_id(i);
    if (id == 1) lstrcpyA(buf, "Mod Wheel");
    else if (id == 7) lstrcpyA(buf, "Volume");
    else if (id == 11) lstrcpyA(buf, "Expression");
    else if (id == 64) lstrcpyA(buf, "Sustain");
    else if (id == 128) lstrcpyA(buf, "Aftertouch");
    else if (id == 129) lstrcpyA(buf, "Pitch Bend");
    else wsprintfA(buf, "CC %d", (int)id);
}

static uintptr_t lk_ensure(LK *o) {
    if (!o->h) o->h = lkNew();
    return o->h;
}

#define S SMTG_STDMETHODCALLTYPE

static Steinberg_tresult lk_qi(LK *o, const char *iid, void **obj) {
    void *r = NULL;
    if (same(iid, Steinberg_FUnknown_iid) || same(iid, Steinberg_Vst_IComponent_iid)) r = &o->comp;
    else if (same(iid, Steinberg_Vst_IAudioProcessor_iid)) r = &o->proc;
    else if (same(iid, Steinberg_Vst_IEditController_iid)) r = &o->ctrl;
    else if (same(iid, Steinberg_Vst_IConnectionPoint_iid)) r = &o->conn;
    else if (same(iid, Steinberg_Vst_IMidiMapping_iid)) r = &o->midimap;
    else if (same(iid, IID_LK)) r = o;
    if (!r) { *obj = NULL; return NOINTERFACE; }
    InterlockedIncrement(&o->refs);
    *obj = r;
    return OK;
}

static ULONG lk_addref(LK *o) { return (ULONG)InterlockedIncrement(&o->refs); }

static ULONG lk_release(LK *o) {
    LONG n = InterlockedDecrement(&o->refs);
    if (n == 0) {
        if (o->h) lkRelease(o->h);
        free(o);
    }
    return (ULONG)n;
}

/* ---- IComponent */
static Steinberg_tresult S c_qi(void *t, const char *iid, void **obj) { return lk_qi(FROM(t, comp), iid, obj); }
static Steinberg_uint32 S c_addref(void *t) { return lk_addref(FROM(t, comp)); }
static Steinberg_uint32 S c_release(void *t) { return lk_release(FROM(t, comp)); }
static Steinberg_tresult S c_init(void *t, struct Steinberg_FUnknown *ctx) { (void)t; (void)ctx; return OK; }
static Steinberg_tresult S c_term(void *t) { (void)t; return OK; }
static Steinberg_tresult S c_ctrlclass(void *t, char *id) { (void)t; memcpy(id, CID_CTRL, 16); return OK; }
static Steinberg_tresult S c_setio(void *t, Steinberg_Vst_IoMode m) { (void)t; (void)m; return OK; }
static Steinberg_int32 S c_buscount(void *t, Steinberg_Vst_MediaType type, Steinberg_Vst_BusDirection dir) {
    (void)t;
    if (type == Steinberg_Vst_MediaTypes_kAudio && dir == Steinberg_Vst_BusDirections_kOutput) return 1;
    if (type == Steinberg_Vst_MediaTypes_kEvent && dir == Steinberg_Vst_BusDirections_kInput) return 1;
    return 0;
}
static Steinberg_tresult S c_businfo(void *t, Steinberg_Vst_MediaType type, Steinberg_Vst_BusDirection dir, Steinberg_int32 idx, struct Steinberg_Vst_BusInfo *b) {
    (void)t;
    if (idx != 0) return NOTOK;
    memset(b, 0, sizeof(*b));
    b->mediaType = type;
    b->direction = dir;
    b->busType = 0; /* kMain */
    b->flags = Steinberg_Vst_BusInfo_BusFlags_kDefaultActive;
    if (type == Steinberg_Vst_MediaTypes_kAudio && dir == Steinberg_Vst_BusDirections_kOutput) {
        b->channelCount = 2;
        put16(b->name, 128, "Stereo Out");
        return OK;
    }
    if (type == Steinberg_Vst_MediaTypes_kEvent && dir == Steinberg_Vst_BusDirections_kInput) {
        b->channelCount = 16;
        put16(b->name, 128, "MIDI In");
        return OK;
    }
    return NOTOK;
}
static Steinberg_tresult S c_routing(void *t, struct Steinberg_Vst_RoutingInfo *a, struct Steinberg_Vst_RoutingInfo *b) { (void)t; (void)a; (void)b; return NOTIMPL; }
static Steinberg_tresult S c_activatebus(void *t, Steinberg_Vst_MediaType type, Steinberg_Vst_BusDirection dir, Steinberg_int32 idx, Steinberg_TBool st) { (void)t; (void)type; (void)dir; (void)idx; (void)st; return OK; }
static Steinberg_tresult S c_setactive(void *t, Steinberg_TBool st) {
    LK *o = FROM(t, comp);
    if (!st && o->h) lkMidi(o->h, 0xB0, 123, 0); /* toutes les notes off */
    return OK;
}

static Steinberg_tresult S c_getstate(void *t, struct Steinberg_IBStream *s) {
    LK *o = FROM(t, comp);
    int n = 0;
    void *buf = lkGetState(lk_ensure(o), &n);
    if (!buf) return NOTOK;
    char *p = (char *)buf;
    int left = n;
    while (left > 0) {
        Steinberg_int32 w = 0;
        if (s->lpVtbl->write(s, p, left, &w) != OK || w <= 0) { free(buf); return NOTOK; }
        p += w; left -= w;
    }
    free(buf);
    return OK;
}
static Steinberg_tresult S c_setstate(void *t, struct Steinberg_IBStream *s) {
    LK *o = FROM(t, comp);
    size_t cap = 1 << 16, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NOTOK;
    for (;;) {
        if (cap - len < 4096) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NOTOK; } buf = nb; }
        Steinberg_int32 got = 0;
        if (s->lpVtbl->read(s, buf + len, (Steinberg_int32)(cap - len > 65536 ? 65536 : cap - len), &got) != OK || got <= 0) break;
        len += (size_t)got;
    }
    if (len > 0) lkSetState(lk_ensure(o), buf, (int)len);
    free(buf);
    return OK;
}

static Steinberg_Vst_IComponentVtbl comp_vtbl = {
    c_qi, c_addref, c_release, c_init, c_term, c_ctrlclass, c_setio, c_buscount, c_businfo, c_routing, c_activatebus, c_setactive, c_setstate, c_getstate};

/* ---- IAudioProcessor */
static Steinberg_tresult S p_qi(void *t, const char *iid, void **obj) { return lk_qi(FROM(t, proc), iid, obj); }
static Steinberg_uint32 S p_addref(void *t) { return lk_addref(FROM(t, proc)); }
static Steinberg_uint32 S p_release(void *t) { return lk_release(FROM(t, proc)); }
static Steinberg_tresult S p_setbusarr(void *t, Steinberg_Vst_SpeakerArrangement *in, Steinberg_int32 nin, Steinberg_Vst_SpeakerArrangement *out, Steinberg_int32 nout) {
    (void)t; (void)in;
    if (nin == 0 && nout == 1 && out[0] == Steinberg_Vst_SpeakerArr_kStereo) return OK;
    return NOTOK;
}
static Steinberg_tresult S p_getbusarr(void *t, Steinberg_Vst_BusDirection dir, Steinberg_int32 idx, Steinberg_Vst_SpeakerArrangement *arr) {
    (void)t;
    if (dir == Steinberg_Vst_BusDirections_kOutput && idx == 0) { *arr = Steinberg_Vst_SpeakerArr_kStereo; return OK; }
    return NOTOK;
}
static Steinberg_tresult S p_cansize(void *t, Steinberg_int32 s) { (void)t; return s == Steinberg_Vst_SymbolicSampleSizes_kSample32 ? OK : NOTOK; }
static Steinberg_uint32 S p_latency(void *t) { (void)t; return 0; }
static Steinberg_tresult S p_setup(void *t, struct Steinberg_Vst_ProcessSetup *s) {
    LK *o = FROM(t, proc);
    o->sampleRate = s->sampleRate;
    lkSetup(lk_ensure(o), s->sampleRate, s->maxSamplesPerBlock);
    return OK;
}
static Steinberg_tresult S p_setproc(void *t, Steinberg_TBool st) { (void)t; (void)st; return OK; }

static void apply_param(uintptr_t h, unsigned id, double v) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    if (id == 129) { int b = (int)(v * 16383.0 + 0.5); lkMidi(h, 0xE0, b & 0x7F, (b >> 7) & 0x7F); }
    else if (id == 128) lkMidi(h, 0xD0, (int)(v * 127.0 + 0.5), 0);
    else if (id < 120) lkMidi(h, 0xB0, (int)id, (int)(v * 127.0 + 0.5));
}

static void apply_event(uintptr_t h, struct Steinberg_Vst_Event *e) {
    int ch = 0;
    switch (e->type) {
    case Steinberg_Vst_Event_EventTypes_kNoteOnEvent: {
        int vel = (int)(e->Steinberg_Vst_Event_noteOn.velocity * 127.0f + 0.5f);
        if (vel < 1) vel = 1;
        if (vel > 127) vel = 127;
        ch = e->Steinberg_Vst_Event_noteOn.channel & 15;
        lkMidi(h, 0x90 | ch, e->Steinberg_Vst_Event_noteOn.pitch & 0x7F, vel);
        break;
    }
    case Steinberg_Vst_Event_EventTypes_kNoteOffEvent:
        ch = e->Steinberg_Vst_Event_noteOff.channel & 15;
        lkMidi(h, 0x80 | ch, e->Steinberg_Vst_Event_noteOff.pitch & 0x7F, 0);
        break;
    case Steinberg_Vst_Event_EventTypes_kPolyPressureEvent: {
        int pr = (int)(e->Steinberg_Vst_Event_polyPressure.pressure * 127.0f + 0.5f);
        if (pr < 0) pr = 0;
        if (pr > 127) pr = 127;
        ch = e->Steinberg_Vst_Event_polyPressure.channel & 15;
        lkMidi(h, 0xA0 | ch, e->Steinberg_Vst_Event_polyPressure.pitch & 0x7F, pr);
        break;
    }
    case Steinberg_Vst_Event_EventTypes_kLegacyMIDICCOutEvent: {
        struct Steinberg_Vst_LegacyMIDICCOutEvent *m = &e->Steinberg_Vst_Event_midiCCOut;
        if (m->controlNumber < 128) lkMidi(h, 0xB0, m->controlNumber, m->value);
        else if (m->controlNumber == 129) lkMidi(h, 0xE0, m->value & 0x7F, m->value2 & 0x7F);
        break;
    }
    }
}

static Steinberg_tresult S p_process(void *t, struct Steinberg_Vst_ProcessData *d) {
    LK *o = FROM(t, proc);
    uintptr_t h = lk_ensure(o);
    int n = d->numSamples;

    /* molettes, pédale, volume : arrivent comme des paramètres (dernière valeur de chaque file) */
    if (d->inputParameterChanges) {
        struct Steinberg_Vst_IParameterChanges *pc = d->inputParameterChanges;
        Steinberg_int32 cnt = pc->lpVtbl->getParameterCount(pc);
        for (Steinberg_int32 i = 0; i < cnt; i++) {
            struct Steinberg_Vst_IParamValueQueue *q = pc->lpVtbl->getParameterData(pc, i);
            if (!q) continue;
            Steinberg_int32 pts = q->lpVtbl->getPointCount(q);
            if (pts <= 0) continue;
            Steinberg_int32 off = 0;
            Steinberg_Vst_ParamValue v = 0;
            if (q->lpVtbl->getPoint(q, pts - 1, &off, &v) == OK) apply_param(h, q->lpVtbl->getParameterId(q), v);
        }
    }

    float *outL = NULL, *outR = NULL;
    if (d->numOutputs > 0 && d->outputs && d->outputs[0].numChannels >= 2) {
        outL = d->outputs[0].Steinberg_Vst_AudioBusBuffers_channelBuffers32[0];
        outR = d->outputs[0].Steinberg_Vst_AudioBusBuffers_channelBuffers32[1];
        d->outputs[0].silenceFlags = 0;
    }

    int pos = 0;
    if (d->inputEvents) {
        struct Steinberg_Vst_IEventList *el = d->inputEvents;
        Steinberg_int32 cnt = el->lpVtbl->getEventCount(el);
        for (Steinberg_int32 i = 0; i < cnt; i++) {
            struct Steinberg_Vst_Event e;
            if (el->lpVtbl->getEvent(el, i, &e) != OK) continue;
            int off = e.sampleOffset;
            if (off < pos) off = pos;
            if (off > n) off = n;
            if (off > pos && outL) { lkProcess(h, outL + pos, outR + pos, off - pos); pos = off; }
            apply_event(h, &e);
        }
    }
    if (outL && n > pos) lkProcess(h, outL + pos, outR + pos, n - pos);
    return OK;
}
static Steinberg_uint32 S p_tail(void *t) { LK *o = FROM(t, proc); return (Steinberg_uint32)(o->sampleRate * 4); }

static Steinberg_Vst_IAudioProcessorVtbl proc_vtbl = {
    p_qi, p_addref, p_release, p_setbusarr, p_getbusarr, p_cansize, p_latency, p_setup, p_setproc, p_process, p_tail};

/* ---- IConnectionPoint : le contrôleur retrouve son composant */
static Steinberg_tresult S k_qi(void *t, const char *iid, void **obj) { return lk_qi(FROM(t, conn), iid, obj); }
static Steinberg_uint32 S k_addref(void *t) { return lk_addref(FROM(t, conn)); }
static Steinberg_uint32 S k_release(void *t) { return lk_release(FROM(t, conn)); }
static Steinberg_tresult S k_connect(void *t, struct Steinberg_Vst_IConnectionPoint *other) {
    LK *me = FROM(t, conn);
    void *peer = NULL;
    if (!other) return OK;
    if (other->lpVtbl->queryInterface(other, IID_LK, &peer) == OK && peer) {
        LK *p = (LK *)peer;
        if (me->kind == 1 && p->kind == 0 && !me->h) {
            me->h = lk_ensure(p);
            lkRetain(me->h);
        }
        lk_release(p); /* rend la référence prise par queryInterface */
    }
    return OK;
}
static Steinberg_tresult S k_disconnect(void *t, struct Steinberg_Vst_IConnectionPoint *other) { (void)t; (void)other; return OK; }
static Steinberg_tresult S k_notify(void *t, struct Steinberg_Vst_IMessage *m) { (void)t; (void)m; return OK; }
static Steinberg_Vst_IConnectionPointVtbl conn_vtbl = {k_qi, k_addref, k_release, k_connect, k_disconnect, k_notify};

/* ---- IMidiMapping */
static Steinberg_tresult S m_qi(void *t, const char *iid, void **obj) { return lk_qi(FROM(t, midimap), iid, obj); }
static Steinberg_uint32 S m_addref(void *t) { return lk_addref(FROM(t, midimap)); }
static Steinberg_uint32 S m_release(void *t) { return lk_release(FROM(t, midimap)); }
static Steinberg_tresult S m_assign(void *t, Steinberg_int32 bus, Steinberg_int16 channel, Steinberg_Vst_CtrlNumber cc, Steinberg_Vst_ParamID *id) {
    (void)t; (void)bus; (void)channel;
    if (cc >= 0 && cc < 120) { *id = (unsigned)cc; return OK; }
    if (cc == 128 || cc == 129) { *id = (unsigned)cc; return OK; } /* aftertouch, pitch bend */
    return NOTOK;
}
static Steinberg_Vst_IMidiMappingVtbl midimap_vtbl = {m_qi, m_addref, m_release, m_assign};

/* ---- IPlugView */
typedef struct LKView {
    Steinberg_IPlugView view;
    LONG refs;
    LK *owner;
    int attached;
} LKView;

static Steinberg_tresult S v_qi(void *t, const char *iid, void **obj) {
    LKView *v = (LKView *)t;
    if (same(iid, Steinberg_FUnknown_iid) || same(iid, Steinberg_IPlugView_iid)) {
        InterlockedIncrement(&v->refs);
        *obj = v;
        return OK;
    }
    *obj = NULL;
    return NOINTERFACE;
}
static Steinberg_uint32 S v_addref(void *t) { return (ULONG)InterlockedIncrement(&((LKView *)t)->refs); }
static Steinberg_uint32 S v_release(void *t) {
    LKView *v = (LKView *)t;
    LONG n = InterlockedDecrement(&v->refs);
    if (n == 0) {
        if (v->attached && v->owner->h) lkDetach(v->owner->h);
        lk_release(v->owner);
        free(v);
    }
    return (ULONG)n;
}
static Steinberg_tresult S v_platform(void *t, const char *type) { (void)t; return strcmp(type, PLATFORM_VIEW) == 0 ? OK : NOTOK; }
static Steinberg_tresult S v_attached(void *t, void *parent, const char *type) {
    LKView *v = (LKView *)t;
    if (strcmp(type, PLATFORM_VIEW) != 0 || !parent) return NOTOK;
    if (v->attached) lkDetach(lk_ensure(v->owner));
    if (!lkAttach(lk_ensure(v->owner), (uintptr_t)parent, (uintptr_t)module_handle())) return NOTOK;
    v->attached = 1;
    return OK;
}
static Steinberg_tresult S v_removed(void *t) {
    LKView *v = (LKView *)t;
    if (v->attached) { lkDetach(v->owner->h); v->attached = 0; }
    return OK;
}
static Steinberg_tresult S v_wheel(void *t, float d) { (void)t; (void)d; return NOTOK; }
static Steinberg_tresult S v_keydown(void *t, Steinberg_char16 k, Steinberg_int16 kc, Steinberg_int16 m) { (void)t; (void)k; (void)kc; (void)m; return NOTOK; }
static Steinberg_tresult S v_keyup(void *t, Steinberg_char16 k, Steinberg_int16 kc, Steinberg_int16 m) { (void)t; (void)k; (void)kc; (void)m; return NOTOK; }
static Steinberg_tresult S v_getsize(void *t, struct Steinberg_ViewRect *r) {
    LKView *v = (LKView *)t;
    int w = 1200, h = 744;
    lkSize(lk_ensure(v->owner), &w, &h);
    r->left = 0; r->top = 0; r->right = w; r->bottom = h;
    return OK;
}
static Steinberg_tresult S v_onsize(void *t, struct Steinberg_ViewRect *r) { (void)t; (void)r; return OK; }
static Steinberg_tresult S v_onfocus(void *t, Steinberg_TBool s) { (void)t; (void)s; return OK; }
static Steinberg_tresult S v_setframe(void *t, struct Steinberg_IPlugFrame *f) { (void)t; (void)f; return OK; }
static Steinberg_tresult S v_canresize(void *t) { (void)t; return NOTOK; }
static Steinberg_tresult S v_check(void *t, struct Steinberg_ViewRect *r) { return v_getsize(t, r); }
static Steinberg_IPlugViewVtbl view_vtbl = {
    v_qi, v_addref, v_release, v_platform, v_attached, v_removed, v_wheel, v_keydown, v_keyup, v_getsize, v_onsize, v_onfocus, v_setframe, v_canresize, v_check};

/* ---- IEditController */
static Steinberg_tresult S e_qi(void *t, const char *iid, void **obj) { return lk_qi(FROM(t, ctrl), iid, obj); }
static Steinberg_uint32 S e_addref(void *t) { return lk_addref(FROM(t, ctrl)); }
static Steinberg_uint32 S e_release(void *t) { return lk_release(FROM(t, ctrl)); }
static Steinberg_tresult S e_init(void *t, struct Steinberg_FUnknown *c) { (void)t; (void)c; return OK; }
static Steinberg_tresult S e_term(void *t) { (void)t; return OK; }
static Steinberg_tresult S e_setcompstate(void *t, struct Steinberg_IBStream *s) { (void)t; (void)s; return OK; }
static Steinberg_tresult S e_setstate(void *t, struct Steinberg_IBStream *s) { (void)t; (void)s; return OK; }
static Steinberg_tresult S e_getstate(void *t, struct Steinberg_IBStream *s) { (void)t; (void)s; return OK; }
static Steinberg_int32 S e_pcount(void *t) { (void)t; return NPARAMS; }
static Steinberg_tresult S e_pinfo(void *t, Steinberg_int32 i, struct Steinberg_Vst_ParameterInfo *p) {
    (void)t;
    if (i < 0 || i >= NPARAMS) return NOTOK;
    memset(p, 0, sizeof(*p));
    char nm[32];
    param_name(i, nm);
    p->id = param_id(i);
    put16(p->title, 128, nm);
    put16(p->shortTitle, 128, nm);
    p->stepCount = 0;
    p->defaultNormalizedValue = (param_id(i) == 129) ? 0.5 : (param_id(i) == 7 || param_id(i) == 11 ? 1.0 : 0.0);
    p->unitId = 0;
    p->flags = Steinberg_Vst_ParameterInfo_ParameterFlags_kCanAutomate | Steinberg_Vst_ParameterInfo_ParameterFlags_kIsHidden;
    return OK;
}
static Steinberg_tresult S e_pstr(void *t, Steinberg_Vst_ParamID id, Steinberg_Vst_ParamValue v, Steinberg_Vst_String128 s) {
    (void)t; (void)id;
    char buf[16];
    int n = (int)(v * 127.0 + 0.5);
    buf[0] = 0;
    wsprintfA(buf, "%d", n);
    put16(s, 128, buf);
    return OK;
}
static Steinberg_tresult S e_pval(void *t, Steinberg_Vst_ParamID id, Steinberg_Vst_TChar *s, Steinberg_Vst_ParamValue *v) { (void)t; (void)id; (void)s; (void)v; return NOTOK; }
static Steinberg_Vst_ParamValue S e_n2p(void *t, Steinberg_Vst_ParamID id, Steinberg_Vst_ParamValue v) { (void)t; (void)id; return v; }
static Steinberg_Vst_ParamValue S e_p2n(void *t, Steinberg_Vst_ParamID id, Steinberg_Vst_ParamValue v) { (void)t; (void)id; return v; }
static Steinberg_Vst_ParamValue S e_getnorm(void *t, Steinberg_Vst_ParamID id) {
    LK *o = FROM(t, ctrl);
    { int i = param_index(id); if (i >= 0) return o->pvals[i]; }
    return 0;
}
static Steinberg_tresult S e_setnorm(void *t, Steinberg_Vst_ParamID id, Steinberg_Vst_ParamValue v) {
    LK *o = FROM(t, ctrl);
    { int i = param_index(id); if (i >= 0) { o->pvals[i] = v; return OK; } }
    return NOTOK;
}
static Steinberg_tresult S e_handler(void *t, struct Steinberg_Vst_IComponentHandler *h) { (void)t; (void)h; return OK; }
static struct Steinberg_IPlugView *S e_createview(void *t, const char *name) {
    LK *o = FROM(t, ctrl);
    if (!name || strcmp(name, "editor") != 0) return NULL;
    lk_ensure(o);
    LKView *v = (LKView *)calloc(1, sizeof(LKView));
    if (!v) return NULL;
    v->view.lpVtbl = &view_vtbl;
    v->refs = 1;
    v->owner = o;
    lk_addref(o);
    return (struct Steinberg_IPlugView *)v;
}
static Steinberg_Vst_IEditControllerVtbl ctrl_vtbl = {
    e_qi, e_addref, e_release, e_init, e_term, e_setcompstate, e_setstate, e_getstate, e_pcount, e_pinfo, e_pstr, e_pval,
    e_n2p, e_p2n, e_getnorm, e_setnorm, e_handler, e_createview};

static LK *lk_create(int kind) {
    LK *o = (LK *)calloc(1, sizeof(LK));
    if (!o) return NULL;
    o->comp.lpVtbl = &comp_vtbl;
    o->proc.lpVtbl = &proc_vtbl;
    o->ctrl.lpVtbl = &ctrl_vtbl;
    o->conn.lpVtbl = &conn_vtbl;
    o->midimap.lpVtbl = &midimap_vtbl;
    o->refs = 1;
    o->kind = kind;
    o->sampleRate = 44100;
    for (int i = 0; i < NPARAMS; i++) o->pvals[i] = (param_id(i) == 129) ? 0.5 : ((param_id(i) == 7 || param_id(i) == 11) ? 1.0 : 0.0);
    if (kind == 0) o->h = lkNew(); /* le composant possède le moteur ; le contrôleur s'y relie par connect() */
    return o;
}

/* ------------------------------------------------------------------ fabrique */

typedef struct Factory {
    Steinberg_IPluginFactory3 f;
} Factory;

static Steinberg_tresult S f_qi(void *t, const char *iid, void **obj) {
    if (same(iid, Steinberg_FUnknown_iid) || same(iid, Steinberg_IPluginFactory_iid) ||
        same(iid, Steinberg_IPluginFactory2_iid) || same(iid, Steinberg_IPluginFactory3_iid)) {
        *obj = t;
        return OK;
    }
    *obj = NULL;
    return NOINTERFACE;
}
static Steinberg_uint32 S f_addref(void *t) { (void)t; return 2; }
static Steinberg_uint32 S f_release(void *t) { (void)t; return 1; }
static Steinberg_tresult S f_info(void *t, struct Steinberg_PFactoryInfo *i) {
    (void)t;
    memset(i, 0, sizeof(*i));
    strcpy(i->vendor, VENDOR);
    strcpy(i->url, "");
    strcpy(i->email, "");
    i->flags = 16; /* kUnicode */
    return OK;
}
static Steinberg_int32 S f_count(void *t) { (void)t; return 2; }
static Steinberg_tresult S f_class(void *t, Steinberg_int32 idx, struct Steinberg_PClassInfo *c) {
    (void)t;
    if (idx < 0 || idx > 1) return NOTOK;
    memset(c, 0, sizeof(*c));
    memcpy(c->cid, idx == 0 ? CID_COMP : CID_CTRL, 16);
    c->cardinality = 0x7FFFFFFF;
    strcpy(c->category, idx == 0 ? "Audio Module Class" : "Component Controller Class");
    strcpy(c->name, PLUGIN_NAME);
    return OK;
}
static Steinberg_tresult S f_create(void *t, const char *cid, const char *iid, void **obj) {
    (void)t;
    int kind = same(cid, CID_COMP) ? 0 : (same(cid, CID_CTRL) ? 1 : -1);
    *obj = NULL;
    if (kind < 0) return NOTOK;
    LK *o = lk_create(kind);
    if (!o) return NOTOK;
    Steinberg_tresult r = lk_qi(o, iid, obj);
    lk_release(o); /* la référence initiale ; l'appelant garde celle de queryInterface */
    return r;
}
static Steinberg_tresult S f_class2(void *t, Steinberg_int32 idx, struct Steinberg_PClassInfo2 *c) {
    (void)t;
    if (idx < 0 || idx > 1) return NOTOK;
    memset(c, 0, sizeof(*c));
    memcpy(c->cid, idx == 0 ? CID_COMP : CID_CTRL, 16);
    c->cardinality = 0x7FFFFFFF;
    strcpy(c->category, idx == 0 ? "Audio Module Class" : "Component Controller Class");
    strcpy(c->name, PLUGIN_NAME);
    c->classFlags = 0;
    strcpy(c->subCategories, idx == 0 ? "Instrument|Synth" : "");
    strcpy(c->vendor, VENDOR);
    strcpy(c->version, VERSION_STR);
    strcpy(c->sdkVersion, "VST 3.7");
    return OK;
}
static void w16(Steinberg_char16 *d, int max, const char *s) { put16((Steinberg_Vst_TChar *)d, max, s); }
static Steinberg_tresult S f_classw(void *t, Steinberg_int32 idx, struct Steinberg_PClassInfoW *c) {
    (void)t;
    if (idx < 0 || idx > 1) return NOTOK;
    memset(c, 0, sizeof(*c));
    memcpy(c->cid, idx == 0 ? CID_COMP : CID_CTRL, 16);
    c->cardinality = 0x7FFFFFFF;
    strcpy(c->category, idx == 0 ? "Audio Module Class" : "Component Controller Class");
    w16(c->name, 64, PLUGIN_NAME);
    c->classFlags = 0;
    strcpy(c->subCategories, idx == 0 ? "Instrument|Synth" : "");
    w16(c->vendor, 64, VENDOR);
    w16(c->version, 64, VERSION_STR);
    w16(c->sdkVersion, 64, "VST 3.7");
    return OK;
}
static Steinberg_tresult S f_ctx(void *t, struct Steinberg_FUnknown *c) { (void)t; (void)c; return OK; }

static Steinberg_IPluginFactory3Vtbl factory_vtbl = {
    f_qi, f_addref, f_release, f_info, f_count, f_class, f_create, f_class2, f_classw, f_ctx};
static Factory the_factory = {{&factory_vtbl}};

LK_EXPORT void *GetPluginFactory(void) { return &the_factory; }
#ifdef _WIN32
LK_EXPORT int InitDll(void) { return 1; }
LK_EXPORT int ExitDll(void) { return 1; }
#else
/* points d'entrée du paquet .vst3 sur Mac */
LK_EXPORT int bundleEntry(void *bundleRef) { (void)bundleRef; return 1; }
LK_EXPORT int bundleExit(void) { return 1; }
#endif
