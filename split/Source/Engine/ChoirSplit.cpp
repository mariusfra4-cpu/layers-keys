#include "ChoirSplit.h"
#include "BasicPitch.h"
#include "Dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lsplit
{

const char* choirVoiceName (int v)
{
    switch (v)
    {
        case voiceSoprano: return "Soprano";
        case voiceAlto:    return "Alto";
        case voiceTenor:   return "Ténor";
        case voiceBass:    return "Basse (voix)";
        default:           return "?";
    }
}

namespace
{
    constexpr int kNfft = 4096, kHop = 1024, kHarm = 10;
    // tessitures (notes MIDI) et centre habituel de chaque pupitre
    constexpr double kLow[numVoices]    = { 59.0, 53.0, 46.0, 38.0 };
    constexpr double kHigh[numVoices]   = { 84.0, 77.0, 70.0, 65.0 };
    constexpr double kCentre[numVoices] = { 70.0, 64.0, 57.0, 50.0 };

    double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

    void processBlock (const std::vector<float>& xl, const std::vector<float>& xr, double sr, size_t blockStart,
                       const BasicPitchFrames& bp, std::array<std::vector<float>, 2 * numVoices>& out)
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

        // 1. notes simultanées de chaque trame : activations de Basic Pitch (jusqu'à 4 notes)
        std::vector<std::array<double, numVoices>> pitch ((size_t) frames); // note MIDI par pupitre, 0 = muet
        std::array<double, numVoices> prev {};
        std::array<int, numVoices> silentFor {};
        silentFor.fill (1000);
        for (int t = 0; t < frames; ++t)
        {
            pitch[(size_t) t].fill (0.0);
            const double sec = (blockStart + (double) t * kHop) / sr;
            const long bi = std::lround (sec * BasicPitchFrames::rate);
            if (bi < 0 || bi >= (long) bp.frames()) continue;
            const float* act = bp.note (bi);
            const float* cont = bp.contour (bi);

            std::vector<int> peaks;
            for (int i = 1; i < 87; ++i)
                if (act[i] > 0.2f && act[i] >= act[i - 1] && act[i] >= act[i + 1]) peaks.push_back (i);
            std::sort (peaks.begin(), peaks.end(), [&] (int a, int b) { return act[a] > act[b]; });
            std::vector<double> notes;
            for (int i : peaks)
            {
                // hauteur fine (1/3 de demi-ton) avec le contour
                const int ci = i * 3;
                int bc = ci;
                for (int c = std::max (0, ci - 2); c <= std::min (263, ci + 2); ++c)
                    if (cont[c] > cont[bc]) bc = c;
                const double m = 21.0 + bc / 3.0;
                bool dup = false;
                for (double n : notes) dup = dup || std::abs (n - m) < 0.8;
                if (! dup) notes.push_back (m);
                if ((int) notes.size() == numVoices) break;
            }

            // 2. attribution aux pupitres : ordre respecté (la plus aiguë au pupitre le plus aigu), continuité
            std::sort (notes.begin(), notes.end(), std::greater<double>());

            const int n = (int) notes.size();
            double bestCost = 1.0e30;
            std::array<int, numVoices> bestAssign {};
            for (int mask = 0; mask < (1 << numVoices); ++mask)
            {
                int bits = 0;
                for (int b = 0; b < numVoices; ++b) bits += (mask >> b) & 1;
                if (bits != n) continue;
                double cost = 0.0;
                int ni = 0;
                std::array<int, numVoices> as {};
                as.fill (-1);
                for (int v = 0; v < numVoices; ++v)
                {
                    if (! (mask & (1 << v))) continue;
                    const double note = notes[(size_t) ni];
                    // la voix reste proche de sa note précédente, et de sa tessiture habituelle
                    const bool recent = silentFor[(size_t) v] < 20;
                    const double ref = recent ? 0.6 * prev[(size_t) v] + 0.4 * kCentre[v] : kCentre[v];
                    cost += std::abs (note - ref);
                    if (note < kLow[v]) cost += 6.0 * (kLow[v] - note);
                    if (note > kHigh[v]) cost += 6.0 * (note - kHigh[v]);
                    as[(size_t) v] = ni++;
                }
                if (cost < bestCost) { bestCost = cost; bestAssign = as; }
            }
            for (int v = 0; v < numVoices; ++v)
            {
                if (n > 0 && bestAssign[(size_t) v] >= 0)
                {
                    pitch[(size_t) t][(size_t) v] = notes[(size_t) bestAssign[(size_t) v]];
                    prev[(size_t) v] = pitch[(size_t) t][(size_t) v];
                    silentFor[(size_t) v] = 0;
                }
                else
                    ++silentFor[(size_t) v];
            }
        }

        // 3. nettoyage des pistes de hauteur
        for (int v = 0; v < numVoices; ++v)
        {
            // médiane glissante (7 trames) sur les trames chantées : retire les sauts isolés
            std::vector<double> sm ((size_t) frames, 0.0);
            for (int t = 0; t < frames; ++t)
            {
                if (pitch[(size_t) t][(size_t) v] <= 0.0) continue;
                std::vector<double> win;
                for (int d = -3; d <= 3; ++d)
                {
                    const int u = t + d;
                    if (u >= 0 && u < frames && pitch[(size_t) u][(size_t) v] > 0.0) win.push_back (pitch[(size_t) u][(size_t) v]);
                }
                std::nth_element (win.begin(), win.begin() + (long) (win.size() / 2), win.end());
                const double md = win[win.size() / 2];
                sm[(size_t) t] = std::abs (pitch[(size_t) t][(size_t) v] - md) > 1.5 ? md : pitch[(size_t) t][(size_t) v];
            }
            // trous courts (≤ 15 trames) entre deux mêmes notes : la voix tient sa note
            int t = 0;
            while (t < frames)
            {
                if (sm[(size_t) t] > 0.0) { ++t; continue; }
                int e = t;
                while (e < frames && sm[(size_t) e] <= 0.0) ++e;
                if (t > 0 && e < frames && e - t <= 15 && std::abs (sm[(size_t) t - 1] - sm[(size_t) e]) < 1.0)
                    for (int u = t; u < e; ++u) sm[(size_t) u] = 0.5 * (sm[(size_t) t - 1] + sm[(size_t) e]);
                t = e;
            }
            for (int u = 0; u < frames; ++u) pitch[(size_t) u][(size_t) v] = sm[(size_t) u];
        }
        // on retire les apparitions de moins de 3 trames
        for (int v = 0; v < numVoices; ++v)
        {
            int t = 0;
            while (t < frames)
            {
                if (pitch[(size_t) t][(size_t) v] <= 0.0) { ++t; continue; }
                int e = t;
                while (e < frames && pitch[(size_t) e][(size_t) v] > 0.0) ++e;
                if (e - t < 3)
                    for (int u = t; u < e; ++u) pitch[(size_t) u][(size_t) v] = 0.0;
                t = e;
            }
        }

        // 4. masques doux ; le reste est partagé entre les voix qui chantent (somme exacte)
        std::array<std::vector<std::complex<float>>, 2 * numVoices> spec;
        for (auto& s : spec) s.assign ((size_t) frames * nb, { 0.0f, 0.0f });
        std::vector<std::array<float, numVoices>> m ((size_t) nb);
        for (int t = 0; t < frames; ++t)
        {
            for (auto& a : m) a.fill (0.0f);
            int active = 0;
            for (int v = 0; v < numVoices; ++v)
            {
                const double p = pitch[(size_t) t][(size_t) v];
                if (p <= 0.0) continue;
                ++active;
                const double f0 = midiToHz (p);
                for (int h = 1; h <= kHarm + 4; ++h)
                {
                    const double b = f0 * h / binHz;
                    if (b >= maxBin) break;
                    const double sigma = 1.0 + 0.05 * h;
                    for (int k = std::max (1, (int) (b - 3 * sigma)); k <= std::min (maxBin, (int) (b + 3 * sigma)); ++k)
                    {
                        const double d = (k - b) / sigma;
                        m[(size_t) k][(size_t) v] = std::max (m[(size_t) k][(size_t) v], (float) std::exp (-0.5 * d * d));
                    }
                }
            }

            // part de chaque voix dans l'énergie de la trame
            std::array<double, numVoices> share {};
            double tot = 0.0;
            for (int k = 0; k < nb; ++k)
            {
                const float a = 0.5f * std::abs (L[(size_t) t * nb + k] + R[(size_t) t * nb + k]);
                for (int v = 0; v < numVoices; ++v)
                    share[(size_t) v] += (double) m[(size_t) k][(size_t) v] * a * a;
            }
            for (double s : share) tot += s;
            for (int v = 0; v < numVoices; ++v)
                share[(size_t) v] = tot > 0.0 ? share[(size_t) v] / tot : (active == 0 ? 1.0 / numVoices : 0.0);

            for (int k = 0; k < nb; ++k)
            {
                auto& mk = m[(size_t) k];
                float sum = 0.0f;
                for (float x : mk) sum += x;
                if (sum > 1.0f) { for (auto& x : mk) x /= sum; sum = 1.0f; }
                const float rest = 1.0f - sum;
                const size_t i = (size_t) t * nb + (size_t) k;
                for (int v = 0; v < numVoices; ++v)
                {
                    const float g = mk[(size_t) v] + rest * (float) share[(size_t) v];
                    spec[(size_t) (2 * v)][i] = L[i] * g;
                    spec[(size_t) (2 * v + 1)][i] = R[i] * g;
                }
            }
        }

        for (int c = 0; c < 2 * numVoices; ++c)
        {
            out[(size_t) c].assign ((size_t) len, 0.0f);
            stft.inverse (spec[(size_t) c], frames, out[(size_t) c].data());
        }
    }
}

