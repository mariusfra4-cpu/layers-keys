// Petit « faux Logic » : charge l'Audio Unit Layers Keys comme le fait Logic Pro et vérifie ce qu'auval ne vérifie pas :
//   - rendu avec des notes MIDI décalées dans le bloc (Logic envoie toujours les notes avec un décalage),
//   - temps de traîne annoncé (bounce « avec la fin du son »),
//   - sauvegarde puis rechargement de l'état (projet Logic),
//   - ouverture de la fenêtre du plugin dans une vraie fenêtre, fermeture, puis réouverture.
// Construction (sur Mac) :  clang -fobjc-arc -arch arm64 -arch x86_64 tools/au_logic_test.m -o au_logic_test \
//                              -framework AudioToolbox -framework AppKit -framework CoreAudioKit
// Le plugin doit être installé (~/Library/Audio/Plug-Ins/Components). Code de sortie 0 = tout va bien.

#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreMIDI/CoreMIDI.h>
#import <AudioUnit/AUCocoaUIView.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(cond, ...) do { if (cond) { printf("OK    "); } else { printf("ÉCHEC "); fails++; } printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

static void spin(double sec) { [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:sec]]; }

static NSView *openEditor(AudioUnit au, NSWindow **outWin) {
    UInt32 sz = 0;
    Boolean w = 0;
    OSStatus st = AudioUnitGetPropertyInfo(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &sz, &w);
    CHECK(st == noErr && sz >= sizeof(AudioUnitCocoaViewInfo), "CocoaUI annoncée (taille %u, code %d)", (unsigned)sz, (int)st);
    if (st != noErr) return nil;
    AudioUnitCocoaViewInfo *info = (AudioUnitCocoaViewInfo *)calloc(1, sz);
    st = AudioUnitGetProperty(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &sz);
    CHECK(st == noErr && info->mCocoaAUViewBundleLocation && info->mCocoaAUViewClass[0], "CocoaUI lue (code %d)", (int)st);
    if (st != noErr || !info->mCocoaAUViewBundleLocation) { free(info); return nil; }
    NSURL *url = (__bridge_transfer NSURL *)info->mCocoaAUViewBundleLocation;
    NSString *clsName = (__bridge_transfer NSString *)info->mCocoaAUViewClass[0];
    free(info);
    printf("      paquet : %s  classe : %s\n", url.path.UTF8String, clsName.UTF8String);
    NSBundle *b = [NSBundle bundleWithURL:url];
    CHECK(b != nil, "paquet de l'interface trouvé");
    /* exactement comme Logic, GarageBand et FL Studio : la classe doit être DANS le paquet (aucune recherche ailleurs) */
    [b load];
    Class c = [b classNamed:clsName];
    CHECK(c != nil, "classe de l'interface trouvée dans le paquet du plugin (comme Logic / FL Studio)");
    if (!c) return nil;
    NSBundle *owner = [NSBundle bundleForClass:c];
    CHECK([owner.bundleURL.path.stringByStandardizingPath isEqualToString:url.path.stringByStandardizingPath],
          "la classe appartient bien au paquet du plugin (%s)", owner.bundlePath.UTF8String);
    CHECK([c conformsToProtocol:@protocol(AUCocoaUIBase)], "la classe déclare le protocole AUCocoaUIBase");
    NSObject<AUCocoaUIBase> *factory = [[c alloc] init];
    CHECK([factory respondsToSelector:@selector(uiViewForAudioUnit:withSize:)], "la fabrique répond à uiViewForAudioUnit:withSize:");
    NSView *v = [factory uiViewForAudioUnit:au withSize:NSMakeSize(800, 600)];
    CHECK(v != nil && v.frame.size.width > 100 && v.frame.size.height > 100, "vue de l'éditeur créée (%.0f x %.0f)", v.frame.size.width, v.frame.size.height);
    if (!v) return nil;
    NSWindow *win = [[NSWindow alloc] initWithContentRect:v.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                  backing:NSBackingStoreBuffered defer:NO];
    win.releasedWhenClosed = NO;
    [win setContentView:v];
    [win orderFront:nil];
    *outWin = win;
    return v;
}

