// Layers Split — données d'un morceau ouvert : mix, stems, formes d'onde, analyse.
#pragma once

#include <juce_core/juce_core.h>

#include "../Engine/Analysis.h"
#include "../Engine/Separator.h"

#include <memory>

namespace lsplit
{

struct Peaks
{
    std::vector<float> lo, hi; // min / max par colonne
    bool empty() const { return lo.empty(); }
};

Peaks computePeaks (const StereoBuffer& b, int columns = 4096);

// Tout ce qui est lu par le fil audio. Une fois publié, jamais modifié (lecture seule).
struct PlayData
{
    double sampleRate = 44100.0;
    StereoBuffer mix;
    StemSet stems;              // vide tant que la séparation n'est pas faite
    bool hasStems = false;

    Peaks mixPeaks;
    Peaks stemPeaks[numStems];

    AnalysisResult analysis;
    bool hasAnalysis = false;

    juce::File sourceFile;
    juce::File cacheDir;        // dossier des stems calculés (WAV float 32 bits)
    juce::String deviceUsed;
    double separationSeconds = 0.0;

    size_t length() const { return mix.size(); }
    double durationSec() const { return sampleRate > 0 ? (double) mix.size() / sampleRate : 0.0; }
};

using PlayDataPtr = std::shared_ptr<const PlayData>;

} // namespace lsplit
