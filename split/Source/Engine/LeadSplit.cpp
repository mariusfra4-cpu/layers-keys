#include "LeadSplit.h"
#include "Dsp.h"

#include <algorithm>
#include <cmath>

namespace lsplit
{

namespace
{
    constexpr int kNfft = 4096, kHop = 1024;
    constexpr int kHarmonics = 12;

    float interpMag (const float* m, int nb, double bin)
    {
        const int i = (int) bin;
        if (i < 0 || i + 1 >= nb) return 0.0f;
        const float f = (float) (bin - i);
        return m[i] * (1.0f - f) + m[i + 1] * f;
    }

    template <typename T>
    T median (std::vector<T> v)
    {
        if (v.empty()) return T();
        std::nth_element (v.begin(), v.begin() + (long) (v.size() / 2), v.end());
        return v[v.size() / 2];
    }

    // Traite un bloc : x (2 canaux, longueur multiple de kHop) -> lead
    void processBlock (const std::vector<float>& xl, const std::vector<float>& xr, double sr,
                       const LeadSplitSettings& s, std::vector<float>& ol, std::vector<float>& orr)
    {
        const int len = (int) xl.size();
        Stft stft (kNfft, kHop);
        std::vector<std::complex<float>> L, R;
        int frames = 0;
        stft.forward (xl.data(), len, L, frames);
        stft.forward (xr.data(), len, R, frames);
        const int nb = stft.bins();
        const double binHz = sr / kNfft;
        const int maxBin = std::min (nb - 2, (int) (7000.0 / binHz));
        const int minBin = std::max (1, (int) (90.0 / binHz));

        // spectre d'amplitude du centre (compressé pour la recherche de hauteur)
        std::vector<float> mag ((size_t) frames * (size_t) nb), comp ((size_t) frames * (size_t) nb);
        std::vector<double> energy ((size_t) frames, 0.0);
        double maxEnergy = 0.0;
        for (int t = 0; t < frames; ++t)
        {
            double e = 0.0;
            for (int k = 0; k < nb; ++k)
            {
                const size_t i = (size_t) t * (size_t) nb + (size_t) k;
                const float a = 0.5f * std::abs (L[i] + R[i]);
                mag[i] = a;
                comp[i] = std::sqrt (a);
                if (k >= minBin && k <= maxBin) e += (double) a * a;
            }
            energy[(size_t) t] = e;
            maxEnergy = std::max (maxEnergy, e);
        }

        // candidats de fondamentale : grille logarithmique au dixième de demi-ton
        std::vector<double> cand;
        for (double f = s.minHz; f <= s.maxHz; f *= std::pow (2.0, 1.0 / 120.0))
            cand.push_back (f);
        double wts[kHarmonics];
        for (int h = 0; h < kHarmonics; ++h) wts[h] = std::pow (0.84, h);

        std::vector<double> f0 ((size_t) frames, 0.0), ratio ((size_t) frames, 0.0);
        for (int t = 0; t < frames; ++t)
        {
            if (energy[(size_t) t] < maxEnergy * 1.0e-5 || energy[(size_t) t] <= 0.0) continue;
            const float* c = comp.data() + (size_t) t * (size_t) nb;
            double best = -1.0;
            size_t bi = 0;
            for (size_t j = 0; j < cand.size(); ++j)
            {
                double sal = 0.0;
                for (int h = 1; h <= kHarmonics; ++h)
                {
                    const double b = cand[j] * h / binHz;
                    if (b >= maxBin) break;
                    sal += wts[h - 1] * interpMag (c, nb, b);
                }
                if (sal > best) { best = sal; bi = j; }
            }
            f0[(size_t) t] = cand[bi];

            // part d'énergie portée par les harmoniques de cette note
            const float* m = mag.data() + (size_t) t * (size_t) nb;
            double el = 0.0;
            for (int h = 1; h <= kHarmonics; ++h)
            {
                const double b = cand[bi] * h / binHz;
                if (b >= maxBin) break;
                const int k0 = (int) std::floor (b - 1.5), k1 = (int) std::ceil (b + 1.5);
                for (int k = std::max (minBin, k0); k <= std::min (maxBin, k1); ++k)
                    el += (double) m[k] * m[k];
            }
            ratio[(size_t) t] = el / energy[(size_t) t];
        }

        // lissage : la hauteur (médiane sur 5 trames) et la décision « solo » (médiane sur 9 trames)
        std::vector<double> f0s ((size_t) frames, 0.0), rs ((size_t) frames, 0.0);
        for (int t = 0; t < frames; ++t)
        {
            std::vector<double> a, r;
            for (int d = -4; d <= 4; ++d)
            {
                const int u = t + d;
                if (u < 0 || u >= frames) continue;
                r.push_back (ratio[(size_t) u]);
                if (std::abs (d) <= 2 && f0[(size_t) u] > 0) a.push_back (std::log (f0[(size_t) u]));
            }
            rs[(size_t) t] = median (r);
            // garde la hauteur de la trame si elle est proche de la médiane, sinon la médiane (erreurs d'octave)
            if (! a.empty() && f0[(size_t) t] > 0)
            {
                const double md = median (a);
                f0s[(size_t) t] = std::abs (std::log (f0[(size_t) t]) - md) < 0.03 ? f0[(size_t) t] : std::exp (md);
            }
        }

        // masque doux des harmoniques
        std::vector<std::complex<float>> OL ((size_t) frames * (size_t) nb, { 0.0f, 0.0f }), OR = OL;
        std::vector<float> mask ((size_t) nb);
        for (int t = 0; t < frames; ++t)
        {
            const double v = std::clamp ((rs[(size_t) t] - s.threshold) / 0.12, 0.0, 1.0);
            if (v <= 0.0 || f0s[(size_t) t] <= 0.0) continue;
            std::fill (mask.begin(), mask.end(), 0.0f);
            for (int h = 1; h <= kHarmonics; ++h)
            {
                const double b = f0s[(size_t) t] * h / binHz;
                if (b >= maxBin) break;
                const double sigma = 0.9 + 0.06 * h; // un peu plus large en haut (vibrato, bends)
                const int k0 = (int) std::floor (b - 3 * sigma), k1 = (int) std::ceil (b + 3 * sigma);
                for (int k = std::max (1, k0); k <= std::min (maxBin, k1); ++k)
                {
                    const double d = (k - b) / sigma;
                    mask[(size_t) k] = std::max (mask[(size_t) k], (float) std::exp (-0.5 * d * d));
                }
            }
            for (int k = 0; k < nb; ++k)
            {
                const float g = (float) v * mask[(size_t) k];
                if (g <= 0.0f) continue;
                const size_t i = (size_t) t * (size_t) nb + (size_t) k;
                OL[i] = L[i] * g;
                OR[i] = R[i] * g;
            }
        }

        ol.assign ((size_t) len, 0.0f);
        orr.assign ((size_t) len, 0.0f);
        stft.inverse (OL, frames, ol.data());
        stft.inverse (OR, frames, orr.data());
    }
}

bool splitLead (const StereoBuffer& in, double sr, StereoBuffer& lead,
                const std::function<bool (double)>& progress, const LeadSplitSettings& s)
{
    const size_t n = in.size();
    lead.resize (n);
    if (n == 0) return true;

    // blocs de ~24 s avec un recouvrement de ~0,7 s (fondu linéaire)
    const size_t block = (size_t) kHop * 1024;
    const size_t ov = (size_t) kHop * 32;
    const size_t step = block - ov;
    std::vector<float> xl (block), xr (block), ol, orr;
    const size_t count = (n + step - 1) / step;
    size_t k = 0;
    for (size_t start = 0; start < n; start += step, ++k)
    {
        if (progress && ! progress ((double) k / (double) count))
            return false;
        for (size_t i = 0; i < block; ++i)
        {
            const size_t j = start + i;
            xl[i] = j < n ? in.l[j] : 0.0f;
            xr[i] = j < n ? in.r[j] : 0.0f;
        }
        processBlock (xl, xr, sr, s, ol, orr);
        for (size_t i = 0; i < block && start + i < n; ++i)
        {
            float w = 1.0f;
            if (start > 0 && i < ov) w = (float) i / (float) ov;                    // entrée en fondu
            if (start + step < n && i >= step) w = (float) (block - i) / (float) ov; // sortie en fondu
            lead.l[start + i] += ol[i] * w;
            lead.r[start + i] += orr[i] * w;
        }
        if (start + block >= n) break;
    }
    if (progress) progress (1.0);
    return true;
}

} // namespace lsplit
