#include "Analysis.h"
#include "Dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace lsplit
{

static constexpr double kPi = 3.14159265358979323846;

namespace
{
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // Filtre de pondération K (BS.1770), valable pour toute fréquence d'échantillonnage.
    void kWeighting (double sr, Biquad& shelf, Biquad& hp)
    {
        {
            const double G = 3.999843853973347, Q = 0.7071752369554193, fc = 1681.9744509555319;
            const double A = std::pow (10.0, G / 40.0), w0 = 2.0 * kPi * fc / sr;
            const double alpha = std::sin (w0) / (2.0 * Q), c = std::cos (w0), sA = std::sqrt (A);
            const double a0 = (A + 1) - (A - 1) * c + 2 * sA * alpha;
            shelf.b0 = A * ((A + 1) + (A - 1) * c + 2 * sA * alpha) / a0;
            shelf.b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
            shelf.b2 = A * ((A + 1) + (A - 1) * c - 2 * sA * alpha) / a0;
            shelf.a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
            shelf.a2 = ((A + 1) - (A - 1) * c - 2 * sA * alpha) / a0;
        }
        {
            const double Q = 0.5003270373253953, fc = 38.13547087613982;
            const double w0 = 2.0 * kPi * fc / sr, alpha = std::sin (w0) / (2.0 * Q), c = std::cos (w0);
            const double a0 = 1 + alpha;
            hp.b0 = (1 + c) / 2 / a0;
            hp.b1 = -(1 + c) / a0;
            hp.b2 = (1 + c) / 2 / a0;
            hp.a1 = -2 * c / a0;
            hp.a2 = (1 - alpha) / a0;
        }
    }

    std::vector<float> mono (const StereoBuffer& b)
    {
        std::vector<float> m (b.size());
        for (size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (b.l[i] + b.r[i]);
        return m;
    }

    // Spectre d'amplitude par blocs (évite de garder tout le spectrogramme en mémoire).
    template <typename Fn>
    void forEachFrame (const std::vector<float>& x, int nfft, int hop, Fn&& fn)
    {
        Stft stft (nfft, hop);
        const int framesPerBlock = 256;
        const size_t seg = (size_t) hop * (size_t) (framesPerBlock - 1);
        std::vector<std::complex<float>> spec;
        std::vector<float> mag ((size_t) stft.bins());
        std::vector<float> buf (seg);
        for (size_t start = 0; start + (size_t) nfft < x.size(); start += seg)
        {
            const size_t len = std::min (seg, x.size() - start);
            std::fill (buf.begin(), buf.end(), 0.0f);
            std::copy (x.begin() + (long) start, x.begin() + (long) (start + len), buf.begin());
            int frames = 0;
            stft.forward (buf.data(), (int) seg, spec, frames);
            // chaque bloc avance de (frames - 1) trames : la dernière trame appartient au bloc suivant
            const int valid = std::min (frames - 1, (int) ((len + (size_t) hop - 1) / (size_t) hop));
            for (int t = 0; t < valid; ++t)
            {
                for (int k = 0; k < stft.bins(); ++k)
                    mag[(size_t) k] = std::abs (spec[(size_t) t * (size_t) stft.bins() + (size_t) k]);
                fn (mag);
            }
        }
    }
}

double integratedLoudness (const StereoBuffer& b, double sr)
{
    const size_t n = b.size();
    if (n == 0 || sr <= 0) return -100.0;

    Biquad s[2], h[2];
    kWeighting (sr, s[0], h[0]);
    kWeighting (sr, s[1], h[1]);

    const size_t step = (size_t) std::max (1.0, std::round (sr * 0.1)); // 100 ms (blocs de 400 ms, 75 % de recouvrement)
    std::vector<double> sub;
    double acc = 0.0;
    size_t cnt = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double l = h[0].process (s[0].process (b.l[i]));
        const double r = h[1].process (s[1].process (b.r[i]));
        acc += l * l + r * r;
        if (++cnt == step)
        {
            sub.push_back (acc / (double) step);
            acc = 0.0;
            cnt = 0;
        }
    }
    if (sub.size() < 4) return -100.0;

    std::vector<double> blocks;
    for (size_t i = 0; i + 4 <= sub.size(); ++i)
        blocks.push_back ((sub[i] + sub[i + 1] + sub[i + 2] + sub[i + 3]) / 4.0);

    auto lufsOf = [] (double z) { return -0.691 + 10.0 * std::log10 (std::max (z, 1.0e-20)); };

    double sumAbs = 0.0;
    size_t nAbs = 0;
    for (double z : blocks)
        if (lufsOf (z) > -70.0) { sumAbs += z; ++nAbs; }
    if (nAbs == 0) return -100.0;

    const double relGate = lufsOf (sumAbs / (double) nAbs) - 10.0;
    double sumRel = 0.0;
    size_t nRel = 0;
    for (double z : blocks)
        if (lufsOf (z) > -70.0 && lufsOf (z) > relGate) { sumRel += z; ++nRel; }
    return nRel > 0 ? lufsOf (sumRel / (double) nRel) : -100.0;
}

double samplePeakDb (const StereoBuffer& b)
{
    float peak = 0.0f;
    for (size_t i = 0; i < b.size(); ++i)
        peak = std::max ({ peak, std::abs (b.l[i]), std::abs (b.r[i]) });
    return peak > 0.0f ? 20.0 * std::log10 ((double) peak) : -100.0;
}

double estimateTempo (const StereoBuffer& b, double sr, double& confidence)
{
    confidence = 0.0;
    if (b.size() < (size_t) (sr * 4)) return 0.0;

    // travail à ~22 kHz : suffisant pour les attaques et deux fois plus rapide
    const std::vector<float> m0 = mono (b);
    const double rate = 22050.0;
    const std::vector<float> m = resample (m0.data(), m0.size(), sr, rate);

    const int nfft = 1024, hop = 256;
    const double frameRate = rate / hop;
    std::vector<float> prev, env;
    forEachFrame (m, nfft, hop, [&] (const std::vector<float>& mag)
    {
        std::vector<float> lg (mag.size());
        for (size_t k = 0; k < mag.size(); ++k) lg[k] = std::log1p (100.0f * mag[k]);
        float flux = 0.0f;
        if (! prev.empty())
            for (size_t k = 1; k < lg.size(); ++k) flux += std::max (0.0f, lg[k] - prev[k]);
        env.push_back (flux);
        prev = std::move (lg);
    });
    if (env.size() < 200) return 0.0;

    // retire la tendance locale (moyenne glissante ~1 s)
    const int w = (int) frameRate;
    std::vector<double> e (env.size());
    double run = 0.0;
    for (size_t i = 0; i < env.size(); ++i)
    {
        run += env[i];
        if (i >= (size_t) w) run -= env[i - (size_t) w];
        const double mean = run / (double) std::min (i + 1, (size_t) w);
        e[i] = std::max (0.0, (double) env[i] - mean);
    }

    const int minLag = (int) std::floor (60.0 * frameRate / 200.0);
    const int maxLag = (int) std::ceil (60.0 * frameRate / 60.0);
    std::vector<double> ac ((size_t) maxLag + 2, 0.0);
    double ac0 = 0.0;
    for (double v : e) ac0 += v * v;
    if (ac0 <= 0.0) return 0.0;
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double s = 0.0;
        for (size_t i = (size_t) lag; i < e.size(); ++i) s += e[i] * e[i - (size_t) lag];
        ac[(size_t) lag] = s / ac0;
    }

    // pondération log-gaussienne autour de 120 BPM (préférence perceptive)
    int best = -1;
    double bestScore = -1.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const double bpm = 60.0 * frameRate / lag;
        const double wgt = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 1.0, 2.0));
        // renforce avec le double de la période (mesures régulières)
        double score = ac[(size_t) lag];
        if (2 * lag <= maxLag) score += 0.5 * ac[(size_t) (2 * lag)];
        score *= wgt;
        if (score > bestScore) { bestScore = score; best = lag; }
    }
    if (best < 0) return 0.0;

    // affinage : on cherche le pic de corrélation autour de 4 périodes (précision 4x meilleure)
    auto acAt = [&] (int lagV)
    {
        double sAc = 0.0;
        for (size_t i = (size_t) lagV; i < e.size(); ++i) sAc += e[i] * e[i - (size_t) lagV];
        return sAc / ac0;
    };
    double lag = best;
    {
        const int y0i = best - 1, y2i = best + 1;
        const double y0 = ac[(size_t) y0i], y1 = ac[(size_t) best], y2 = ac[(size_t) y2i];
        const double den = y0 - 2 * y1 + y2;
        if (std::abs (den) > 1.0e-12) lag += 0.5 * (y0 - y2) / den;
    }
    for (int mult : { 4, 2 })
    {
        const int centre = (int) std::lround (lag * mult);
        if (centre + mult + 1 >= (int) e.size() / 2) continue;
        int bi = centre;
        double bv = -1.0e30;
        for (int l = centre - mult; l <= centre + mult; ++l)
        {
            const double v = acAt (l);
            if (v > bv) { bv = v; bi = l; }
        }
        const double y0 = acAt (bi - 1), y1 = bv, y2 = acAt (bi + 1);
        const double den = y0 - 2 * y1 + y2;
        double refined = bi;
        if (std::abs (den) > 1.0e-12) refined += 0.5 * (y0 - y2) / den;
        lag = refined / mult;
        break;
    }

    confidence = std::clamp (ac[(size_t) best] * 2.0, 0.0, 1.0);
    double bpm = 60.0 * frameRate / lag;
    return std::round (bpm * 10.0) / 10.0;
}

