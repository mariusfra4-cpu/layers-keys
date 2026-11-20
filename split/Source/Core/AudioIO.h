// Layers Split — lecture des fichiers audio, écriture des stems, cache, emplacements.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "Project.h"

namespace lsplit
{

// Dossier contenant onnxruntime + DirectML et le sous-dossier « models ».
juce::File findRuntimeDir();
juce::File modelsDirFor (const juce::File& runtimeDir);

// Dossier des réglages et du cache : %APPDATA%\LayersSplit
juce::File userDataDir();

juce::String audioFileWildcard();
bool isSupportedAudioFile (const juce::File& f);

// Décode un fichier en stéréo float. Faux + message en cas d'erreur.
bool loadAudioFile (const juce::File& f, StereoBuffer& out, double& sampleRate, juce::String& error,
                    double maxSeconds = 30.0 * 60.0);

// Écrit un WAV (bits = 16, 24 ou 32 = float).
bool writeWav (const juce::File& f, const StereoBuffer& b, double sampleRate, int bits, juce::String& error);

// Clé de cache : fichier source (chemin, taille, date) + réglages de séparation.
juce::String cacheKeyFor (const juce::File& source, const SeparationSettings& s);
juce::File cacheDirFor (const juce::String& key);
bool cacheIsComplete (const juce::File& dir);
bool saveToCache (const juce::File& dir, const PlayData& data, juce::String& error);
bool loadFromCache (const juce::File& dir, PlayData& data, juce::String& error);
void pruneCache (int keepMostRecent = 12);

juce::String formatTime (double seconds, bool withMillis = false);

} // namespace lsplit
