#include "Internal.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace africanloops {

const std::vector<std::string>& percussionOrder() {
    static const std::vector<std::string> order = {perc::Kick, perc::Snare, perc::Hat, perc::Hand, perc::Talking};
    return order;
}

namespace detail {

namespace {

enum Kind { KKick, KSnare, KHat, KHand, KTalking, NumKinds };

struct Features {
    BandArray rise{};      // énergie apportée par le coup, par bande
    double flatPres = 0;   // 1 = bruit (caisse claire), 0 = note pure
    double flatBody = 0;
    double glide = 0;      // glissement de la note (talking drum)
    double force = 0;
};

BandArray bandRise(const Analysis& an, int t) {
    const int T = (int)an.bands.size();
    auto at = [&](int i, int b) { return (i < 0 || i >= T) ? 0.0 : an.bands[i][b]; };
    BandArray r{};
    for (int b = 0; b < NumBands; ++b) {
        double before = 0.5 * (at(t - 4, b) + at(t - 3, b));
        double after = std::max(at(t + 1, b), at(t + 2, b));
        r[b] = std::max(0.0, after - before);
    }
    return r;
}

// 1 si le spectre est plat (bruit), proche de 0 s'il contient des notes.
double flatness(const Analysis& an, int t, double lo, double hi) {
    const auto& m = an.mag[t];
    double logSum = 0, sum = 0, n = 0;
    for (int k = 1; k < (int)m.size(); ++k) {
        double hz = binHz(k, an.rate);
        if (hz < lo || hz >= hi) continue;
        double v = m[k] + 1e-9;
        logSum += std::log(v);
        sum += v;
        n++;
    }
    if (n == 0 || sum <= 0) return 0;
    return std::exp(logSum / n) / (sum / n);
}

// De combien la note principale (60–700 Hz) glisse après le coup : la
// signature du talking drum. La mesure s'arrête au coup suivant (tranche stop).
double glide(const Analysis& an, int t, int stop) {
    const int T = (int)an.mag.size();
    stop = std::min(stop, T);
    const int span = (int)(0.25 * an.rate / kHop);
    std::vector<double> freqs;
    for (int dt = 1; dt <= span && t + dt < stop - 1; ++dt) {
        const auto& m = an.mag[t + dt];
        int bestK = 0;
        double bestV = 0, total = 0;
        for (int k = 1; k < (int)m.size(); ++k) {
            double hz = binHz(k, an.rate);
            if (hz < 60 || hz > 700) continue;
            total += m[k];
            if (m[k] > bestV) { bestK = k; bestV = m[k]; }
        }
        if (bestK == 0 || bestV < 0.15 * total) break;  // plus de note claire
        double k = bestK;
        if (bestK > 1 && bestK + 1 < (int)m.size()) {  // position fine du pic
            double a = m[bestK - 1], b = m[bestK], c = m[bestK + 1], d = a - 2 * b + c;
            if (d != 0) k += 0.5 * (a - c) / d;
        }
        freqs.push_back(k * an.rate / kFFT);
    }
    if (freqs.size() < 4) return 0;
    double first = (freqs[0] + freqs[1]) / 2;
    double last = (freqs[freqs.size() - 1] + freqs[freqs.size() - 2]) / 2;
    if (first <= 0) return 0;
    // la note doit glisser régulièrement dans un seul sens
    int up = 0, down = 0;
    for (size_t i = 1; i < freqs.size(); ++i) {
        double r = freqs[i] / freqs[i - 1];
        if (r > 1.25 || r < 0.8) return 0;  // saut brutal : pas un glissement
        if (r > 1.002) up++;
        else if (r < 0.998) down++;
    }
    double steps = (double)freqs.size() - 1;
    if (up < 0.6 * steps && down < 0.6 * steps) return 0;
    return std::abs(last - first) / first;
}

Features features(const Analysis& an, int t, int start, int stop) {
    Features h;
    const int T = (int)an.mag.size();
    int ta = std::max(0, frameAt(start + kHop / 2));  // tranche où le coup est bien présent
    h.rise = bandRise(an, ta);
    h.force = an.flux[t];
    int fr = std::min(ta + 1, T - 1);
    h.flatPres = flatness(an, fr, 2500, 9000);
    h.flatBody = flatness(an, fr, 120, 1200);
    h.glide = glide(an, ta, stop);
    return h;
}

// Quelles percussions jouent sur ce coup. Chaque bande est jugée par rapport à
// la plus forte montée de cette bande dans la loop (ref), et top évite de
// prendre un simple souffle pour un coup.
std::vector<Kind> classify(const Features& h, const BandArray& ref, double top) {
    std::vector<Kind> kinds;
    if (top <= 0) return kinds;
    std::array<bool, NumBands> on{};
    bool any = false;
    for (int b = 0; b < NumBands; ++b) {
        double rel = ref[b] > 0 ? h.rise[b] / ref[b] : 0;
        on[b] = rel >= 0.2 && h.rise[b] >= 1e-3 * top;
        any = any || on[b];
    }
    if (!any) return kinds;
    const auto& r = h.rise;
    bool kick = on[Sub] && r[Sub] >= r[Body];
    bool snare = on[Pres] && h.flatPres > 0.3 && (on[Body] || on[Mid]);
    bool hat = on[Air] && !snare && r[Air] >= 0.5 * r[Pres] && r[Body] < r[Air] + r[Pres];
    bool hand = !snare && (on[Body] || on[Mid]) && h.flatBody < 0.4 && !(kick && r[Body] < 0.7 * r[Sub]);
    bool talking = hand && h.glide > 0.12;
    if (kick) kinds.push_back(KKick);
    if (snare) kinds.push_back(KSnare);
    if (hat) kinds.push_back(KHat);
    if (talking) kinds.push_back(KTalking);
    else if (hand) kinds.push_back(KHand);
    return kinds;
}

int kindIndex(const std::string& k) {
    const auto& o = percussionOrder();
    return (int)(std::find(o.begin(), o.end(), k) - o.begin());
}

}  // namespace

std::vector<Hit> detectHits(const Analysis& an) {
    const size_t N = an.onsets.size();
    std::vector<Features> feats(N);
    BandArray ref{};
    double top = 0;
    for (size_t i = 0; i < N; ++i) {
        int stop = (int)an.mag.size();
        if (i + 1 < N) stop = frameAt(an.starts[i + 1]);
        feats[i] = features(an, an.onsets[i], an.starts[i], stop);
        for (int b = 0; b < NumBands; ++b) {
            ref[b] = std::max(ref[b], feats[i].rise[b]);
            top = std::max(top, feats[i].rise[b]);
        }
    }
    std::vector<Hit> hits;
    for (size_t i = 0; i < N; ++i) {
        auto kinds = classify(feats[i], ref, top);
        if (kinds.empty()) continue;
        Hit h;
        h.time = (double)an.starts[i] / an.rate;
        h.force = feats[i].force;
        for (Kind k : kinds) h.kinds.push_back(percussionOrder()[k]);
        hits.push_back(std::move(h));
    }
    return hits;
}

// Compte les coups par famille ; une famille doit revenir au moins 2 fois
// (et 5 % des coups) pour être retenue.
std::vector<Percussion> summarize(const std::vector<Hit>& hits) {
    std::array<int, NumKinds> count{};
    for (const auto& h : hits)
        for (const auto& k : h.kinds) count[kindIndex(k)]++;
    std::vector<Percussion> out;
    for (int k = 0; k < NumKinds; ++k) {
        int c = count[k];
        if (c >= 2 && (hits.empty() || c >= 0.05 * hits.size()))
            out.push_back({percussionOrder()[k], c, (double)c / hits.size()});
    }
    return out;
}

// Une piste par famille trouvée. Chaque petit morceau du spectre (instant ×
// fréquence) est partagé entre les familles qui jouent à cet instant dans
// cette zone : la somme des pistes redonne exactement la loop.
std::vector<Stem> separateStems(const Analysis& an, const std::vector<Percussion>& percs, const std::vector<Hit>& hits) {
    std::vector<int> kinds;  // familles présentes, dans l'ordre du mixer
    for (const auto& p : percs) kinds.push_back(kindIndex(p.kind));
    std::sort(kinds.begin(), kinds.end());
    if (kinds.empty()) return {};
    const int S = (int)kinds.size();
    const int T = (int)an.specL.size();
    const double fr = (double)an.rate / kHop;
    std::array<int, NumKinds> idx;
    idx.fill(-1);
    for (int i = 0; i < S; ++i) idx[kinds[i]] = i;

    // activité de chaque famille au cours du temps (monte au coup, puis s'éteint)
    static constexpr double decay[NumKinds] = {0.25, 0.18, 0.08, 0.2, 0.35};
    std::vector<std::vector<double>> act(S, std::vector<double>(T, 0.0));
    for (const auto& h : hits) {
        int t0 = frameAt(h.time * an.rate);
        for (const auto& kname : h.kinds) {
            int k = kindIndex(kname), i = idx[k];
            if (i < 0) continue;
            double tau = decay[k] * fr;
            for (int t = std::max(0, t0 - 1); t < T && t < t0 + (int)(6 * tau); ++t)
                act[i][t] = std::max(act[i][t], std::exp(-std::max(0, t - t0) / tau));
        }
    }
    // qui reçoit chaque zone de fréquences quand personne ne joue
    static constexpr int pref[NumBands][NumKinds] = {
        {KKick, KHand, KTalking, KSnare, KHat},  // Sub
        {KHand, KTalking, KSnare, KKick, KHat},  // Body
        {KHand, KSnare, KTalking, KHat, KKick},  // Mid
        {KSnare, KHat, KHand, KTalking, KKick},  // Pres
        {KHat, KSnare, KHand, KTalking, KKick},  // Air
    };
    std::array<int, NumBands> fallback{};
    for (int b = 0; b < NumBands; ++b)
        for (int k : pref[b])
            if (idx[k] >= 0) { fallback[b] = idx[k]; break; }
    // importance de chaque famille dans chaque zone de fréquences
    static constexpr double affinity[NumKinds][NumBands] = {
        {1, 0.35, 0.05, 0.02, 0.01},  // kick
        {0.05, 0.8, 0.8, 1, 0.5},     // caisse claire
        {0.01, 0.02, 0.15, 0.5, 1},   // charleston
        {0.3, 1, 1, 0.3, 0.1},        // djembé / conga
        {0.4, 1, 0.8, 0.2, 0.05},     // talking drum
    };
    std::vector<std::vector<Spectrum>> outL(S, std::vector<Spectrum>(T)), outR(S, std::vector<Spectrum>(T));
    std::vector<std::array<double, NumKinds>> weights(NumBands);
    for (int t = 0; t < T; ++t) {
        for (int b = 0; b < NumBands; ++b) {
            double sum = 0;
            for (int i = 0; i < S; ++i) sum += weights[b][i] = act[i][t] * affinity[kinds[i]][b];
            if (sum < 0.05) {
                weights[b].fill(0);
                weights[b][fallback[b]] = 1;
            } else {
                for (int i = 0; i < S; ++i) weights[b][i] /= sum;
            }
        }
        for (int i = 0; i < S; ++i) {
            outL[i][t].assign(kBins, {});
            outR[i][t].assign(kBins, {});
        }
        for (int k = 0; k < kBins; ++k) {
            const auto& w = weights[an.bandOfBin[k]];
            for (int i = 0; i < S; ++i) {
                if (w[i] == 0) continue;
                outL[i][t][k] = an.specL[t][k] * w[i];
                outR[i][t][k] = an.specR[t][k] * w[i];
            }
        }
    }
    std::vector<Stem> stems(S);
    for (int i = 0; i < S; ++i) {
        auto a = std::make_shared<Audio>();
        a->rate = an.rate;
        a->L = istft(outL[i], an.n);
        a->R = istft(outR[i], an.n);
        stems[i] = {percussionOrder()[kinds[i]], a};
    }
    return stems;
}

}  // namespace detail

namespace {
LoopInfo analyseLoop(const Audio& a, const std::string& filename, detail::Analysis& an) {
    an = detail::analyseSpectrum(a);
    LoopInfo info;
    info.hits = detail::detectHits(an);
    info.tempo = detail::detectTempo(a, an, info.hits, filename);
    info.seconds = a.seconds();
    info.rate = a.rate;
    info.percussions = detail::summarize(info.hits);
    for (int s : an.starts) info.slices.push_back((double)s / a.rate);
    return info;
}
}  // namespace

LoopInfo analyse(const Audio& a, const std::string& filename) {
    detail::Analysis an;
    return analyseLoop(a, filename, an);
}

LoopInfo separate(const Audio& a, const std::string& filename, std::vector<Stem>& stems) {
    detail::Analysis an;
    LoopInfo info = analyseLoop(a, filename, an);
    stems = detail::separateStems(an, info.percussions, info.hits);
    if (stems.empty()) stems.push_back({"LOOP", std::make_shared<Audio>(a)});
    return info;
}

}  // namespace africanloops