void estimateKey (const StereoBuffer& b, double sr, int& root, bool& minor, double& confidence)
{
    root = -1;
    minor = false;
    confidence = 0.0;
    if (b.size() < (size_t) (sr * 2)) return;

    const std::vector<float> m0 = mono (b);
    const double rate = 11025.0; // l'harmonie utile est sous 2 kHz
    const std::vector<float> m = resample (m0.data(), m0.size(), sr, rate);

    const int nfft = 4096, hop = 2048;
    std::array<double, 12> chroma {};
    std::vector<int> pcOfBin ((size_t) (nfft / 2 + 1), -1);
    for (int k = 1; k <= nfft / 2; ++k)
    {
        const double f = k * rate / nfft;
        if (f < 55.0 || f > 2000.0) continue;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        pcOfBin[(size_t) k] = ((int) std::lround (midi) % 12 + 12) % 12;
    }
    forEachFrame (m, nfft, hop, [&] (const std::vector<float>& mag)
    {
        std::array<double, 12> c {};
        double tot = 0.0;
        for (size_t k = 0; k < mag.size(); ++k)
            if (pcOfBin[k] >= 0) { c[(size_t) pcOfBin[k]] += mag[k]; tot += mag[k]; }
        if (tot <= 1.0e-9) return;
        for (int i = 0; i < 12; ++i) chroma[(size_t) i] += c[(size_t) i] / tot; // chaque trame compte autant
    });

    // profils de Krumhansl-Kessler
    const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    const double minr[12]  = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    auto corr = [&] (const double* prof, int shift)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) i]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = chroma[(size_t) ((i + shift) % 12)] - mx, y = prof[i] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return (sxx > 0 && syy > 0) ? sxy / std::sqrt (sxx * syy) : 0.0;
    };

    double best = -2, second = -2;
    for (int r = 0; r < 12; ++r)
        for (int mode = 0; mode < 2; ++mode)
        {
            const double c = corr (mode == 0 ? major : minr, r);
            if (c > best) { second = best; best = c; root = r; minor = mode == 1; }
            else if (c > second) second = c;
        }
    confidence = std::clamp ((best - second) * 5.0 + std::max (0.0, best) * 0.5, 0.0, 1.0);
}

std::string AnalysisResult::keyName() const
{
    static const char* names[12] = { "Do", "Do#", "Ré", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    if (keyRoot < 0) return "—";
    return std::string (names[keyRoot]) + (keyMinor ? " mineur" : " majeur");
}

std::string AnalysisResult::keyShort() const
{
    static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (keyRoot < 0) return "—";
    return std::string (names[keyRoot]) + (keyMinor ? "m" : "");
}

std::string AnalysisResult::camelot() const
{
    if (keyRoot < 0) return "—";
    // Do majeur = 8B ; chaque quinte ajoute 1. La mineur = 8A.
    const int fifths = (keyRoot * 7) % 12;               // position sur le cycle des quintes depuis Do
    const int rel = keyMinor ? (keyRoot + 3) % 12 : keyRoot; // tonalité majeure relative
    const int relFifths = (rel * 7) % 12;
    const int num = ((keyMinor ? relFifths : fifths) + 7) % 12 + 1;
    return std::to_string (num) + (keyMinor ? "A" : "B");
}

} // namespace lsplit
