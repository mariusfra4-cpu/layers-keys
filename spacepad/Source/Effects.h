// Spacepad by JB Praise — effets : Low cut, Warm, Motion, Air, Width, Sub, Shimmer, Reverb, Freeze, Swell.
#pragma once
#include <juce_dsp/juce_dsp.h>

namespace spacepad
{
struct FxParams
{
    float shimmer = 0;   // 0..1 : couche une octave plus haut envoyée dans la réverbération
    float warm = 0;      // 0..1 : saturation douce + aigus adoucis
    float air = 0;       // 0..1 : brillance (+0..+9 dB au-dessus de 8 kHz)
    float reverb = 0;    // 0..1 : quantité et longueur de la réverbération
    float motion = 0;    // 0..1 : filtre qui respire lentement
    float width = 0.5f;  // 0..1 : 0 = mono, 0.5 = normal, 1 = très large
    float sub = 0;       // 0..1 : couche une octave plus bas
    float lowCut = 0;    // 0..1 : coupe-bas de 20 Hz à 500 Hz
    bool freeze = false; // réverbération figée à l'infini
    bool swell = false;  // le volume monte et redescend doucement
};

class FxChain
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    // main : son principal (stéréo, modifié sur place) ; sub, shim : couches grave et aiguë du moteur
    void process (juce::AudioBuffer<float>& main, juce::AudioBuffer<float>& sub, juce::AudioBuffer<float>& shim, int num, const FxParams& p);

private:
    double sr = 48000;
    juce::dsp::StateVariableTPTFilter<float> lowCut, motionLp, warmLp;
    juce::dsp::IIR::Filter<float> airL, airR;
    juce::dsp::Reverb reverb;
    juce::AudioBuffer<float> wet;
    juce::SmoothedValue<float> sWarm, sAir, sSub, sShim, sRev, sWidth, sMotion;
    double lfoPhase = 0, swellPhase = 0;
    float lastAirDb = -100, lastLowCut = -1;
    juce::SmoothedValue<float> swellGain;
};
}
