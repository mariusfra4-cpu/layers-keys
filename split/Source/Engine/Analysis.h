// Layers Split — analyse musicale : tempo, tonalité, loudness (EBU R128 / BS.1770), crête.
#pragma once

#include "MdxModel.h"

#include <string>

namespace lsplit
{

struct AnalysisResult
{
    double bpm = 0.0;            // 0 si inconnu
    double bpmConfidence = 0.0;  // 0..1
    int keyRoot = -1;            // 0 = Do ... 11 = Si, -1 si inconnu
    bool keyMinor = false;
    double keyConfidence = 0.0;  // 0..1
    double lufs = -100.0;        // loudness intégré
    double peakDb = -100.0;      // crête échantillon (dBFS)
    double durationSec = 0.0;

    std::string keyName() const;     // « La mineur »
    std::string keyShort() const;    // « Am »
    std::string camelot() const;     // « 8A »
};

double integratedLoudness (const StereoBuffer& b, double sr);
double samplePeakDb (const StereoBuffer& b);
// Tempo à partir d'un signal rythmique (idéalement le stem batterie).
double estimateTempo (const StereoBuffer& b, double sr, double& confidence);
// Tonalité à partir d'un signal harmonique (idéalement mix sans la batterie).
void estimateKey (const StereoBuffer& b, double sr, int& root, bool& minor, double& confidence);

} // namespace lsplit
