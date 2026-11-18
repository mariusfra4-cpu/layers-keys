// Spacepad by JB Praise — réglages de l'interface partagés par l'application, le VST3 et l'AU
// (fichier settings.json du dossier commun) : langue, couleurs, thème, fond animé, visualiseur…
#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace spacepad
{
enum class Lang { FR = 0, EN = 1, ES = 2 };

Lang lang();                                      // langue actuelle (au premier appel : réglage enregistré, sinon langue de l'ordinateur)
void setLang (Lang);                              // change et enregistre
bool reloadLangIfChanged();                       // relit les réglages s'ils ont été changés ailleurs (autre fenêtre ou plugin)
juce::String L (const char* fr, const char* en, const char* es);   // textes en UTF-8
juce::String noteName (int key);                  // Do/Ré/Mi (FR), C/D/E (EN), Do/Re/Mi (ES)
juce::String solfege (int key);                   // Do Ré Mi (Do Re Mi en espagnol), quelle que soit la langue
juce::String modeWord (int mode);                 // majeur/mineur, major/minor, mayor/menor

// Personnalisation (fenêtre Paramètres)
struct UiSettings
{
    int accent = 0;          // 0 bleu, 1 violet, 2 or, 3 vert, 4 rose, 5 orange, 6 blanc
    int theme = 0;           // 0 Nuit, 1 Minuit bleu, 2 Graphite, 3 Clair
    int background = 0;      // 0 Galaxie, 1 Tourbillon d'étoiles, 2 Aucun
    float brightness = 0.5f; // luminosité de l'animation (0..1)
    float speed = 0.5f;      // vitesse de rotation (0..1)
    int viz = 0;             // visualiseur : 0 barres + bulles, 1 barres, 2 onde, 3 masqué
    bool solfegeNames = false; // noms des notes : C D E (faux) ou Do Ré Mi (vrai)
    int scale = 100;         // taille de l'interface : 90, 100, 125, 150 (%)
    bool animations = true;  // animations (désactiver pour les petits ordinateurs)
    bool keepAwake = true;   // garder l'écran allumé pendant le jeu
    // MIDI : quelles notes changent la tonalité
    int midiKeyMode = 0;     // 0 zone de commande (une octave), 1 jamais, 2 toutes les notes
    int zoneLow = 36;        // première note de la zone (36 = Do le plus grave d'un clavier 61 touches)
    // pédales et boutons MIDI assignés (une action par case ; -1 = rien ; 1000+n = CC n ; 2000+n = note n)
    std::array<int, 11> midiMap { { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
    float fadeSeconds = 8.0f; // durée de « Fin du chant »
    bool copySounds = false;  // copier les sons dans Spacepad à l'enregistrement
    int centerView = 0;       // centre de l'écran : 0 PAD (galaxie et tonalités), 1 CROWD (grande salle et crowds)
    int fxPage = 0;           // panneau des effets : 0 PAD, 1 CROWD
    int crowdBg = 0;          // fond du mode CROWD : 0 photo douce, 1 photo naturelle, 2 photo teinte Spacepad, 3 salle dessinée
};

// actions assignables à une pédale ou un bouton MIDI
enum MidiAction { actPlayStop = 0, actNextSong, actPrevSong, actModeToggle, actFreeze, actSwell, actFadeEnd, actStopAll, actPadVolume, actCrowdVolume, actMix, actCount };
juce::String midiActionName (int action);
juce::String midiCodeName (int code);            // « CC 64 », « Note C2 », « Non assigné »
UiSettings uiSettings();
void setUiSettings (const UiSettings&);           // change et enregistre
int settingsVersion();                            // augmente à chaque changement (pour redessiner)
}
