// Spacepad by JB Praise — reconnaissance de la tonalité d'un son :
// 1) par le nom du fichier (C, C#, Db, Em, E minor, Do, Ré, Mi mineur…)
// 2) sinon en écoutant le son (profil de notes comparé aux 24 tonalités).
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

namespace spacepad
{
struct KeyInfo
{
    int key = -1;          // 0 = C (Do) … 11 = B (Si) ; -1 = pas de tonalité
    int mode = -1;         // 0 = majeur, 1 = mineur, -1 = inconnu
    float confidence = 0;  // 0..1 (analyse du son seulement)
    bool valid() const { return key >= 0; }
};

// Noms des tonalités pour l'affichage
juce::String keyName (int key);                    // "C", "C#", "D", "Eb"…
juce::String keyNameFr (int key);                  // "Do", "Do#", "Ré", "Mib"…
juce::String keyLabel (int key, int mode);         // "C", "Em"…

// Tonalité écrite dans un nom (fichier ou dossier). mode = -1 si le nom ne dit pas majeur/mineur.
KeyInfo keyFromName (const juce::String& name);

// Le nom du dossier dit-il majeur / mineur ? (dossiers "Major", "Mineur", "Min"…) -1 sinon
int modeFromFolderName (const juce::String& name);

// Analyse du son : profil de notes (12 valeurs) puis meilleure des 24 tonalités.
struct AudioKeyResult
{
    KeyInfo key;
    float tonalStrength = 0;  // corrélation (0..1) : faible = son sans tonalité (ambiance, bruit, effet)
};
AudioKeyResult keyFromAudio (const juce::AudioBuffer<float>& mono, double sampleRate);
AudioKeyResult keyFromFile (juce::AudioFormatManager& fm, const juce::File& f, double maxSeconds = 25.0);

// Profil de notes brut (utile pour les tests)
std::array<float, 12> chromaOf (const juce::AudioBuffer<float>& mono, double sampleRate);
}
