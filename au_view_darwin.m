//go:build darwin && au

/* Classe de l'interface de l'Audio Unit, écrite en vrai Objective-C et compilée DANS le plugin.

   Logic Pro, GarageBand, MainStage et FL Studio lisent la propriété CocoaUI (le paquet + le nom de la classe),
   ouvrent le paquet puis cherchent la classe avec  [NSBundle classNamed:]. Cette recherche ne trouve que les
   classes écrites dans le fichier du plugin : une classe fabriquée en mémoire au démarrage (comme avant, depuis Go)
   est invisible, et l'hôte affiche alors sa fenêtre grise de secours à la place de l'interface.

   Le nom de la classe est propre à Layers Keys pour ne pas entrer en conflit avec un autre plugin. */

#import <AppKit/AppKit.h>
#import <AudioUnit/AudioUnit.h>
#import <AudioUnit/AUCocoaUIView.h>
#include <stdint.h>
#include "_cgo_export.h"

@interface LKAUViewFactory : NSObject <AUCocoaUIBase>
@end

@implementation LKAUViewFactory

- (unsigned)interfaceVersion {
    return 0;
}

- (NSString *)description {
    return @"Layers Keys";
}

/* l'hôte demande la vue de l'éditeur ; la taille proposée est ignorée (l'éditeur a sa propre taille) */
- (NSView *)uiViewForAudioUnit:(AudioUnit)inAudioUnit withSize:(NSSize)inPreferredSize {
    (void)inPreferredSize;
    uintptr_t v = lkAUCreateView((uintptr_t)inAudioUnit);
    /* la vue est déjà « autorelease » côté Go : l'hôte la retient lui-même */
    return (NSView *)(void *)v;
}

@end