static int render(AudioUnit au, UInt32 n, int withBuffers, float *peak) {
    static float L[4096], R[4096];
    struct { AudioBufferList l; AudioBuffer extra; } abl;
    abl.l.mNumberBuffers = 2;
    abl.l.mBuffers[0].mNumberChannels = 1;
    abl.l.mBuffers[0].mDataByteSize = n * 4;
    abl.l.mBuffers[0].mData = withBuffers ? L : NULL;
    abl.l.mBuffers[1].mNumberChannels = 1;
    abl.l.mBuffers[1].mDataByteSize = n * 4;
    abl.l.mBuffers[1].mData = withBuffers ? R : NULL;
    static Float64 t = 0;
    AudioTimeStamp ts = {0};
    ts.mSampleTime = t;
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    t += n;
    AudioUnitRenderActionFlags fl = 0;
    OSStatus st = AudioUnitRender(au, &fl, &ts, 0, n, &abl.l);
    if (st == noErr && peak) {
        float *l = (float *)abl.l.mBuffers[0].mData, *r = (float *)abl.l.mBuffers[1].mData;
        for (UInt32 i = 0; i < n; i++) {
            if (l[i] != l[i] || r[i] != r[i]) { *peak = 1e9f; break; } // NaN
            if (fabsf(l[i]) > *peak) *peak = fabsf(l[i]);
            if (fabsf(r[i]) > *peak) *peak = fabsf(r[i]);
        }
    }
    return (int)st;
}

/* --son : avec le plugin de TEST (déjà autorisé), charge le Grand Piano sur la piste 1 par l'état du plugin
   (comme un projet Logic), joue une note comme Logic et mesure le son qui sort. */
static int soundTest(AudioComponent comp) {
    AudioUnit au = NULL;
    OSStatus st = AudioComponentInstanceNew(comp, &au);
    CHECK(st == noErr && au, "son : instance créée");
    if (!au) return 1;
    UInt32 maxf = 1024;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxf, sizeof maxf);
    AudioStreamBasicDescription f = {0};
    UInt32 sz = sizeof f;
    AudioUnitGetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, &sz);
    f.mSampleRate = 48000;
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
    st = AudioUnitInitialize(au);
    CHECK(st == noErr, "son : initialisation");
    float peak = 0;
    for (int i = 0; i < 20; i++) render(au, 512, 1, &peak);
    CHECK(peak < 1e-4, "son : silence avant toute note (crête %.5f)", peak);
    /* état actuel du plugin → piste 1 = Grand Piano intégré → renvoyé au plugin (ce que fait Logic en ouvrant un projet) */
    CFPropertyListRef pl = NULL;
    sz = sizeof pl;
    st = AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &sz);
    NSDictionary *d = (__bridge_transfer NSDictionary *)pl;
    NSData *data = d[@kAUPresetDataKey];
    NSMutableDictionary *js = data ? [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:nil] : nil;
    CHECK(js != nil && [js[@"setup"] isKindOfClass:[NSArray class]], "son : état du plugin lisible");
    if (!js) return 1;
    NSMutableDictionary *t1 = js[@"setup"][0];
    t1[@"src"] = [@{@"kind": @"builtin", @"builtin": @0, @"name": @"Grand Piano"} mutableCopy];
    t1[@"vol"] = @0;
    t1[@"mute"] = @NO;
    NSMutableDictionary *nd = [d mutableCopy];
    nd[@kAUPresetDataKey] = [NSJSONSerialization dataWithJSONObject:js options:0 error:nil];
    CFPropertyListRef npl = (__bridge CFPropertyListRef)nd;
    st = AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &npl, sizeof npl);
    CHECK(st == noErr, "son : Grand Piano chargé sur la piste 1 (état du plugin)");
    for (int i = 0; i < 60; i++) { render(au, 512, 1, NULL); spin(0.02); }
    /* une note, comme Logic : décalage dans le bloc */
    MusicDeviceMIDIEvent(au, 0x90, 60, 110, 100);
    peak = 0;
    for (int i = 0; i < 60; i++) render(au, 512, 1, &peak);
    CHECK(peak > 0.01, "son : la note joue, le son sort du plugin (crête %.4f)", peak);
    MusicDeviceMIDIEvent(au, 0x80, 60, 0, 0);
    /* notes envoyées « à la nouvelle façon » (listes UMP), comme Logic Pro récent : MIDI 1.0 puis MIDI 2.0 */
    for (int proto = 1; proto <= 2; proto++) {
        MIDIEventList list;
        MIDIEventPacket *pk = MIDIEventListInit(&list, proto == 1 ? kMIDIProtocol_1_0 : kMIDIProtocol_2_0);
        UInt32 words[2];
        int nw;
        if (proto == 1) { words[0] = 0x20000000u | (0x90u << 16) | (67u << 8) | 110u; nw = 1; }          /* note 67 */
        else { words[0] = 0x40000000u | (0x90u << 16) | (69u << 8); words[1] = 0xE0000000u; nw = 2; }    /* note 69, vitesse 16 bits */
        pk = MIDIEventListAdd(&list, sizeof list, pk, 64, nw, words);
        OSStatus est = MusicDeviceMIDIEventList(au, 0, &list);
        peak = 0;
        for (int i = 0; i < 60; i++) render(au, 512, 1, &peak);
        CHECK(est == noErr && peak > 0.01, "son : note reçue en liste MIDI %d.0 (comme Logic récent), crête %.4f (code %d)", proto, peak, (int)est);
        MusicDeviceMIDIEvent(au, 0xB0, 123, 0, 0);
        for (int i = 0; i < 100; i++) render(au, 512, 1, NULL);
    }
    /* stéréo entrelacée dans un seul tampon (certains hôtes) : le son doit sortir aussi */
    {
        static float buf[2 * 1024];
        MusicDeviceMIDIEvent(au, 0x90, 72, 110, 0);
        float pk2 = 0;
        OSStatus est = noErr;
        for (int i = 0; i < 40; i++) {
            AudioBufferList one;
            one.mNumberBuffers = 1;
            one.mBuffers[0].mNumberChannels = 2;
            one.mBuffers[0].mDataByteSize = 512 * 2 * sizeof(float);
            one.mBuffers[0].mData = buf;
            AudioUnitRenderActionFlags fl = 0;
            AudioTimeStamp ts = {0};
            ts.mFlags = kAudioTimeStampSampleTimeValid;
            est = AudioUnitRender(au, &fl, &ts, 0, 512, &one);
            if (est != noErr) break;
            for (int k = 0; k < 1024; k++) if (fabsf(buf[k]) > pk2) pk2 = fabsf(buf[k]);
        }
        CHECK(est == noErr && pk2 > 0.01, "son : stéréo entrelacée dans un seul tampon (crête %.4f, code %d)", pk2, (int)est);
        MusicDeviceMIDIEvent(au, 0xB0, 123, 0, 0);
        for (int i = 0; i < 100; i++) render(au, 512, 1, NULL);
    }
    /* même chose avec la fenêtre du plugin ouverte (comme dans Logic) */
    NSWindow *w = nil;
    NSView *v = openEditor(au, &w);
    if (v) {
        spin(0.5);
        MusicDeviceMIDIEvent(au, 0x90, 64, 110, 0);
        peak = 0;
        for (int i = 0; i < 60; i++) { render(au, 512, 1, &peak); if (i % 10 == 0) spin(0.02); }
        CHECK(peak > 0.01, "son : la note joue aussi avec la fenêtre ouverte (crête %.4f)", peak);
        MusicDeviceMIDIEvent(au, 0x80, 64, 0, 0);
        [w close];
        spin(0.5);
    }
    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);
    printf("\n%s (%d échec(s))\n", fails ? "TEST DU SON : PROBLÈME" : "TEST DU SON : TOUT VA BIEN", fails);
    return fails ? 1 : 0;
}

