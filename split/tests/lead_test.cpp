#include "../Source/Engine/LeadSplit.h"
#include <cmath>
#include <cstdlib>
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
static double sdr (const StereoBuffer& est, const StereoBuffer& ref, size_t a, size_t b)
{
    double e = 0, r = 0;
    for (size_t i = a; i < b; ++i) { double x = est.l[i] - ref.l[i], y = est.r[i] - ref.r[i]; e += x*x + y*y; r += (double) ref.l[i]*ref.l[i] + (double) ref.r[i]*ref.r[i]; }
    return 10 * std::log10 ((r + 1e-12) / (e + 1e-12));
}
static double energyDb (const StereoBuffer& x, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += (double) x.l[i]*x.l[i] + (double) x.r[i]*x.r[i];
    return 10 * std::log10 (e / (b - a) + 1e-20);
}
int main (int, char** argv)
{
    StereoBuffer mix, lead, rhy; readWav (argv[1], mix); readWav (argv[2], lead); readWav (argv[3], rhy);
    LeadSplitSettings st; if (argv[4]) st.threshold = atof (argv[4]);
    StereoBuffer est; splitLead (mix, 44100, est, nullptr, st);
    StereoBuffer rest = mix; for (size_t i = 0; i < mix.size(); ++i) { rest.l[i] -= est.l[i]; rest.r[i] -= est.r[i]; }
    const size_t s0 = 44100 * 10, s1 = 44100 * 20;
    StereoBuffer zero; zero.resize (mix.size());
    std::cout << "solo : SDR " << sdr (est, lead, s0, s1) << " dB (rien extrait = " << sdr (zero, lead, s0, s1) << " dB)\n";
    std::cout << "rythmique pendant le solo : SDR " << sdr (rest, rhy, s0, s1) << " dB (sans séparation = " << sdr (mix, rhy, s0, s1) << " dB)\n";
    std::cout << "rythmique hors solo : SDR " << sdr (rest, rhy, 0, s0) << " dB\n";
    std::cout << "fuite dans « solo » hors solo : " << energyDb (est, 0, s0) - energyDb (rhy, 0, s0) << " dB sous la rythmique\n";
}
