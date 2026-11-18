#include "Internal.h"

#include <algorithm>
#include <cmath>

namespace africanloops::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;

// FFT radix 2 sur place.
void fft(std::vector<std::complex<double>>& x, bool inverse) {
    const int n = (int)x.size();
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    const double sign = inverse ? 1.0 : -1.0;
    for (int size = 2; size <= n; size <<= 1) {
        const std::complex<double> w = std::polar(1.0, sign * 2 * kPi / size);
        for (int start = 0; start < n; start += size) {
            std::complex<double> wk(1, 0);
            for (int k = 0; k < size / 2; ++k) {
                auto a = x[start + k], b = x[start + k + size / 2] * wk;
                x[start + k] = a + b;
                x[start + k + size / 2] = a - b;
                wk *= w;
            }
        }
    }
    if (inverse)
        for (auto& v : x) v /= (double)n;
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

std::vector<Spectrum> stft(const std::vector<float>& x) {
    const int n = (int)x.size();
    const int frames = (n - kFrameStart) / kHop + 1;
    const auto& w = sqrtHann();
    std::vector<Spectrum> out(frames);
    std::vector<std::complex<double>> buf(kFFT);
    for (int t = 0; t < frames; ++t) {
        const int start = t * kHop + kFrameStart;
        for (int i = 0; i < kFFT; ++i) {
            int j = start + i;
            double v = (j >= 0 && j < n) ? x[j] : 0.0;
            buf[i] = {v * w[i], 0};
        }
        fft(buf, false);
        out[t].assign(buf.begin(), buf.begin() + kBins);
    }
    return out;
}

std::vector<float> istft(const std::vector<Spectrum>& spec, int n) {
    std::vector<double> acc(n, 0.0);
    const auto& w = sqrtHann();
    std::vector<std::complex<double>> buf(kFFT);
    for (size_t t = 0; t < spec.size(); ++t) {
        std::copy(spec[t].begin(), spec[t].end(), buf.begin());
        for (int k = 1; k < kFFT / 2; ++k) buf[kFFT - k] = std::conj(spec[t][k]);
        fft(buf, true);
        const int start = (int)t * kHop + kFrameStart;
        for (int i = 0; i < kFFT; ++i) {
            int j = start + i;
            if (j >= 0 && j < n) acc[j] += buf[i].real() * w[i];
        }
    }
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i) out[i] = (float)(acc[i] / 2);
    return out;
}

Analysis analyseSpectrum(const Audio& a) {
    Analysis an;
    an.rate = a.rate;
    an.n = a.frames();
    an.specL = stft(a.L);
    an.specR = stft(a.R);
    const int T = (int)an.specL.size();
    an.bandOfBin.resize(kBins);
    for (int k = 0; k < kBins; ++k) an.bandOfBin[k] = bandOf(binHz(k, a.rate));
    an.mag.assign(T, std::vector<double>(kBins));
    an.bands.assign(T, BandArray{});
    an.bflux.assign(T, BandArray{});
    an.flux.assign(T, 0.0);
    std::vector<double> prev(kBins, 0.0);
    for (int t = 0; t < T; ++t) {
        auto& m = an.mag[t];
        for (int k = 0; k < kBins; ++k) {
            m[k] = std::abs(an.specL[t][k] + an.specR[t][k]) * 0.5;
            const int b = an.bandOfBin[k];
            an.bands[t][b] += m[k] * m[k];
            // montée d'énergie en échelle logarithmique (comme l'oreille)
            double d = std::log1p(100 * m[k]) - std::log1p(100 * prev[k]);
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
    an.mono = a.mono();
    for (int t : an.onsets) an.starts.push_back(refineOnset(an.mono, frameCenter(t)));
    return an;
}

}  // namespace africanloops::detail
