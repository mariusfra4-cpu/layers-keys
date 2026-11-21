#include "../Source/Engine/ChoirSplit.h"
#include "../Source/Engine/OrtRuntime.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
using namespace lsplit;
static bool readWav (const std::string& path, StereoBuffer& b)
{
    std::ifstream f (path, std::ios::binary); if (! f) return false;
    std::vector<char> d ((std::istreambuf_iterator<char> (f)), {});
    size_t p = 12;
    while (p + 8 <= d.size()) { uint32_t sz; std::memcpy (&sz, &d[p + 4], 4);
        if (! std::memcmp (&d[p], "data", 4)) { size_t n = sz / 8; b.resize (n); const float* s = (const float*) &d[p + 8];
            for (size_t i = 0; i < n; ++i) { b.l[i] = s[2*i]; b.r[i] = s[2*i+1]; } return true; }
        p += 8 + sz + (sz & 1); }
    return false;
}
static double sdr (const StereoBuffer& est, const StereoBuffer& ref)
{
    double e = 0, r = 0;
    for (size_t i = 0; i < ref.size(); ++i) { double x = est.l[i] - ref.l[i], y = est.r[i] - ref.r[i]; e += x*x + y*y; r += (double) ref.l[i]*ref.l[i] + (double) ref.r[i]*ref.r[i]; }
    return 10 * std::log10 ((r + 1e-12) / (e + 1e-12));
}
int main (int, char** argv)
{
    // choir_test mix.wav prefixe ortdir basic_pitch.onnx
    std::string err;
    if (! OrtRuntime::load (argv[3], err)) { std::cerr << err << "\n"; return 1; }
    StereoBuffer mix; readWav (argv[1], mix);
    StereoBuffer out[numVoices];
    splitChoir (mix, 44100, out, argv[4], nullptr);
    double worst = 1e9, sumErr = 0;
    for (int v = 0; v < numVoices; ++v)
    {
        StereoBuffer ref; readWav (std::string (argv[2]) + "_" + std::to_string (v) + ".wav", ref);
        StereoBuffer quarter = mix; for (size_t i = 0; i < mix.size(); ++i) { quarter.l[i] *= 0.25f; quarter.r[i] *= 0.25f; }
        const double s = sdr (out[v], ref);
        worst = std::min (worst, s);
        std::cout << choirVoiceName (v) << " : SDR " << s << " dB (sans séparation, chœur / 4 : " << sdr (quarter, ref) << " dB)\n";
    }
    for (size_t i = 0; i < mix.size(); ++i) { double a = mix.l[i]; for (int v = 0; v < numVoices; ++v) a -= out[v].l[i]; sumErr = std::max (sumErr, std::abs (a)); }
    std::cout << "somme des pupitres - chœur : écart max " << sumErr << "\n";
    return 0;
}
