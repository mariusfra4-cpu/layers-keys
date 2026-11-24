#pragma once

#include <AfricanLoops/AfricanLoops.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

// Filtre biquad (formules RBJ), stéréo, sans allocation.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2]{}, z2[2]{};
    void lowShelf(double rate, double f, double gainDb);
    void highShelf(double rate, double f, double gainDb);
    void peak(double rate, double f, double q, double gainDb);
    void lowPass(double rate, double f);
    void highPass(double rate, double f);
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    inline float process(float x, int c) {
        const float y = b0 * x + z1[c];
        z1[c] = b1 * x - a1 * y + z2[c];
        z2[c] = b2 * x - a2 * y;
        return y;
    }
};

// Tous les effets d'African Loops, du son de chaque piste jusqu'à la sortie.
class FxChain {
public:
    // Réglages lus à chaque bloc (valeurs « en clair » : dB, Hz, ms, 0..1…).
    struct Settings {
        // pistes
        std::array<float, africanloops::kNumTracks> trackLow{}, trackMid{}, trackHigh{}, send{};
        // master (volume / pan / mute)
        float master = 1, masterPan = 0;
        bool masterMute = false;
        // filtres globaux et chaleur
        float lp = 1, hp = 0, warmth = 0;
        // égaliseur master
        float eqLow = 0, eqLowFreq = 80, eqLowMid = 0, eqHighMid = 0, eqHigh = 0;
        // compresseur / limiteur
        bool dynamics = false;
        float compThreshold = -14, compRatio = 3, compAttack = 8, compRelease = 120, makeup = 2, ceiling = -0.3f;
        // réverbération
        bool reverbOn = false;
        float revSize = 0.55f, revDamping = 0.5f, revWidth = 1, revPredelay = 20, revLowCut = 150, revHighCut = 8000, revLevel = 0.35f;
        // modulation
        bool modOn = false;
        int modType = 0;  // 0 chorus, 1 flanger, 2 phaser, 3 trémolo
        float modRate = 0.6f, modDepth = 0.3f, modFeedback = 0, modMix = 0.35f;
        // écho calé sur le tempo
        bool echoOn = false;
        float echoBeats = 0.75f, echoFeedback = 0.35f, echoMix = 0.2f, echoLowCut = 200;
        bool echoPingPong = true;
        double bpm = 120;  // tempo joué (pour l'écho)
    };

    void prepare(double sampleRate, int maxBlock);
    // Mixe les pistes (avec leur égaliseur et leur envoi vers la réverbération) puis
    // applique la chaîne master. Le résultat est écrit dans outL / outR.
    void process(const std::array<africanloops::Bus, africanloops::kNumTracks>& tracks,
                 const std::array<bool, africanloops::kNumTracks>& active, float* outL, float* outR, int n,
                 const Settings& s);
    float gainReduction() const { return reductionDb; }  // dB, pour l'affichage

private:
    void ensure(int n);
    double rate = 44100;
    int capacity = 0;
    std::vector<float> sendL, sendR, tmpL, tmpR;

    std::array<std::array<Biquad, 3>, africanloops::kNumTracks> trackEq{};
    std::array<std::array<float, 3>, africanloops::kNumTracks> trackEqSet{};
    std::array<Biquad, 4> masterEq{};
    std::array<float, 5> masterEqSet{};
    Biquad revLow, revHigh, echoLow;
    float revLowSet = -1, revHighSet = -1, echoLowSet = -1;

    juce::dsp::StateVariableTPTFilter<float> lowpass, highpass;
    juce::SmoothedValue<float> lpHz{20000.f}, hpHz{20.f}, warm{0.f};
    juce::dsp::Reverb reverb;
    std::vector<float> preL, preR;  // pré-délai
    int prePos = 0;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;
    double tremPhase = 0;
    std::vector<float> echoL, echoR;
    int echoPos = 0;
    juce::dsp::Compressor<float> comp;
    juce::dsp::Limiter<float> limiter;
    float reductionDb = 0;
    int lastModType = -1;
};