/* --gardien : imite la surveillance de Logic Pro 13. Logic attrape les signaux (SIGSEGV, SIGBUS, SIGURG…) qui arrivent
   pendant qu'il appelle un plugin, et affiche alors « The Audio Unit plug-in reported a problem… ». Ici, on installe
   un gardien APRÈS le chargement du plugin (comme Logic), on fait tourner le son sur une ligne audio séparée avec des notes,
   la fenêtre ouverte et un son chargé, puis on compte chaque signal reçu. Le plugin ne doit en produire AUCUN. */
static const int g_sigs[] = {SIGURG, SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGABRT, SIGFPE, SIGPROF, SIGUSR1, SIGUSR2};
#define NSIGS (int)(sizeof g_sigs / sizeof g_sigs[0])
static struct sigaction g_old[NSIGS];
static _Atomic int g_cnt[NSIGS], g_cntAudio[NSIGS];
static pthread_t g_audioThread;
static _Atomic int g_run = 1;
static void guard(int sig, siginfo_t *si, void *uc) {
    for (int i = 0; i < NSIGS; i++) {
        if (g_sigs[i] != sig) continue;
        atomic_fetch_add(&g_cnt[i], 1);
        if (pthread_equal(pthread_self(), g_audioThread)) atomic_fetch_add(&g_cntAudio[i], 1);
        /* on rend la main au gestionnaire d'avant (celui de Go), pour que le plugin continue comme sans gardien */
        if (g_old[i].sa_flags & SA_SIGINFO) { if (g_old[i].sa_sigaction) g_old[i].sa_sigaction(sig, si, uc); }
        else if (g_old[i].sa_handler != SIG_DFL && g_old[i].sa_handler != SIG_IGN) g_old[i].sa_handler(sig);
        return;
    }
}
static void *audioLoop(void *p) {
    AudioUnit au = (AudioUnit)p;
    while (atomic_load(&g_run)) { render(au, 512, 1, NULL); usleep(10000); } /* ≈ un bloc de 512 images toutes les 10 ms */
    return NULL;
}
static int guardTest(AudioComponent comp) {
    AudioUnit au = NULL;
    OSStatus st = AudioComponentInstanceNew(comp, &au);
    CHECK(st == noErr && au, "gardien : instance créée");
    if (!au) return 1;
    UInt32 maxf = 1024;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxf, sizeof maxf);
    st = AudioUnitInitialize(au);
    CHECK(st == noErr, "gardien : initialisation");
    /* le gardien est posé maintenant, après le chargement du plugin, comme Logic */
    for (int i = 0; i < NSIGS; i++) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = guard;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(g_sigs[i], &sa, &g_old[i]);
    }
    pthread_create(&g_audioThread, NULL, audioLoop, au);
    NSWindow *w = nil;
    NSView *v = openEditor(au, &w);
    /* un son chargé par l'état du plugin (comme un projet Logic), puis 12 s de jeu avec des notes */
    CFPropertyListRef pl = NULL;
    UInt32 sz = sizeof pl;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &sz) == noErr && pl) {
        NSDictionary *d = (__bridge_transfer NSDictionary *)pl;
        NSMutableDictionary *js = [NSJSONSerialization JSONObjectWithData:d[@kAUPresetDataKey] options:NSJSONReadingMutableContainers error:nil];
        if ([js[@"setup"] isKindOfClass:[NSArray class]]) {
            js[@"setup"][0][@"src"] = [@{@"kind": @"builtin", @"builtin": @0, @"name": @"Grand Piano"} mutableCopy];
            NSMutableDictionary *nd = [d mutableCopy];
            nd[@kAUPresetDataKey] = [NSJSONSerialization dataWithJSONObject:js options:0 error:nil];
            CFPropertyListRef npl = (__bridge CFPropertyListRef)nd;
            AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &npl, sizeof npl);
        }
    }
    for (int k = 0; k < 120; k++) {
        int n = 48 + (k * 7) % 36;
        MusicDeviceMIDIEvent(au, 0x90, n, 100, 64);
        spin(0.05);
        MusicDeviceMIDIEvent(au, 0x80, n, 0, 0);
        if (k % 20 == 0) { /* Logic lit l'état régulièrement (sauvegarde automatique du projet) */
            CFPropertyListRef p2 = NULL; UInt32 s2 = sizeof p2;
            if (AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &p2, &s2) == noErr && p2) CFRelease(p2);
        }
        spin(0.05);
    }
    atomic_store(&g_run, 0);
    pthread_join(g_audioThread, NULL);
    if (v) [w close];
    spin(0.3);
    int total = 0;
    static const char *names[] = {"SIGURG", "SIGSEGV", "SIGBUS", "SIGILL", "SIGTRAP", "SIGABRT", "SIGFPE", "SIGPROF", "SIGUSR1", "SIGUSR2"};
    for (int i = 0; i < NSIGS; i++) {
        int c = atomic_load(&g_cnt[i]);
        total += c;
        if (c) printf("      signal %s : %d fois (dont %d sur la ligne audio)\n", names[i], c, atomic_load(&g_cntAudio[i]));
    }
    CHECK(total == 0, "gardien : aucun signal pendant 12 s de jeu (ce que Logic Pro 13 prendrait pour un plantage) : %d signal(aux)", total);
    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);
    printf("\n%s (%d échec(s))\n", fails ? "GARDIEN : PROBLÈME" : "GARDIEN : TOUT VA BIEN", fails);
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        AudioComponentDescription d = {kAudioUnitType_MusicDevice, 'Lkys', 'JBPr', 0, 0};
        AudioComponent comp = AudioComponentFindNext(NULL, &d);
        CHECK(comp != NULL, "Audio Unit « JB Praise: Layers Keys » trouvée");
        if (!comp) return 1;
        UInt32 ver = 0;
        AudioComponentGetVersion(comp, &ver);
        printf("      version annoncée : %u.%u.%u\n", ver >> 16, (ver >> 8) & 0xFF, ver & 0xFF);
        if (argc > 1 && strcmp(argv[1], "--son") == 0) return soundTest(comp);
        if (argc > 1 && strcmp(argv[1], "--gardien") == 0) return guardTest(comp);

        AudioUnit au = NULL;
        OSStatus st = AudioComponentInstanceNew(comp, &au);
        CHECK(st == noErr && au, "instance créée (code %d)", (int)st);
        if (!au) return 1;

        // réglages comme Logic : 48 kHz, blocs jusqu'à 1024
        UInt32 maxf = 1024;
        st = AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxf, sizeof maxf);
        CHECK(st == noErr, "taille de bloc maximale 1024");
        AudioStreamBasicDescription f = {0};
        UInt32 sz = sizeof f;
        AudioUnitGetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, &sz);
        f.mSampleRate = 48000;
        st = AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        CHECK(st == noErr, "format 48 kHz stéréo accepté");
        st = AudioUnitInitialize(au);
        CHECK(st == noErr, "initialisation (code %d)", (int)st);

        Float64 tail = -1;
        sz = sizeof tail;
        st = AudioUnitGetProperty(au, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &tail, &sz);
        CHECK(st == noErr && tail >= 5, "temps de traîne annoncé : %.1f s", tail);

        // rendu avec des notes décalées dans le bloc, dont des accords et des décalages hors limites
        int bad = 0;
        float peak = 0;
        for (int blk = 0; blk < 400; blk++) {
            UInt32 n = (blk % 3 == 0) ? 1024 : (blk % 3 == 1 ? 256 : 137);
            for (int k = 0; k < 6; k++) {
                UInt32 off = (UInt32)((blk * 97 + k * 211) % (n + (k == 5 ? 50 : 0))); // k=5 : parfois au-delà du bloc
                int note = 48 + (blk + k * 4) % 36;
                MusicDeviceMIDIEvent(au, (blk + k) % 2 ? 0x90 : 0x80, note, 100, off);
            }
            if (blk % 50 == 0) MusicDeviceMIDIEvent(au, 0xB0, 64, blk % 100 ? 0 : 127, 10);
            if (render(au, n, blk % 2, &peak) != noErr) bad++;
        }
        CHECK(bad == 0, "400 blocs rendus avec notes décalées (erreurs : %d)", bad);
        CHECK(peak < 4.0f, "son sans valeur aberrante (crête %.3f)", peak);
        // beaucoup de notes dans un seul bloc (plus que la file) : rien ne doit planter
        for (int i = 0; i < 3000; i++) MusicDeviceMIDIEvent(au, i % 2 ? 0x80 : 0x90, 40 + i % 40, 90, (UInt32)(i % 512));
        CHECK(render(au, 512, 1, &peak) == noErr, "bloc avec 3000 notes en attente");
        AudioUnitReset(au, kAudioUnitScope_Global, 0);

        // état du projet : lecture puis rechargement
        CFPropertyListRef pl = NULL;
        sz = sizeof pl;
        st = AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &sz);
        CHECK(st == noErr && pl, "état du plugin lu (sauvegarde du projet)");
        if (pl) {
            st = AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, sizeof pl);
            CHECK(st == noErr, "état du plugin rechargé (ouverture du projet)");
            CFRelease(pl);
        }

        // fenêtre du plugin : ouverture, rendu pendant qu'elle est ouverte, fermeture, réouverture
        for (int round = 1; round <= 2; round++) {
            NSWindow *win = nil;
            NSView *v = openEditor(au, &win);
            if (v) {
                for (int i = 0; i < 30; i++) { render(au, 512, 1, NULL); spin(0.02); }
                CHECK(v.window == win, "ouverture %d : la vue est dans la fenêtre", round);
                CHECK(v.layer != nil && v.layer.contents != nil, "ouverture %d : l'interface est dessinée (image présente)", round);
                /* l'image doit continuer d'être redessinée (minuterie en marche) : on compare deux images successives */
                id img1 = v.layer.contents;
                spin(0.5);
                CHECK(v.layer.contents != nil, "ouverture %d : l'interface est toujours dessinée après 0,5 s", round);
                (void)img1;
                /* vrais clics de souris dans la fenêtre, sur le clavier du bas (joue des notes) : l'interface doit répondre */
                NSDate *t0 = [NSDate date];
                for (int k = 0; k < 6; k++) {
                    NSPoint p = NSMakePoint(v.frame.size.width * (0.3 + 0.08 * k), v.frame.size.height * 0.14);
                    NSEvent *dn = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:p modifierFlags:0 timestamp:0
                                                 windowNumber:win.windowNumber context:nil eventNumber:k clickCount:1 pressure:1];
                    NSEvent *up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:p modifierFlags:0 timestamp:0
                                                 windowNumber:win.windowNumber context:nil eventNumber:k clickCount:1 pressure:0];
                    [win sendEvent:dn];
                    render(au, 512, 1, NULL);
                    spin(0.05);
                    [win sendEvent:up];
                    spin(0.05);
                }
                double dt = -[t0 timeIntervalSinceNow];
                CHECK(dt < 5.0, "ouverture %d : 6 clics traités sans blocage (%.2f s)", round, dt);
                [win close];
                win = nil;
                v = nil;
                spin(4.0); // le plugin libère son éditeur quand la vue n'est plus dans une fenêtre
                CHECK(1, "ouverture %d : fenêtre fermée sans plantage", round);
            }
        }

        /* Comme Logic Pro 13 : la MÊME vue est gardée quand on ferme la fenêtre du plugin, puis remise dans une
           nouvelle fenêtre à la réouverture (sans redemander une vue). Elle doit se redessiner et répondre aux clics. */
        {
            NSWindow *w1 = nil;
            NSView *kept = openEditor(au, &w1);
            if (kept) {
                spin(0.5);
                [w1 setContentView:[[NSView alloc] initWithFrame:kept.frame]]; /* la vue quitte sa fenêtre… */
                [w1 close];
                w1 = nil;
                CHECK(kept.window == nil, "Logic 13 : vue retirée de sa fenêtre (fenêtre du plugin fermée)");
                spin(4.5); /* … plus de 3 s : l'ancienne version détruisait l'éditeur à ce moment */
                NSWindow *w2 = [[NSWindow alloc] initWithContentRect:kept.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                             backing:NSBackingStoreBuffered defer:NO];
                w2.releasedWhenClosed = NO;
                [w2 setContentView:kept]; /* réouverture : la même vue revient */
                [w2 orderFront:nil];
                id before = kept.layer.contents;
                spin(0.6);
                CHECK(kept.window == w2 && kept.layer.contents != nil, "Logic 13 : la même vue est de nouveau affichée et dessinée");
                /* clics dans la vue revenue : doivent être traités, sans blocage ni plantage */
                NSDate *t1 = [NSDate date];
                for (int k = 0; k < 6; k++) {
                    NSPoint p = NSMakePoint(kept.frame.size.width * (0.3 + 0.08 * k), kept.frame.size.height * 0.14);
                    NSEvent *dn = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:p modifierFlags:0 timestamp:0
                                                 windowNumber:w2.windowNumber context:nil eventNumber:100 + k clickCount:1 pressure:1];
                    NSEvent *up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:p modifierFlags:0 timestamp:0
                                                 windowNumber:w2.windowNumber context:nil eventNumber:100 + k clickCount:1 pressure:0];
                    [w2 sendEvent:dn];
                    render(au, 512, 1, NULL);
                    spin(0.05);
                    [w2 sendEvent:up];
                    spin(0.05);
                }
                double dt1 = -[t1 timeIntervalSinceNow];
                CHECK(dt1 < 5.0, "Logic 13 : 6 clics dans la vue revenue, sans blocage (%.2f s)", dt1);
                spin(0.5);
                CHECK(kept.layer.contents != nil && kept.layer.contents != before, "Logic 13 : l'image se met à jour après les clics");
                [w2 close];
                spin(0.5);
            }
        }

        AudioUnitUninitialize(au);
        st = AudioComponentInstanceDispose(au);
        CHECK(st == noErr, "instance fermée");
        // deuxième instance après la première (Logic crée et détruit souvent des instances)
        st = AudioComponentInstanceNew(comp, &au);
        CHECK(st == noErr && au, "deuxième instance créée");
        if (au) {
            AudioUnitInitialize(au);
            CHECK(render(au, 512, 1, NULL) == noErr, "deuxième instance : rendu");
            AudioUnitUninitialize(au);
            AudioComponentInstanceDispose(au);
        }
        printf("\n%s (%d échec(s))\n", fails ? "TEST LOGIC : PROBLÈME" : "TEST LOGIC : TOUT VA BIEN", fails);
        return fails ? 1 : 0;
    }
}
