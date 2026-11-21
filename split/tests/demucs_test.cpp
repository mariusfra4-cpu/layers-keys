// Compare la séparation Demucs du moteur C++ à la référence PyTorch (demucs.apply_model).
#include "../Source/Engine/DemucsModel.h"
#include <chrono>
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
int main (int argc, char** argv)
{
    // ortdir model.onnx mix.wav refdir
    std::string err;
    if (! OrtRuntime::load (argv[1], err)) { std::cerr << err << "\n"; return 1; }
    DemucsModel m;
    if (! m.load (argv[2], Device::cpu, err)) { std::cerr << err << "\n"; return 1; }
    StereoBuffer mix; if (! readWav (argv[3], mix)) { std::cerr << "wav\n"; return 1; }
    std::vector<StereoBuffer> out;
    auto t0 = std::chrono::steady_clock::now();
    m.process (mix, out, nullptr);
    std::cout << "temps " << std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() << " s\n";
    int bad = 0;
    for (size_t s = 0; s < m.sources().size(); ++s)
    {
        StereoBuffer ref; readWav (std::string (argv[4]) + "/" + m.sources()[s] + ".wav", ref);
        double e = 0, r = 0;
        for (size_t i = 0; i < ref.size(); ++i) { double a = out[s].l[i] - ref.l[i], b = out[s].r[i] - ref.r[i]; e += a*a + b*b; r += (double) ref.l[i]*ref.l[i] + (double) ref.r[i]*ref.r[i]; }
        const double rel = std::sqrt (e / (r + 1e-20));
        std::cout << m.sources()[s] << " ecart relatif " << rel << "\n";
        if (rel > 1e-3) ++bad;
    }
    std::cout << (bad ? "ECHEC\n" : "OK\n");
    return bad ? 1 : 0;
}
