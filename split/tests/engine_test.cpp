// Test du moteur sans JUCE : lit un WAV float32 stéréo, sépare, écrit les stems en WAV float32.
#include "../Source/Engine/Separator.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
using namespace lsplit;

static bool readWav (const char* path, StereoBuffer& b, double& sr)
{
    std::ifstream f (path, std::ios::binary); if (! f) return false;
    std::vector<char> d ((std::istreambuf_iterator<char> (f)), {});
    size_t p = 12; int ch = 0, bits = 0, fmt = 0;
    while (p + 8 <= d.size()) {
        uint32_t sz; std::memcpy (&sz, &d[p + 4], 4);
        if (! std::memcmp (&d[p], "fmt ", 4)) { uint16_t v; std::memcpy (&v, &d[p+8], 2); fmt = v; std::memcpy (&v, &d[p+10], 2); ch = v;
            uint32_t r; std::memcpy (&r, &d[p+12], 4); sr = r; std::memcpy (&v, &d[p+22], 2); bits = v; }
        if (! std::memcmp (&d[p], "data", 4)) {
            if ((fmt != 3 && fmt != 0xFFFE) || bits != 32 || ch != 2) return false;
            size_t n = sz / 8; b.resize (n); const float* s = (const float*) &d[p + 8];
            for (size_t i = 0; i < n; ++i) { b.l[i] = s[2*i]; b.r[i] = s[2*i+1]; } return true; }
        p += 8 + sz + (sz & 1);
    }
    return false;
}
static void writeWav (const std::string& path, const StereoBuffer& b, double sr)
{
    std::ofstream f (path, std::ios::binary); uint32_t n = (uint32_t) b.size(), data = n * 8, riff = 36 + data, r = (uint32_t) sr, br = r * 8;
    uint16_t fmt = 3, ch = 2, ba = 8, bits = 32; uint32_t fs = 16;
    f.write ("RIFF", 4); f.write ((char*) &riff, 4); f.write ("WAVEfmt ", 8); f.write ((char*) &fs, 4); f.write ((char*) &fmt, 2); f.write ((char*) &ch, 2);
    f.write ((char*) &r, 4); f.write ((char*) &br, 4); f.write ((char*) &ba, 2); f.write ((char*) &bits, 2); f.write ("data", 4); f.write ((char*) &data, 4);
    for (uint32_t i = 0; i < n; ++i) { f.write ((char*) &b.l[i], 4); f.write ((char*) &b.r[i], 4); }
}
int main (int argc, char** argv)
{
    if (argc < 5) { std::cerr << "usage: engine_test ortdir modelsdir in.wav outdir [quality]\n"; return 2; }
    std::string err;
    if (! OrtRuntime::load (argv[1], err)) { std::cerr << err << "\n"; return 1; }
    std::cout << "onnxruntime " << OrtRuntime::version() << "\n";
    StereoBuffer mix; double sr = 0;
    if (! readWav (argv[3], mix, sr)) { std::cerr << "lecture wav\n"; return 1; }
    SeparationSettings s; s.quality = (Quality) (argc > 5 ? atoi (argv[5]) : 1); s.useGpu = false;
    Separator sep (argv[2]); StemSet res;
    auto t0 = std::chrono::steady_clock::now(); int lastPct = -1;
    sep.run (mix, sr, s, res, [&] (const std::string& st, double f) { int pc = (int) (f * 100); if (pc / 10 != lastPct / 10) { std::cout << st << " " << pc << "%\n"; lastPct = pc; } return true; });
    double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::cout << "temps " << secs << " s, " << res.deviceUsed << "\n";
    for (int k = 0; k < numStems; ++k) if (res.present[k]) writeWav (std::string (argv[4]) + "/" + stemFileName (k) + ".wav", res.stems[k], sr);
    return 0;
}
