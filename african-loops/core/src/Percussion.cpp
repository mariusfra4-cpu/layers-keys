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

namespace {

// Filtre passe-bas Butterworth (2e ordre), appliqué à l'aller puis au retour :
// aucune avance ni retard de phase, et une coupure plus raide.
struct Biquad {
    double b0, b1, b2, a1, a2;
};

Biquad lowpass(double fc, double fs) {
    const double w = 2 * 3.14159265358979323846 * fc / fs, c = std::cos(w), al = std::sin(w) / (2 * 0.70710678);
    const double a0 = 1 + al;
    return {(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0};
}

void filterForwardBackward(const Biquad& q, std::vector<float>& x) {
    auto pass = [&](auto begin, auto end) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto it = begin; it != end; ++it) {
            const double in = *it;
            const double y = q.b0 * in + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
            x2 = x1; x1 = in; y2 = y1; y1 = y;
            *it = (float)y;
        }
    };
    pass(x.begin(), x.end());
    pass(x.rbegin(), x.rend());
}

// Découpe un canal en 5 bandes (< 120, 120–500, 500–2500, 2500–7000, > 7000 Hz).
// Chaque bande est la différence de deux passe-bas : leur somme redonne
// exactement le signal. La loop est prolongée par son début et sa fin (elle se
// répète), pour que les filtres soient justes jusqu'aux bords.
std::array<std::vector<float>, NumBands> splitBands(const std::vector<float>& x, int rate) {
    const int n = (int)x.size(), pad = std::min(n, rate / 8);
    std::vector<float> ext((size_t)(n + 2 * pad));
    for (int i = 0; i < n + 2 * pad; ++i) ext[(size_t)i] = x[(size_t)(((i - pad) % n + n) % n)];
    static constexpr double cuts[NumBands - 1] = {120, 500, 2500, 7000};
    std::array<std::vector<float>, NumBands> bands;
    std::vector<float> prev(ext.size(), 0.f), lp(ext.size());
    for (int b = 0; b < NumBands - 1; ++b) {
        std::copy(ext.begin(), ext.end(), lp.begin());
        if (cuts[b] < rate * 0.45) filterForwardBackward(lowpass(cuts[b], rate), lp);
        bands[b].resize((size_t)n);
        for (int i = 0; i < n; ++i) bands[b][(size_t)i] = lp[(size_t)(i + pad)] - prev[(size_t)(i + pad)];
        prev.swap(lp);
    }
    bands[NumBands - 1].resize((size_t)n);
    for (int i = 0; i < n; ++i) bands[NumBands - 1][(size_t)i] = ext[(size_t)(i + pad)] - prev[(size_t)(i + pad)];
    return bands;
}

}  // namespace

