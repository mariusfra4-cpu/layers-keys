// Spacepad by JB Praise — chorus, delay au tempo, et la chaîne d'effets propre à chaque crowd.
#pragma once
#include <juce_dsp/juce_dsp.h>

namespace spacepad
{
// divisions du delay (en noires) : 1/2, 1/4, 1/8 pointée, 1/8, 1/4 triolet, 1/16
float delayDivision (int index);
juce::StringArray delayDivisionNames();

// Chorus stéréo : deux lignes à retard modulées (gauche et droite décalées) qui épaississent et élargissent
class StereoChorus
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, int num, float amount);
private:
    double sr = 48000;
    std::vector<float> bufL, bufR;
    int write = 0;
    double phase = 0;
    juce::SmoothedValue<float> amt;
    float read (const std::vector<float>& b, float delaySamples) const;
};

// Delay au tempo : répétitions qui rebondissent de gauche à droite, adoucies à chaque passage
class TempoDelay
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, int num, float seconds, float feedback, float mix);
private:
    double sr = 48000;
    std::vector<float> bufL, bufR;
    int write = 0;
    juce::SmoothedValue<float> time, sMix, sFb;
    float dampL = 0, dampR = 0;
    float read (const std::vector<float>& b, float delaySamples) const;
};

struct CrowdFxParams
{
    float chorus = 0, delay = 0, delaySeconds = 0.8f, feedback = 0.35f;
    float reverb = 0.3f, width = 0.5f, distance = 0, movement = 0, lowCut = 0.1f;
};

// effets d'un crowd : Low cut, Distance, Chorus, Delay, Mouvement, Reverb, Width
class CrowdFx
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (juce::AudioBuffer<float>& buf, int num, const CrowdFxParams& p);
private:
    double sr = 48000;
    juce::dsp::StateVariableTPTFilter<float> lowCut, far;
    StereoChorus chorus;
    TempoDelay delay;
    juce::dsp::Reverb reverb;
    juce::AudioBuffer<float> wet;
    juce::SmoothedValue<float> sDist, sRev, sWidth, sMove;
    double panPhase = 0;
    float lastLowCut = -1, lastFar = -1;
};
}
