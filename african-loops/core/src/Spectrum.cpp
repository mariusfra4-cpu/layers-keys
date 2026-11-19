#include "Internal.h"

#include <algorithm>
#include <cmath>

namespace africanloops::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;

// FFT radix 2 de taille kFFT, avec tables précalculées (inversion des bits et
// facteurs de rotation) : plusieurs fois plus rapide qu'un calcul à la volée.
struct FFTTables {
    std::vector<int> rev;
    std::vector<std::complex<double>> tw;  // exp(-2iπk/N), k < N/2
    FFTTables() : rev(kFFT), tw(kFFT / 2) {
        for (int i = 0, j = 0; i < kFFT; ++i) {
            rev[i] = j;
            int bit = kFFT >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
        }
        for (int k = 0; k < kFFT / 2; ++k) tw[k] = std::polar(1.0, -2 * kPi * k / kFFT);
    }
};
const FFTTables& tables() {
    static const FFTTables t;
    return t;
}

void fft(std::vector<std::complex<double>>& x) {
    const auto& t = tables();
    for (int i = 0; i < kFFT; ++i)
        if (i < t.rev[i]) std::swap(x[i], x[t.rev[i]]);
    for (int size = 2; size <= kFFT; size <<= 1) {
        const int half = size / 2, step = kFFT / size;
        for (int start = 0; start < kFFT; start += size)
            for (int k = 0; k < half; ++k) {
                auto& a = x[start + k];
                auto& b = x[start + k + half];
                const auto v = b * t.tw[k * step];
                b = a - v;
                a += v;
            }
    }
}

// Racine de fenêtre de Hann, à l'analyse ET à la reconstruction : la somme
// des fenêtres qui se chevauchent vaut exactement 2.
const std::vector<double>& sqrtHann() {
    static const std::vector<double> w = [] {
        std::vector<double> v(kFFT);
        for (int i = 0; i < kFFT; ++i) v[i] = std::sqrt(0.5 - 0.5 * std::cos(2 * kPi * i / kFFT));
        return v;
    }();
    return w;
}

constexpr double kEdges[NumBands + 1] = {0, 120, 500, 2500, 7000, 1e9};

// Trouve l'échantillon exact où le coup démarre (là où l'énergie monte le plus vite).
int refineOnset(const std::vector<float>& x, int center) {
    constexpr int blk = 32;
    int lo = std::max(0, center - kHop), hi = std::min(center + 5 * kHop / 2, (int)x.size() - blk);
    auto energyAt = [&](int i) {
        double s = 0;
        for (int j = i; j < i + blk && j < (int)x.size(); ++j)
            if (j >= 0) s += (double)x[j] * x[j];
        return s;
    };
    int best = center;
    double bestD = 0, prev = energyAt(lo - blk);
    for (int i = lo; i <= hi; i += blk / 2) {
        double e = energyAt(i);
        double d = std::log(e + 1e-7) - std::log(prev + 1e-7);
        if (d > bestD) { best = i; bestD = d; }
        prev = energyAt(i - blk / 2);
    }
    return std::max(0, best - blk);
}

// Pics de la courbe d'attaques, avec un seuil qui s'adapte au son autour.
std::vector<int> pickOnsets(const std::vector<double>& flux, int rate) {
    const int T = (int)flux.size();
    std::vector<int> out;
    if (T == 0) return out;
    const double fr = (double)rate / kHop;
    const int win = (int)(0.1 * fr);
    const int minGap = std::max(1, (int)(0.045 * fr));
    const double maxF = *std::max_element(flux.begin(), flux.end());
    if (maxF <= 0) return out;
    int last = -minGap - 1;
    for (int t = 0; t < T; ++t) {
        int lo = std::max(0, t - win), hi = std::min(T - 1, t + win);
        bool isMax = true;
        double sum = 0;
        for (int j = lo; j <= hi; ++j) {
            sum += flux[j];
            if (j >= t - minGap && j <= t + minGap && (flux[j] > flux[t] || (flux[j] == flux[t] && j < t)))
                isMax = false;
        }
        double mean = sum / (hi - lo + 1);
        if (isMax && flux[t] > 1.4 * mean + 0.06 * maxF && t - last >= minGap) {
            out.push_back(t);
            last = t;
        }
    }
    return out;
}

}  // namespace

int frameAt(double sample) { return (int)std::lround((sample - (kFrameStart + kFFT / 2)) / kHop); }

int bandOf(double hz) {
    for (int b = 0; b < NumBands; ++b)
        if (hz < kEdges[b + 1]) return b;
    return NumBands - 1;
}

Analysis analyseSpectrum(const Audio& a) {
    Analysis an;
    an.rate = a.rate;
    an.n = a.frames();
    an.mono = a.mono();
    const int n = an.n;
    const int T = (n - kFrameStart) / kHop + 1;
    an.bandOfBin.resize(kBins);
    for (int k = 0; k < kBins; ++k) an.bandOfBin[k] = bandOf(binHz(k, a.rate));
    an.mag.assign(T, std::vector<float>(kBins));
    an.bands.assign(T, BandArray{});
    an.bflux.assign(T, BandArray{});
    an.flux.assign(T, 0.0);
    const auto& w = sqrtHann();
    std::vector<std::complex<double>> buf(kFFT);
    std::vector<float> prev(kBins, 0.f);
    for (int t = 0; t < T; ++t) {
        const int start = t * kHop + kFrameStart;
        for (int i = 0; i < kFFT; ++i) {
            int j = start + i;
            buf[i] = {(j >= 0 && j < n) ? an.mono[j] * w[i] : 0.0, 0};
        }
        fft(buf);
        auto& m = an.mag[t];
        for (int k = 0; k < kBins; ++k) {
            m[k] = (float)std::abs(buf[k]);
            const int b = an.bandOfBin[k];
            an.bands[t][b] += (double)m[k] * m[k];
            // montée d'énergie en échelle logarithmique (comme l'oreille)
            double d = std::log1p(100.0 * m[k]) - std::log1p(100.0 * prev[k]);
            if (d > 0) an.bflux[t][b] += d;
        }
        prev = m;
    }
    // chaque bande compte autant : sinon les centaines de cases des aigus
    // (souffle, bruit) noient les notes graves des tambours
    BandArray cnt{};
    for (int k = 1; k < kBins; ++k) cnt[an.bandOfBin[k]]++;
    for (int t = 0; t < T; ++t)
        for (int b = 0; b < NumBands; ++b)
            if (cnt[b] > 0) an.flux[t] += an.bflux[t][b] / cnt[b];
    an.onsets = pickOnsets(an.flux, a.rate);
    for (int t : an.onsets) an.starts.push_back(refineOnset(an.mono, frameCenter(t)));
    return an;
}

}  // namespace africanloops::detail
