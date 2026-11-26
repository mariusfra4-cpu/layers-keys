// Petit « faux Logic » : charge l'Audio Unit Spacepad comme le fait Logic Pro et vérifie ce qu'auval ne vérifie pas :
//   - rendu avec des notes MIDI décalées dans le bloc (Logic envoie toujours les notes avec un décalage),
//   - temps de traîne annoncé (bounce « avec la fin du son »),
//   - sauvegarde puis rechargement de l'état (projet Logic),
//   - ouverture de la fenêtre du plugin dans une vraie fenêtre, fermeture, puis réouverture.
// Construction (sur Mac) :  clang -fobjc-arc -arch arm64 -arch x86_64 tools/au_logic_test.m -o au_logic_test \
//                              -framework AudioToolbox -framework AppKit -framework CoreAudioKit
// Le plugin doit être installé (~/Library/Audio/Plug-Ins/Components). Code de sortie 0 = tout va bien.
// Repris du test de Layers Keys (« gardien » comme Logic Pro 13 : aucun signal ne doit arriver pendant le jeu).

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


/* « photo » de la vue du plugin (ce qu'elle dessine vraiment, quelle que soit sa façon de dessiner) :
   renvoie la part de pixels non vides et une empreinte pour comparer deux images */
static double viewInk(NSView *v, unsigned long long *hash) {
    NSRect r = v.bounds;
    NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:r];
    if (!rep) return 0;
    [v cacheDisplayInRect:r toBitmapImageRep:rep];
    NSInteger w = rep.pixelsWide, h = rep.pixelsHigh, n = 0, lit = 0;
    unsigned long long hh = 1469598103934665603ULL;
    for (NSInteger y = 0; y < h; y += 4)
        for (NSInteger x = 0; x < w; x += 4) {
            NSUInteger px[4] = {0, 0, 0, 0};
            [rep getPixel:px atX:x y:y];
            if (px[0] + px[1] + px[2] > 24) lit++;
            n++;
            hh = (hh ^ (px[0] | (px[1] << 8) | (px[2] << 16))) * 1099511628211ULL;
        }
    if (hash) *hash = hh;
    return n ? (double)lit / (double)n : 0;
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
        /* on rend la main au gestionnaire d'avant, pour que le plugin continue comme sans gardien */
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
    /* l'état du plugin relu puis renvoyé (comme un projet Logic qu'on ouvre), puis 12 s de jeu avec des notes */
    {
        CFPropertyListRef pl = NULL;
        UInt32 sz = sizeof pl;
        if (AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &sz) == noErr && pl) {
            AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, sizeof pl);
            CFRelease(pl);
        }
    }
    for (int k = 0; k < 120; k++) {
        int n = (k % 3 == 0) ? 36 + (k * 5) % 12 : 48 + (k * 7) % 36;   /* zone de tonalité C2-B2 + autres notes */
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

        AudioComponentDescription d = {kAudioUnitType_MusicDevice, 'Spcp', 'JBPr', 0, 0};
        AudioComponent comp = AudioComponentFindNext(NULL, &d);
        CHECK(comp != NULL, "Audio Unit « JB Praise: Spacepad » trouvée");
        if (!comp) return 1;
        UInt32 ver = 0;
        AudioComponentGetVersion(comp, &ver);
        printf("      version annoncée : %u.%u.%u\n", ver >> 16, (ver >> 8) & 0xFF, ver & 0xFF);
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
                unsigned long long h1 = 0, h2 = 0;
                double ink = viewInk(v, &h1);
                CHECK(ink > 0.2, "ouverture %d : l'interface est dessinée (%.0f %% de l'image)", round, ink * 100);
                /* l'image doit continuer d'être redessinée (animation en marche) : on compare deux images successives */
                spin(0.5);
                double ink2 = viewInk(v, &h2);
                CHECK(ink2 > 0.2 && h1 != h2, "ouverture %d : l'interface vit (image différente après 0,5 s)", round);
                /* vrais clics de souris dans la fenêtre, en bas (boutons de lecture) : l'interface doit répondre */
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
                spin(0.6);
                unsigned long long hb = 0, ha = 0;
                double inkB = viewInk(kept, &hb);
                CHECK(kept.window == w2 && inkB > 0.2, "Logic 13 : la même vue est de nouveau affichée et dessinée (%.0f %%)", inkB * 100);
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
                viewInk(kept, &ha);
                CHECK(ha != hb, "Logic 13 : l'image se met à jour après les clics");
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
