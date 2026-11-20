#include "../Source/Engine/Analysis.h"
#include <cstring>
#include <fstream>
#include <iostream>
using namespace lsplit;
static bool readWav (const char* path, StereoBuffer& b, double& sr)
{
    std::ifstream f (path, std::ios::binary); if (! f) return false;
    std::vector<char> d ((std::istreambuf_iterator<char> (f)), {});
    size_t p = 12;
    while (p + 8 <= d.size()) { uint32_t sz; std::memcpy (&sz, &d[p + 4], 4);
        if (! std::memcmp (&d[p], "fmt ", 4)) { uint32_t r; std::memcpy (&r, &d[p+12], 4); sr = r; }
        if (! std::memcmp (&d[p], "data", 4)) { size_t n = sz / 8; b.resize (n); const float* s = (const float*) &d[p + 8];
            for (size_t i = 0; i < n; ++i) { b.l[i] = s[2*i]; b.r[i] = s[2*i+1]; } return true; }
        p += 8 + sz + (sz & 1); }
    return false;
}
int main (int, char** argv)
{
    StereoBuffer mix, drums; double sr = 0, sr2 = 0;
    readWav (argv[1], mix, sr); readWav (argv[2], drums, sr2);
    AnalysisResult a; a.lufs = integratedLoudness (mix, sr); a.peakDb = samplePeakDb (mix);
    a.bpm = estimateTempo (drums, sr, a.bpmConfidence);
    StereoBuffer harm = mix; for (size_t i = 0; i < harm.size(); ++i) { harm.l[i] -= drums.l[i]; harm.r[i] -= drums.r[i]; }
    estimateKey (harm, sr, a.keyRoot, a.keyMinor, a.keyConfidence);
    std::cout << "LUFS " << a.lufs << " crete " << a.peakDb << " BPM " << a.bpm << " (" << a.bpmConfidence << ") ton " << a.keyName() << " " << a.keyShort() << " " << a.camelot() << " (" << a.keyConfidence << ")\n";
}
