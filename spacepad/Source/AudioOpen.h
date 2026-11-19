// Spacepad by JB Praise — ouvrir n'importe quel fichier son.
// D'abord les lecteurs de JUCE (WAV, AIFF, FLAC, MP3, OGG, et selon le système WMA, M4A, CAF…),
// puis, si un WAV est refusé, notre propre lecteur : en-têtes inhabituels (taille 0 ou fausse, blocs en plus),
// PCM 8/16/24/32 bits, flottant 32/64 bits, A-law, µ-law, IMA ADPCM et MS ADPCM.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

namespace spacepad
{
// extensions acceptées à l'analyse d'un dossier (en minuscules, avec le point)
const juce::StringArray& audioExtensions();
juce::String audioWildcard();                 // « *.wav;*.aif;… » pour la fenêtre de choix de fichiers

// lecteur pour ce fichier, ou nullptr ; reason dit pourquoi en cas d'échec
std::unique_ptr<juce::AudioFormatReader> openAudioReader (juce::AudioFormatManager&, const juce::File&, juce::String* reason = nullptr);
void registerAllFormats (juce::AudioFormatManager&);
}