// Une piste par famille trouvée. À chaque instant, chaque bande de fréquences
// est partagée entre les familles qui jouent : la somme des pistes redonne
// exactement la loop.
std::vector<Stem> separateStems(const Audio& a, const std::vector<Percussion>& percs, const std::vector<Hit>& hits) {
    std::vector<int> kinds;  // familles présentes, dans l'ordre du mixer
    for (const auto& p : percs) {
        int k = kindIndex(p.kind);
        if (k < NumKinds) kinds.push_back(k);
    }
    std::sort(kinds.begin(), kinds.end());
    kinds.erase(std::unique(kinds.begin(), kinds.end()), kinds.end());
    const int n = a.frames();
    if (kinds.empty() || n == 0) return {};
    const int S = (int)kinds.size();
    std::array<int, NumKinds> idx;
    idx.fill(-1);
    for (int i = 0; i < S; ++i) idx[kinds[i]] = i;

    // activité de chaque famille, toutes les 256 échantillons (monte au coup, puis s'éteint)
    constexpr int H = 256;
    const int F = n / H + 2;
    const double fr = (double)a.rate / H;
    static constexpr double decay[NumKinds] = {0.25, 0.18, 0.08, 0.2, 0.35};
    std::vector<std::vector<float>> act(S, std::vector<float>((size_t)F, 0.f));
    for (const auto& h : hits) {
        const int f0 = (int)std::lround(h.time * fr);
        for (const auto& kname : h.kinds) {
            int k = kindIndex(kname);
            if (k >= NumKinds || idx[k] < 0) continue;
            const double tau = decay[k] * fr;
            auto& v = act[(size_t)idx[k]];
            for (int f = std::max(0, f0 - 1); f < F && f < f0 + (int)(6 * tau); ++f)
                v[(size_t)f] = std::max(v[(size_t)f], (float)std::exp(-std::max(0, f - f0) / tau));
        }
    }
    // qui reçoit chaque bande quand personne ne joue
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
    // importance de chaque famille dans chaque bande
    static constexpr float affinity[NumKinds][NumBands] = {
        {1, 0.35f, 0.05f, 0.02f, 0.01f},   // kick
        {0.05f, 0.8f, 0.8f, 1, 0.5f},      // caisse claire
        {0.01f, 0.02f, 0.15f, 0.5f, 1},    // charleston
        {0.3f, 1, 1, 0.3f, 0.1f},          // djembé / conga
        {0.4f, 1, 0.8f, 0.2f, 0.05f},      // talking drum
    };
    // part de chaque piste dans chaque bande, à chaque pas de 256 échantillons
    std::vector<float> share((size_t)F * NumBands * S, 0.f);
    auto at = [&](int f, int b, int i) -> float& { return share[((size_t)f * NumBands + b) * S + i]; };
    for (int f = 0; f < F; ++f)
        for (int b = 0; b < NumBands; ++b) {
            float sum = 0;
            for (int i = 0; i < S; ++i) sum += at(f, b, i) = act[(size_t)i][(size_t)f] * affinity[kinds[i]][b];
            if (sum < 0.05f) {
                for (int i = 0; i < S; ++i) at(f, b, i) = 0;
                at(f, b, fallback[b]) = 1;
            } else {
                for (int i = 0; i < S; ++i) at(f, b, i) /= sum;
            }
        }

    std::vector<Stem> stems((size_t)S);
    for (int i = 0; i < S; ++i) {
        stems[(size_t)i].kind = percussionOrder()[kinds[i]];
        stems[(size_t)i].audio = std::make_shared<Audio>();
        stems[(size_t)i].audio->rate = a.rate;
        stems[(size_t)i].audio->L.assign((size_t)n, 0.f);
        stems[(size_t)i].audio->R.assign((size_t)n, 0.f);
    }
    std::vector<float*> outs((size_t)S);
    std::vector<float> w0((size_t)(S * NumBands)), dw((size_t)(S * NumBands));
    for (int c = 0; c < 2; ++c) {
        const auto bands = splitBands(c == 0 ? a.L : a.R, a.rate);
        for (int i = 0; i < S; ++i) outs[(size_t)i] = (c == 0 ? stems[(size_t)i].audio->L : stems[(size_t)i].audio->R).data();
        for (int f = 0; f * H < n; ++f) {
            // part de chaque piste dans chaque bande, interpolée entre deux pas : pas de saut audible
            for (int b = 0; b < NumBands; ++b)
                for (int i = 0; i < S; ++i) {
                    const float v0 = at(f, b, i), v1 = at(std::min(f + 1, F - 1), b, i);
                    w0[(size_t)(b * S + i)] = v0;
                    dw[(size_t)(b * S + i)] = (v1 - v0) / H;
                }
            const int beg = f * H, end = std::min(n, beg + H);
            for (int j = beg; j < end; ++j) {
                const float t = (float)(j - beg);
                for (int b = 0; b < NumBands; ++b) {
                    const float v = bands[(size_t)b][(size_t)j];
                    if (v == 0) continue;
                    const float* wb = &w0[(size_t)(b * S)];
                    const float* db = &dw[(size_t)(b * S)];
                    for (int i = 0; i < S; ++i) outs[(size_t)i][j] += v * (wb[i] + db[i] * t);
                }
            }
        }
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

std::vector<Stem> separateWith(const Audio& a, const LoopInfo& info) {
    auto stems = detail::separateStems(a, info.percussions, info.hits);
    if (stems.empty()) stems.push_back({"LOOP", std::make_shared<Audio>(a)});
    return stems;
}

LoopInfo separate(const Audio& a, const std::string& filename, std::vector<Stem>& stems) {
    detail::Analysis an;
    LoopInfo info = analyseLoop(a, filename, an);
    stems = detail::separateStems(a, info.percussions, info.hits);
    if (stems.empty()) stems.push_back({"LOOP", std::make_shared<Audio>(a)});
    return info;
}

}  // namespace africanloops
