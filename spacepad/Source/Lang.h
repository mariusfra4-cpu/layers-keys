// Spacepad by JB Praise — langues de l'interface : français, anglais, espagnol.
// Le choix est partagé par l'application, le VST3 et l'AU (fichier settings.json du dossier commun).
#pragma once
#include <juce_core/juce_core.h>

namespace spacepad
{
enum class Lang { FR = 0, EN = 1, ES = 2 };

Lang lang();                                      // langue actuelle (au premier appel : réglage enregistré, sinon langue de l'ordinateur)
void setLang (Lang);                              // change et enregistre
bool reloadLangIfChanged();                       // relit le réglage s'il a été changé ailleurs (autre fenêtre ou plugin)
juce::String L (const char* fr, const char* en, const char* es);   // textes en UTF-8
juce::String noteName (int key);                  // Do/Ré/Mi (FR), C/D/E (EN), Do/Re/Mi (ES)
juce::String modeWord (int mode);                 // majeur/mineur, major/minor, mayor/menor
}
