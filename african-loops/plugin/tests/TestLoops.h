// Loops de test et petits outils partagés par les tests et la visite guidée.
#pragma once

#include "../PluginEditor.h"
#include "../PluginProcessor.h"

#include <cmath>
#include <random>

using namespace africanloops;

// petite loop de batterie (kick, caisse claire, charleston, conga)
inline Audio drums(double bpm, int bars, unsigned seed) {
    const int rate = 44100;
    const double six = 60 / bpm / 4;
    const int total = (int)std::lround(bars * 16 * six * rate);
    Audio a;
    a.rate = rate;
    a.L.assign((size_t)total, 0.f);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> nz(-1, 1);
    for (int s = 0; s < bars * 16; ++s) {
        int st = (int)std::lround(s * six * rate);
        auto add = [&](double dur, auto f) {
            for (int i = 0; i < (int)(dur * rate) && st + i < total; ++i) a.L[(size_t)(st + i)] += (float)f(i / (double)rate);
        };
        if (s % 8 == 0 || s % 16 == 10) add(0.4, [](double t) { return 0.8 * std::sin(2 * 3.14159 * (50 * t + 2.1 * (1 - std::exp(-t / 0.03)))) * std::exp(-t / 0.12); });
        if (s % 8 == 4) add(0.25, [&](double t) { return 0.45 * nz(rng) * std::exp(-t / 0.06) + 0.35 * std::sin(2 * 3.14159 * 190 * t) * std::exp(-t / 0.05); });
        if (s % 2 == 0) { double p1 = 0, p2 = 0; add(0.1, [&](double t) { double x = nz(rng), h = x - 2 * p1 + p2; p2 = p1; p1 = x; return 0.12 * h * std::exp(-t / 0.025); }); }
        if (s % 4 == 3) add(0.4, [](double t) { return 0.5 * std::sin(2 * 3.14159 * 330 * t) * std::exp(-t / 0.09); });
    }
    a.R = a.L;
    return a;
}

inline void writeLoop(const juce::File& f, double bpm, int bars, unsigned seed) {
    f.getParentDirectory().createDirectory();
    auto b = encodeWAV(drums(bpm, bars, seed), 0, 0);
    f.replaceWithData(b.data(), b.size());
}

template <typename Pred>
inline bool waitFor(Pred p, int ms) {
    for (int t = 0; t < ms; t += 20) {
        if (p()) return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    return p();
}


// attente fine (2 ms) pour mesurer les temps de réaction
template <typename Pred>
inline bool waitForFast(Pred p, int ms) {
    for (int t = 0; t < ms; t += 2) {
        if (p()) return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(2);
    }
    return p();
}
