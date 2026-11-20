#include "Dsp.h"

#include "../../ThirdParty/pocketfft_hdronly.h"

#include <algorithm>
#include <cmath>

namespace lsplit
{

static constexpr double kPi = 3.14159265358979323846;

Stft::Stft (int nfft, int hopSize) : n (nfft), h (hopSize), window ((size_t) nfft)
{
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / n)); // Hann périodique
}

void Stft::forward (const float* x, int length, std::vector<std::complex<float>>& out, int& frames) const
{
    const int pad = n / 2;
    frames = 1 + length / h;
    const int padded = length + 2 * pad;

    // remplissage « reflect » des bords (comme torch / numpy, sans répéter l'échantillon du bord)
    std::vector<float> buf ((size_t) padded);
    for (int i = 0; i < padded; ++i)
    {
        int j = i - pad;
        if (length > 1)
        {
            const int period = 2 * (length - 1);
            j = ((j % period) + period) % period;
            if (j >= length) j = period - j;
        }
        else
            j = 0;
        buf[(size_t) i] = x[j];
    }

    std::vector<float> frameData ((size_t) frames * (size_t) n);
    for (int t = 0; t < frames; ++t)
    {
        const float* src = buf.data() + (size_t) t * (size_t) h;
        float* dst = frameData.data() + (size_t) t * (size_t) n;
        for (int i = 0; i < n; ++i)
            dst[i] = src[i] * window[(size_t) i];
    }

    const int nb = bins();
    out.assign ((size_t) frames * (size_t) nb, std::complex<float> (0.0f, 0.0f));
    pocketfft::r2c<float> ({ (size_t) frames, (size_t) n },
                           { (ptrdiff_t) (sizeof (float) * (size_t) n), (ptrdiff_t) sizeof (float) },
                           { (ptrdiff_t) (sizeof (std::complex<float>) * (size_t) nb), (ptrdiff_t) sizeof (std::complex<float>) },
                           1, pocketfft::FORWARD, frameData.data(), out.data(), 1.0f);
}

void Stft::inverse (const std::vector<std::complex<float>>& spec, int frames, float* out) const
{
    const int nb = bins();
    std::vector<float> frameData ((size_t) frames * (size_t) n);
    pocketfft::c2r<float> ({ (size_t) frames, (size_t) n },
                           { (ptrdiff_t) (sizeof (std::complex<float>) * (size_t) nb), (ptrdiff_t) sizeof (std::complex<float>) },
                           { (ptrdiff_t) (sizeof (float) * (size_t) n), (ptrdiff_t) sizeof (float) },
                           1, pocketfft::BACKWARD, spec.data(), frameData.data(), 1.0f / (float) n);

    const int total = n + h * (frames - 1);
    std::vector<float> acc ((size_t) total, 0.0f), env ((size_t) total, 0.0f);
    for (int t = 0; t < frames; ++t)
    {
        const float* src = frameData.data() + (size_t) t * (size_t) n;
        float* a = acc.data() + (size_t) t * (size_t) h;
        float* e = env.data() + (size_t) t * (size_t) h;
        for (int i = 0; i < n; ++i)
        {
            const float w = window[(size_t) i];
            a[i] += src[i] * w;
            e[i] += w * w;
        }
    }

    const int pad = n / 2;
    const int length = h * (frames - 1);
    for (int i = 0; i < length; ++i)
    {
        const float e = env[(size_t) (i + pad)];
        out[i] = e > 1.0e-11f ? acc[(size_t) (i + pad)] / e : 0.0f;
    }
}

//==============================================================================
namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1.0e-12 * sum) break;
        }
        return sum;
    }
}

std::vector<float> resample (const float* in, size_t numIn, double inRate, double outRate)
{
    if (numIn == 0 || inRate <= 0.0 || outRate <= 0.0)
        return {};

    if (std::abs (inRate - outRate) < 1.0e-9)
        return std::vector<float> (in, in + numIn);

    const double ratio = outRate / inRate;
    const double cutoff = std::min (1.0, ratio) * 0.97; // marge anti-repliement
    const int zeroCrossings = 32;
    const double halfWidth = zeroCrossings / std::min (1.0, ratio); // en échantillons d'entrée
    const int taps = (int) std::ceil (halfWidth);

    // noyau tabulé : h(t) pour t dans [0, halfWidth], résolution 1/kRes échantillon
    constexpr int kRes = 512;
    const int tableSize = (int) std::ceil (halfWidth * kRes) + 2;
    std::vector<float> table ((size_t) tableSize);
    const double beta = 9.0;
    const double i0b = besselI0 (beta);
    for (int i = 0; i < tableSize; ++i)
    {
        const double t = (double) i / kRes;
        const double r = t / halfWidth;
        double w = 0.0;
        if (r < 1.0)
            w = besselI0 (beta * std::sqrt (1.0 - r * r)) / i0b;
        const double xx = kPi * cutoff * t;
        const double sinc = t < 1.0e-12 ? 1.0 : std::sin (xx) / xx;
        table[(size_t) i] = (float) (cutoff * sinc * w);
    }

    const size_t numOut = (size_t) std::floor ((double) numIn * ratio + 0.5);
    std::vector<float> out (numOut);
    const double step = inRate / outRate;

    for (size_t o = 0; o < numOut; ++o)
    {
        const double pos = (double) o * step;
        const long centre = (long) std::floor (pos);
        const double frac = pos - (double) centre;
        double acc = 0.0;
        for (long k = centre - taps + 1; k <= centre + taps; ++k)
        {
            if (k < 0 || k >= (long) numIn) continue;
            const double t = std::abs ((double) (k - centre) - frac);
            if (t >= halfWidth) continue;
            const double ti = t * kRes;
            const int i0 = (int) ti;
            const double f = ti - i0;
            const double hv = table[(size_t) i0] + (table[(size_t) i0 + 1] - table[(size_t) i0]) * f;
            acc += in[k] * hv;
        }
        out[o] = (float) acc;
    }
    return out;
}

} // namespace lsplit