bool splitChoir (const StereoBuffer& in, double sr, StereoBuffer out[numVoices],
                 const std::string& basicPitchModel, const std::function<bool (double)>& progress)
{
    const size_t n = in.size();
    for (int v = 0; v < numVoices; ++v) out[v].resize (n);
    if (n == 0) return true;

    // détection des notes sur tout le chœur (rapide : petit réseau)
    BasicPitchFrames bp;
    bp.compute (in, sr, basicPitchModel);

    const size_t block = (size_t) kHop * 512, ov = (size_t) kHop * 32, step = block - ov;
    std::vector<float> xl (block), xr (block);
    std::array<std::vector<float>, 2 * numVoices> o;
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
        processBlock (xl, xr, sr, start, bp, o);
        for (size_t i = 0; i < block && start + i < n; ++i)
        {
            float wgt = 1.0f;
            if (start > 0 && i < ov) wgt = (float) i / (float) ov;
            if (start + step < n && i >= step) wgt = (float) (block - i) / (float) ov;
            for (int v = 0; v < numVoices; ++v)
            {
                out[v].l[start + i] += o[(size_t) (2 * v)][i] * wgt;
                out[v].r[start + i] += o[(size_t) (2 * v + 1)][i] * wgt;
            }
        }
        if (start + block >= n) break;
    }
    if (progress) progress (1.0);
    return true;
}

} // namespace lsplit
