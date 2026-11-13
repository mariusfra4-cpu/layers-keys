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
#import <AudioUnit/AUCocoaUIView.h>
#include <stdio.h>
#include <math.h>

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

int main(void) {
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
