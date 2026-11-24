#include "Effects.h"

#include <cmath>

using namespace africanloops;

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// ------------------------------------------------------------------ biquad (RBJ)

static void normalise(Biquad& q, double b0, double b1, double b2, double a0, double a1, double a2) {
    q.b0 = (float)(b0 / a0); q.b1 = (float)(b1 / a0); q.b2 = (float)(b2 / a0);
    q.a1 = (float)(a1 / a0); q.a2 = (float)(a2 / a0);
}

void Biquad::lowShelf(double r, double f, double g) {
    const double A = std::pow(10.0, g / 40), w = 2 * kPi * f / r, c = std::cos(w), s = std::sin(w);
    const double al = s / 2 * std::sqrt(2.0), sq = 2 * std::sqrt(A) * al;
    normalise(*this, A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
              (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
}

void Biquad::highShelf(double r, double f, double g) {
    const double A = std::pow(10.0, g / 40), w = 2 * kPi * f / r, c = std::cos(w), s = std::sin(w);
    const double al = s / 2 * std::sqrt(2.0), sq = 2 * std::sqrt(A) * al;
    normalise(*this, A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
              (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
}

void Biquad::peak(double r, double f, double qv, double g) {
    const double A = std::pow(10.0, g / 40), w = 2 * kPi * f / r, c = std::cos(w), al = std::sin(w) / (2 * qv);
    normalise(*this, 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

void Biquad::lowPass(double r, double f) {
    const double w = 2 * kPi * f / r, c = std::cos(w), al = std::sin(w) / (2 * 0.70710678);
    normalise(*this, (1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}

void Biquad::highPass(double r, double f) {
    const double w = 2 * kPi * f / r, c = std::cos(w), al = std::sin(w) / (2 * 0.70710678);
    normalise(*this, (1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}

// ------------------------------------------------------------------ chaîne

void FxChain::ensure(int n) {
    if (n <= capacity) return;
    capacity = n;  // n'arrive que si l'hôte dépasse la taille de bloc annoncée
    for (auto* v : {&sendL, &sendR, &tmpL, &tmpR}) v->assign((size_t)n, 0.f);
}

void FxChain::prepare(double sampleRate, int maxBlock) {
    rate = sampleRate;
    capacity = 0;
    ensure(std::max(1, maxBlock));
    juce::dsp::ProcessSpec spec{sampleRate, (juce::uint32)std::max(1, maxBlock), 2};
    lowpass.prepare(spec);
    lowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    highpass.prepare(spec);
    highpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    lpHz.reset(sampleRate, 0.05);
    hpHz.reset(sampleRate, 0.05);
    warm.reset(sampleRate, 0.05);
    reverb.prepare(spec);
    reverb.reset();
    preL.assign((size_t)(sampleRate * 0.25) + 1, 0.f);
    preR.assign(preL.size(), 0.f);
    prePos = 0;
    chorus.prepare(spec);
    phaser.prepare(spec);
    echoL.assign((size_t)(sampleRate * 4) + 1, 0.f);  // jusqu'à 4 s d'écho
    echoR.assign(echoL.size(), 0.f);
    echoPos = 0;
    comp.prepare(spec);
    limiter.prepare(spec);
    for (auto& t : trackEq)
        for (auto& q : t) { q = Biquad{}; }
    for (auto& t : trackEqSet) t = {99, 99, 99};
    for (auto& q : masterEq) q = Biquad{};
    masterEqSet.fill(99);
    revLowSet = revHighSet = echoLowSet = -1;
    lastModType = -1;
}

void FxChain::process(const std::array<Bus, kNumTracks>& tracks, const std::array<bool, kNumTracks>& active, float* L,
                      float* R, int n, const Settings& s) {
    ensure(n);
    std::fill(L, L + n, 0.f);
    std::fill(R, R + n, 0.f);
    float* sL = sendL.data();
    float* sR = sendR.data();
    std::fill(sL, sL + n, 0.f);
    std::fill(sR, sR + n, 0.f);

    // 1. chaque piste : graves / médium / aigus, puis vers la sortie et vers la réverbération
    for (size_t t = 0; t < (size_t)kNumTracks; ++t) {
        if (!active[t] || !tracks[t].L) continue;
        float* tl = tracks[t].L;
        float* tr = tracks[t].R;
        const float lo = s.trackLow[t], mi = s.trackMid[t], hi = s.trackHigh[t];
        if (std::abs(lo) > 0.05f || std::abs(mi) > 0.05f || std::abs(hi) > 0.05f) {
            auto& eq = trackEq[t];
            auto& set = trackEqSet[t];
            if (!juce::exactlyEqual(set[0], lo)) { eq[0].lowShelf(rate, 120, lo); set[0] = lo; }
            if (!juce::exactlyEqual(set[1], mi)) { eq[1].peak(rate, 1000, 0.7, mi); set[1] = mi; }
            if (!juce::exactlyEqual(set[2], hi)) { eq[2].highShelf(rate, 6000, hi); set[2] = hi; }
            for (int i = 0; i < n; ++i) {
                float a = tl[i], b = tr[i];
                for (auto& q : eq) { a = q.process(a, 0); b = q.process(b, 1); }
                tl[i] = a;
                tr[i] = b;
            }
        }
        const float snd = s.reverbOn ? s.send[t] : 0.f;
        for (int i = 0; i < n; ++i) {
            L[i] += tl[i];
            R[i] += tr[i];
        }
        if (snd > 0.0001f)
            for (int i = 0; i < n; ++i) {
                sL[i] += tl[i] * snd;
                sR[i] += tr[i] * snd;
            }
    }

    // 2. réverbération (sur les envois) : pré-délai, réverb 100 % mouillée, coupe-bas / coupe-haut
    if (s.reverbOn) {
        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = juce::jlimit(0.f, 1.f, s.revSize);
        rp.damping = juce::jlimit(0.f, 1.f, s.revDamping);
        rp.width = juce::jlimit(0.f, 1.f, s.revWidth);
        rp.wetLevel = 1;
        rp.dryLevel = 0;
        reverb.setParameters(rp);
        const int pre = juce::jlimit(0, (int)preL.size() - 1, (int)std::lround(s.revPredelay * 0.001 * rate));
        const int size = (int)preL.size();
        for (int i = 0; i < n; ++i) {
            const int rd = (prePos - pre + size) % size;
            preL[(size_t)prePos] = sL[i];
            preR[(size_t)prePos] = sR[i];
            sL[i] = preL[(size_t)rd];
            sR[i] = preR[(size_t)rd];
            prePos = (prePos + 1) % size;
        }
        float* ch[2] = {sL, sR};
        juce::dsp::AudioBlock<float> blk(ch, 2, (size_t)n);
        reverb.process(juce::dsp::ProcessContextReplacing<float>(blk));
        if (!juce::exactlyEqual(revLowSet, s.revLowCut)) { revLow.highPass(rate, juce::jlimit(20.f, 2000.f, s.revLowCut)); revLowSet = s.revLowCut; }
        if (!juce::exactlyEqual(revHighSet, s.revHighCut)) { revHigh.lowPass(rate, juce::jlimit(1000.f, (float)(rate * 0.45), s.revHighCut)); revHighSet = s.revHighCut; }
        for (int i = 0; i < n; ++i) {
            L[i] += revHigh.process(revLow.process(sL[i], 0), 0) * s.revLevel;
            R[i] += revHigh.process(revLow.process(sR[i], 1), 1) * s.revLevel;
        }
    }

    // 3. volume et pan du master
    {
        const float g = s.masterMute ? 0.f : s.master;
        const float a = (juce::jlimit(-1.f, 1.f, s.masterPan) + 1) * juce::MathConstants<float>::halfPi / 2;
        const float gl = g * std::cos(a) * juce::MathConstants<float>::sqrt2, gr = g * std::sin(a) * juce::MathConstants<float>::sqrt2;
        for (int i = 0; i < n; ++i) { L[i] *= gl; R[i] *= gr; }
    }

    float* ch[2] = {L, R};
    juce::dsp::AudioBlock<float> block(ch, 2, (size_t)n);
    juce::dsp::ProcessContextReplacing<float> ctx(block);

    // 4. filtres globaux et chaleur
    auto lpToHz = [](float x) { return 200.f * std::pow(100.f, juce::jlimit(0.f, 1.f, x)); };
    auto hpToHz = [](float x) { return 20.f * std::pow(100.f, juce::jlimit(0.f, 1.f, x)); };
    lpHz.setTargetValue(lpToHz(s.lp));
    hpHz.setTargetValue(hpToHz(s.hp));
    lpHz.skip(n);
    hpHz.skip(n);
    if (s.lp < 0.999f || lpHz.isSmoothing()) {
        lowpass.setCutoffFrequency(juce::jlimit(20.f, (float)(rate * 0.45), lpHz.getCurrentValue()));
        lowpass.process(ctx);
    }
    if (s.hp > 0.001f || hpHz.isSmoothing()) {
        highpass.setCutoffFrequency(juce::jlimit(10.f, (float)(rate * 0.45), hpHz.getCurrentValue()));
        highpass.process(ctx);
    }
    warm.setTargetValue(s.warmth);
    if (warm.getTargetValue() > 0.001f || warm.isSmoothing()) {
        for (int i = 0; i < n; ++i) {
            const float w = warm.getNextValue(), drive = 1.f + 3.f * w, norm = 1.f / std::tanh(drive);
            L[i] += w * (std::tanh(drive * L[i]) * norm - L[i]);
            R[i] += w * (std::tanh(drive * R[i]) * norm - R[i]);
        }
    } else {
        warm.skip(n);
    }

    // 5. égaliseur master 4 bandes
    if (std::abs(s.eqLow) > 0.05f || std::abs(s.eqLowMid) > 0.05f || std::abs(s.eqHighMid) > 0.05f || std::abs(s.eqHigh) > 0.05f) {
        if (!juce::exactlyEqual(masterEqSet[0], s.eqLow) || !juce::exactlyEqual(masterEqSet[4], s.eqLowFreq)) {
            masterEq[0].lowShelf(rate, juce::jlimit(30.f, 400.f, s.eqLowFreq), s.eqLow);
            masterEqSet[0] = s.eqLow;
            masterEqSet[4] = s.eqLowFreq;
        }
        if (!juce::exactlyEqual(masterEqSet[1], s.eqLowMid)) { masterEq[1].peak(rate, 300, 0.9, s.eqLowMid); masterEqSet[1] = s.eqLowMid; }
        if (!juce::exactlyEqual(masterEqSet[2], s.eqHighMid)) { masterEq[2].peak(rate, 2500, 0.9, s.eqHighMid); masterEqSet[2] = s.eqHighMid; }
        if (!juce::exactlyEqual(masterEqSet[3], s.eqHigh)) { masterEq[3].highShelf(rate, 10000, s.eqHigh); masterEqSet[3] = s.eqHigh; }
        for (int i = 0; i < n; ++i) {
            float a = L[i], b = R[i];
            for (auto& q : masterEq) { a = q.process(a, 0); b = q.process(b, 1); }
            L[i] = a;
            R[i] = b;
        }
    }

    // 6. modulation
    if (s.modOn) {
        if (lastModType != s.modType) {
            chorus.reset();
            phaser.reset();
            lastModType = s.modType;
        }
        switch (s.modType) {
            case 0:  // chorus
            case 1:  // flanger : retard très court et retour
                chorus.setRate(juce::jlimit(0.01f, 20.f, s.modRate));
                chorus.setDepth(juce::jlimit(0.f, 1.f, s.modDepth));
                chorus.setCentreDelay(s.modType == 0 ? 7.f : 1.5f);
                chorus.setFeedback(juce::jlimit(-0.95f, 0.95f, s.modType == 0 ? s.modFeedback * 0.5f : s.modFeedback));
                chorus.setMix(juce::jlimit(0.f, 1.f, s.modMix));
                chorus.process(ctx);
                break;
            case 2:  // phaser
                phaser.setRate(juce::jlimit(0.01f, 20.f, s.modRate));
                phaser.setDepth(juce::jlimit(0.f, 1.f, s.modDepth));
                phaser.setCentreFrequency(1300.f);
                phaser.setFeedback(juce::jlimit(-0.95f, 0.95f, s.modFeedback));
                phaser.setMix(juce::jlimit(0.f, 1.f, s.modMix));
                phaser.process(ctx);
                break;
            default: {  // trémolo
                const double inc = 2 * kPi * juce::jlimit(0.01f, 20.f, s.modRate) / rate;
                for (int i = 0; i < n; ++i) {
                    const float lfo = (float)(0.5 + 0.5 * std::sin(tremPhase));
                    const float g = 1.f - juce::jlimit(0.f, 1.f, s.modDepth) * lfo * juce::jlimit(0.f, 1.f, s.modMix * 2);
                    L[i] *= g;
                    R[i] *= g;
                    tremPhase += inc;
                    if (tremPhase > 2 * kPi) tremPhase -= 2 * kPi;
                }
            }
        }
    }

    // 7. écho calé sur le tempo (ping-pong possible)
    if (s.echoOn) {
        const int size = (int)echoL.size();
        const double secs = s.echoBeats * 60.0 / std::max(20.0, s.bpm);
        const int dly = juce::jlimit(1, size - 1, (int)std::lround(secs * rate));
        if (!juce::exactlyEqual(echoLowSet, s.echoLowCut)) { echoLow.highPass(rate, juce::jlimit(20.f, 2000.f, s.echoLowCut)); echoLowSet = s.echoLowCut; }
        const float fb = juce::jlimit(0.f, 0.95f, s.echoFeedback), mix = juce::jlimit(0.f, 1.f, s.echoMix);
        for (int i = 0; i < n; ++i) {
            const int rd = (echoPos - dly + size) % size;
            const float dl = echoL[(size_t)rd], dr = echoR[(size_t)rd];
            const float inL = echoLow.process(L[i], 0), inR = echoLow.process(R[i], 1);
            if (s.echoPingPong) {
                echoL[(size_t)echoPos] = 0.5f * (inL + inR) + dr * fb;  // l'écho passe d'un côté à l'autre
                echoR[(size_t)echoPos] = dl * fb;
            } else {
                echoL[(size_t)echoPos] = inL + dl * fb;
                echoR[(size_t)echoPos] = inR + dr * fb;
            }
            L[i] += dl * mix;
            R[i] += dr * mix;
            echoPos = (echoPos + 1) % size;
        }
    }

    // 8. compresseur + limiteur
    if (s.dynamics) {
        comp.setThreshold(juce::jlimit(-60.f, 0.f, s.compThreshold));
        comp.setRatio(juce::jlimit(1.f, 20.f, s.compRatio));
        comp.setAttack(juce::jlimit(0.1f, 200.f, s.compAttack));
        comp.setRelease(juce::jlimit(5.f, 1000.f, s.compRelease));
        float before = 0, after = 0;
        for (int i = 0; i < n; ++i) before = std::max(before, std::max(std::abs(L[i]), std::abs(R[i])));
        comp.process(ctx);
        for (int i = 0; i < n; ++i) after = std::max(after, std::max(std::abs(L[i]), std::abs(R[i])));
        reductionDb = before > 1e-5f ? juce::jlimit(0.f, 40.f, juce::Decibels::gainToDecibels(before) - juce::Decibels::gainToDecibels(after)) : 0.f;
        block.multiplyBy(juce::Decibels::decibelsToGain(juce::jlimit(-12.f, 18.f, s.makeup)));
        limiter.setThreshold(juce::jlimit(-12.f, 0.f, s.ceiling));
        limiter.setRelease(80.f);
        limiter.process(ctx);
    } else {
        reductionDb = 0;
    }
}
