#include "Internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>

namespace africanloops {

std::vector<float> Audio::mono() const {
    std::vector<float> m(L.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (L[i] + R[i]);
    return m;
}

static std::string lowerExt(const std::string& path) {
    auto dot = path.find_last_of('.');
    auto slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string e = path.substr(dot);
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

bool isAudioFile(const std::string& path) {
    auto e = lowerExt(path);
    return e == ".wav" || e == ".wave" || e == ".mp3";
}

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static float f32(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

bool parseWAV(const std::vector<uint8_t>& b, Audio& a, std::string& err) {
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
        err = "ce fichier n'est pas un WAV valide";
        return false;
    }
    int tag = 0, ch = 0, bits = 0, rate = 0;
    const uint8_t* data = nullptr;
    size_t dataLen = 0;
    bool haveFmt = false;
    a = Audio{};
    size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const uint8_t* id = b.data() + pos;
        size_t size = rd32(b.data() + pos + 4);
        size_t body = pos + 8;
        size_t end = body + size;
        if (end > b.size() || end < body) end = b.size();  // fichier tronqué
        const uint8_t* c = b.data() + body;
        size_t len = end - body;
        if (!std::memcmp(id, "fmt ", 4)) {
            if (len < 16) { err = "WAV invalide (fmt)"; return false; }
            tag = rd16(c);
            ch = rd16(c + 2);
            rate = (int)rd32(c + 4);
            bits = rd16(c + 14);
            if (tag == 0xFFFE && len >= 26) tag = rd16(c + 24);
            haveFmt = true;
        } else if (!std::memcmp(id, "data", 4)) {
            data = c;
            dataLen = len;
        } else if (!std::memcmp(id, "acid", 4) && len >= 24) {
            // bloc ACID : nombre de temps (offset 12) et tempo (offset 20)
            int beats = (int)rd32(c + 12);
            double t = f32(rd32(c + 20));
            if (t >= 40 && t <= 300) a.metaBPM = t;
            if (beats > 0 && beats <= 1024) a.metaBeats = beats;
        }
        pos = end + (size & 1);
    }
    if (!haveFmt || !data) { err = "WAV invalide (données manquantes)"; return false; }
    if (ch < 1 || ch > 8 || rate < 1000 || rate > 384000) { err = "WAV invalide (format)"; return false; }
    int bps = bits / 8;
    if ((tag != 1 && tag != 3) || bps < 1 || bps > 8 || (tag == 3 && bps != 4 && bps != 8)) {
        err = "ce WAV est compressé : seuls les WAV PCM sont pris en charge";
        return false;
    }
    size_t frames = dataLen / (size_t)(bps * ch);
    frames = std::min(frames, (size_t)(rate * kMaxSeconds));
    a.rate = rate;
    a.L.resize(frames);
    a.R.resize(frames);
    auto sample = [&](const uint8_t* s) -> float {
        if (tag == 1) {
            switch (bps) {
                case 1: return (float)((int)s[0] - 128) / 128.f;
                case 2: return (float)(int16_t)rd16(s) / 32768.f;
                case 3: return (float)((int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8) / 8388608.f;
                case 4: return (float)((double)(int32_t)rd32(s) / 2147483648.0);
            }
        } else if (bps == 4) {
            return f32(rd32(s));
        } else if (bps == 8) {
            uint64_t u = (uint64_t)rd32(s) | (uint64_t)rd32(s + 4) << 32;
            double d;
            std::memcpy(&d, &u, 8);
            return (float)d;
        }
        return 0.f;
    };
    for (size_t i = 0; i < frames; ++i) {
        const uint8_t* base = data + i * bps * ch;
        float l = sample(base), r = ch > 1 ? sample(base + bps) : l;
        a.L[i] = std::isfinite(l) ? l : 0.f;  // fichier flottant abîmé
        a.R[i] = std::isfinite(r) ? r : 0.f;
    }
    return true;
}

bool loadWAV(const std::string& path, Audio& out, std::string& err) {
    std::ifstream f(detail::pathFromUtf8(path), std::ios::binary);
    if (!f) { err = "impossible d'ouvrir le fichier"; return false; }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parseWAV(b, out, err);
}

std::vector<uint8_t> encodeWAV(const Audio& a, double bpm, int beats) {
    std::vector<uint8_t> o;
    size_t n = a.L.size(), dataSize = n * 6, acidSize = bpm > 0 ? 8 + 24 : 0;
    o.reserve(44 + acidSize + dataSize);
    auto s4 = [&](const char* t) { o.insert(o.end(), t, t + 4); };
    auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back((uint8_t)(v >> (8 * i))); };
    auto w16 = [&](uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); };
    s4("RIFF"); w32((uint32_t)(4 + 24 + acidSize + 8 + dataSize));
    s4("WAVE"); s4("fmt "); w32(16); w16(1); w16(2);
    w32((uint32_t)a.rate); w32((uint32_t)a.rate * 6); w16(6); w16(24);
    if (bpm > 0) {
        s4("acid"); w32(24);
        w32(0); w16(60); w16(0); w32(0);  // drapeaux, note de base
        w32((uint32_t)beats); w16(4); w16(4);
        float t = (float)bpm; uint32_t u; std::memcpy(&u, &t, 4); w32(u);
    }
    s4("data"); w32((uint32_t)dataSize);
    auto put = [&](float f) {
        f = std::clamp(f, -1.f, 1.f);
        int32_t v = (int32_t)std::lround((double)f * 8388607.0);
        o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)(v >> 16));
    };
    for (size_t i = 0; i < n; ++i) { put(a.L[i]); put(a.R[i]); }
    return o;
}

}  // namespace africanloops
