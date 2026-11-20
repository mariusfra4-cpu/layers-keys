#include "Internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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
static uint32_t rd32be(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}
static float f32(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

namespace {

int16_t alaw(uint8_t v) {
    v ^= 0x55;
    int t = (v & 0x0F) << 4;
    const int seg = (v & 0x70) >> 4;
    if (seg == 0) t += 8;
    else { t += 0x108; if (seg > 1) t <<= seg - 1; }
    return (int16_t)((v & 0x80) ? t : -t);
}

int16_t ulaw(uint8_t v) {
    v = (uint8_t)~v;
    int t = ((v & 0x0F) << 3) + 0x84;
    t <<= (v & 0x70) >> 4;
    return (int16_t)((v & 0x80) ? (0x84 - t) : (t - 0x84));
}

// A-law, µ-law, IMA ADPCM, MS ADPCM → flottants
bool decodeCompressedWav(int tag, int ch, int blockAlign, const uint8_t* fmt, size_t fmtLen, const uint8_t* data,
                         size_t len, Audio& a) {
    std::vector<int16_t> pcm;  // entrelacé
    const size_t maxFrames = (size_t)(a.rate * kMaxSeconds);
    if (tag == 6 || tag == 7) {
        pcm.resize(std::min(len, maxFrames * (size_t)ch));
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = tag == 6 ? alaw(data[i]) : ulaw(data[i]);
    } else if (tag == 0x11) {  // IMA ADPCM
        static constexpr int idxTab[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
        static constexpr int stepTab[89] = {
            7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
            107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
            876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
            4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
            22385, 24623, 27086, 29794, 32767};
        if (blockAlign < 4 * ch || ch > 2) return false;
        const int spb = (blockAlign - 4 * ch) * 8 / (4 * ch) + 1;  // échantillons par canal et par bloc
        for (size_t off = 0; off + (size_t)blockAlign <= len && pcm.size() < maxFrames * (size_t)ch; off += (size_t)blockAlign) {
            const uint8_t* blk = data + off;
            int pred[2], idx[2];
            std::vector<int16_t> out((size_t)(spb * ch));
            for (int c = 0; c < ch; ++c) {
                pred[c] = (int16_t)rd16(blk + 4 * c);
                idx[c] = std::clamp((int)blk[4 * c + 2], 0, 88);
                out[(size_t)c] = (int16_t)pred[c];
            }
            const uint8_t* p = blk + 4 * ch;
            for (int grp = 0; grp < (spb - 1) / 8; ++grp)
                for (int c = 0; c < ch; ++c)
                    for (int byte = 0; byte < 4; ++byte, ++p)
                        for (int half = 0; half < 2; ++half) {
                            const int nib = half == 0 ? (*p & 0x0F) : (*p >> 4);
                            const int step = stepTab[idx[c]];
                            int diff = step >> 3;
                            if (nib & 1) diff += step >> 2;
                            if (nib & 2) diff += step >> 1;
                            if (nib & 4) diff += step;
                            if (nib & 8) diff = -diff;
                            pred[c] = std::clamp(pred[c] + diff, -32768, 32767);
                            idx[c] = std::clamp(idx[c] + idxTab[nib], 0, 88);
                            const int n = 1 + grp * 8 + byte * 2 + half;
                            out[(size_t)(n * ch + c)] = (int16_t)pred[c];
                        }
            pcm.insert(pcm.end(), out.begin(), out.end());
        }
    } else if (tag == 0x02) {  // MS ADPCM
        static constexpr int adapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
        if (fmtLen < 20 || ch > 2 || blockAlign < 7 * ch) return false;
        const int spb = rd16(fmt + 18);  // échantillons par bloc (après cbSize en 16)
        const int nCoef = fmtLen >= 22 ? rd16(fmt + 20) : 0;
        std::vector<int> c1, c2;
        static constexpr int def1[7] = {256, 512, 0, 192, 240, 460, 392}, def2[7] = {0, -256, 0, 64, 0, -208, -232};
        for (int i = 0; i < std::max(nCoef, 7); ++i) {
            if (i < nCoef && fmtLen >= (size_t)(22 + 4 * i + 4)) {
                c1.push_back((int16_t)rd16(fmt + 22 + 4 * i));
                c2.push_back((int16_t)rd16(fmt + 24 + 4 * i));
            } else if (i < 7) {
                c1.push_back(def1[i]);
                c2.push_back(def2[i]);
            }
        }
        if (spb < 2) return false;
        for (size_t off = 0; off + (size_t)blockAlign <= len && pcm.size() < maxFrames * (size_t)ch; off += (size_t)blockAlign) {
            const uint8_t* b = data + off;
            int pi[2], delta[2], s1[2], s2[2];
            for (int c = 0; c < ch; ++c) pi[c] = std::min((int)b[c], (int)c1.size() - 1);
            for (int c = 0; c < ch; ++c) delta[c] = (int16_t)rd16(b + ch + 2 * c);
            for (int c = 0; c < ch; ++c) s1[c] = (int16_t)rd16(b + 3 * ch + 2 * c);
            for (int c = 0; c < ch; ++c) s2[c] = (int16_t)rd16(b + 5 * ch + 2 * c);
            for (int c = 0; c < ch; ++c) pcm.push_back((int16_t)s2[c]);
            for (int c = 0; c < ch; ++c) pcm.push_back((int16_t)s1[c]);
            const uint8_t* p = b + 7 * ch;
            const uint8_t* end = b + blockAlign;
            int c = 0;
            for (long long k = 0; k < (long long)(spb - 2) * ch && p < end; ++k) {
                const int nib = (k & 1) == 0 ? (*p >> 4) : (*p & 0x0F);
                if (k & 1) ++p;
                const int sn = nib >= 8 ? nib - 16 : nib;
                int pred = ((s1[c] * c1[(size_t)pi[c]] + s2[c] * c2[(size_t)pi[c]]) >> 8) + sn * delta[c];
                pred = std::clamp(pred, -32768, 32767);
                s2[c] = s1[c];
                s1[c] = pred;
                delta[c] = std::max(16, (adapt[nib] * delta[c]) >> 8);
                pcm.push_back((int16_t)pred);
                c = (c + 1) % ch;
            }
        }
    } else {
        return false;
    }
    const size_t frames = pcm.size() / (size_t)ch;
    if (frames == 0) return false;
    a.L.resize(frames);
    a.R.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        a.L[i] = pcm[i * (size_t)ch] / 32768.f;
        a.R[i] = pcm[i * (size_t)ch + (ch > 1 ? 1 : 0)] / 32768.f;
    }
    return true;
}

}  // namespace

bool wavMp3Payload(const std::vector<uint8_t>& b, size_t& offset, size_t& length) {
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return false;
    bool mp3 = false;
    for (size_t pos = 12; pos + 8 <= b.size();) {
        const size_t size = rd32(b.data() + pos + 4), body = pos + 8;
        const size_t end = std::min(b.size(), body + size);
        if (!std::memcmp(b.data() + pos, "fmt ", 4) && end - body >= 2) mp3 = rd16(b.data() + body) == 0x55;
        if (!std::memcmp(b.data() + pos, "data", 4) && mp3) {
            offset = body;
            length = end - body;
            return true;
        }
        if (end <= pos) break;
        pos = end + (size & 1);
    }
    return false;
}

bool parseWAV(const std::vector<uint8_t>& b, Audio& a, std::string& err) {
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
        err = "ce fichier n'est pas un WAV valide";
        return false;
    }
    int tag = 0, ch = 0, bits = 0, rate = 0, blockAlign = 0;
    const uint8_t* fmt = nullptr;
    size_t fmtLen = 0;
    long long factFrames = -1;  // bloc « fact » : vrai nombre d'échantillons (WAV compressés)
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
            blockAlign = rd16(c + 12);
            bits = rd16(c + 14);
            fmt = c;
            fmtLen = len;
            if (tag == 0xFFFE && len >= 26) tag = rd16(c + 24);
            haveFmt = true;
        } else if (!std::memcmp(id, "data", 4)) {
            data = c;
            dataLen = len;
        } else if (!std::memcmp(id, "fact", 4) && len >= 4) {
            factFrames = rd32(c);
        } else if (!std::memcmp(id, "smpl", 4) && len >= 60 && rd32(c + 28) > 0) {
            // première boucle : début (offset 44) et fin incluse (offset 48)
            const int st = (int)rd32(c + 44), en = (int)rd32(c + 48) + 1;
            if (st >= 0 && en > st) { a.loopStart = st; a.loopEnd = en; }
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
    // WAV compressés courants dans les packs de loops
    if (tag == 6 || tag == 7 || tag == 0x11 || tag == 0x02) {
        a.rate = rate;
        if (!decodeCompressedWav(tag, ch, blockAlign, fmt, fmtLen, data, dataLen, a)) {
            err = "WAV compressé abîmé";
            return false;
        }
        // le dernier bloc est complété par du silence : on garde la vraie longueur
        if (factFrames > 0 && factFrames < a.frames()) {
            a.L.resize((size_t)factFrames);
            a.R.resize((size_t)factFrames);
        }
        return true;
    }
    if (tag == 0x55) {
        err = "WAV contenant du MP3";  // décodé par le lecteur MP3 du plugin (voir wavMp3Payload)
        return false;
    }
    int bps = bits / 8;
    if ((tag != 1 && tag != 3) || bps < 1 || bps > 8 || (tag == 3 && bps != 4 && bps != 8)) {
        err = "ce WAV utilise une compression non prise en charge (format " + std::to_string(tag) + ")";
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
    if (!parseWAV(b, out, err)) return false;
    prepareLoop(out, false);
    return true;
}

void prepareLoop(Audio& a, bool fromMp3) {
    const int n = a.frames();
    if (n == 0) return;
    auto crop = [&](int from, int to) {
        a.L = std::vector<float>(a.L.begin() + from, a.L.begin() + to);
        a.R = std::vector<float>(a.R.begin() + from, a.R.begin() + to);
    };
    // zone de boucle écrite dans le fichier (au moins 0,1 s)
    if (a.loopEnd > a.loopStart && a.loopEnd <= n && a.loopEnd - a.loopStart >= a.rate / 10 &&
        (a.loopStart > 0 || a.loopEnd < n))
        crop(a.loopStart, a.loopEnd);
    a.loopStart = a.loopEnd = 0;
    if (!fromMp3) return;
    // silence de l'encodeur MP3 : au plus ~60 ms au début et à la fin, presque nul
    const int maxPad = std::min(a.frames() / 4, a.rate * 60 / 1000);
    constexpr float quiet = 3e-4f;
    auto silent = [&](int i) { return std::abs(a.L[(size_t)i]) < quiet && std::abs(a.R[(size_t)i]) < quiet; };
    int head = 0, tail = 0;
    while (head < maxPad && silent(head)) ++head;
    while (tail < maxPad && silent(a.frames() - 1 - tail)) ++tail;
    // un silence plus long fait partie du rythme (loop qui commence par un soupir) : on n'y touche pas
    if (head >= maxPad) head = 0;
    if (tail >= maxPad) tail = 0;
    if (head > 0 || tail > 0) crop(head, a.frames() - tail);
}

double readMp3Bpm(const std::vector<uint8_t>& b) {
    // ID3v2 : « ID3 », version, drapeaux, taille sur 4 × 7 bits
    if (b.size() < 10 || std::memcmp(b.data(), "ID3", 3) != 0) return 0;
    const int ver = b[3];
    const size_t size = ((size_t)(b[6] & 0x7f) << 21) | ((size_t)(b[7] & 0x7f) << 14) | ((size_t)(b[8] & 0x7f) << 7) | (b[9] & 0x7f);
    const size_t end = std::min(b.size(), 10 + size);
    size_t p = 10;
    if (ver == 2) {  // ID3v2.2 : identifiants de 3 lettres
        while (p + 6 <= end) {
            const size_t fs = ((size_t)b[p + 3] << 16) | ((size_t)b[p + 4] << 8) | b[p + 5];
            if (b[p] == 0 || p + 6 + fs > end) break;
            if (!std::memcmp(&b[p], "TBP", 3) && fs > 1) {
                std::string t(b.begin() + (long)p + 7, b.begin() + (long)(p + 6 + fs));
                double v = std::atof(t.c_str());
                return (v >= 40 && v <= 300) ? v : 0;
            }
            p += 6 + fs;
        }
        return 0;
    }
    while (p + 10 <= end) {
        size_t fs = ver >= 4 ? (((size_t)(b[p + 4] & 0x7f) << 21) | ((size_t)(b[p + 5] & 0x7f) << 14) |
                                ((size_t)(b[p + 6] & 0x7f) << 7) | (b[p + 7] & 0x7f))
                             : rd32be(&b[p + 4]);
        if (b[p] == 0 || p + 10 + fs > end) break;
        if (!std::memcmp(&b[p], "TBPM", 4) && fs > 1) {
            // texte : premier octet = codage ; on garde les chiffres (UTF-16 compris)
            std::string t;
            for (size_t i = p + 11; i < p + 10 + fs; ++i)
                if ((b[i] >= '0' && b[i] <= '9') || b[i] == '.' || b[i] == ',') t += b[i] == ',' ? '.' : (char)b[i];
            double v = std::atof(t.c_str());
            return (v >= 40 && v <= 300) ? v : 0;
        }
        p += 10 + fs;
    }
    return 0;
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

namespace africanloops {

namespace {
// Longueur en octets d'un bloc MP3 (couche III) qui commence en b[p], 0 si ce n'est pas un en-tête valide.
size_t mp3FrameLength(const std::vector<uint8_t>& b, size_t p) {
    if (p + 4 > b.size() || b[p] != 0xFF || (b[p + 1] & 0xE0) != 0xE0) return 0;
    const int version = (b[p + 1] >> 3) & 3;  // 3 = MPEG-1, 2 = MPEG-2, 0 = MPEG-2.5
    const int layer = (b[p + 1] >> 1) & 3;    // 1 = couche III
    const int br = b[p + 2] >> 4, sr = (b[p + 2] >> 2) & 3, pad = (b[p + 2] >> 1) & 1;
    if (version == 1 || layer != 1 || br == 0 || br == 15 || sr == 3) return 0;
    static constexpr int rates1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static constexpr int rates2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static constexpr int srs[3][3] = {{11025, 12000, 8000}, {22050, 24000, 16000}, {44100, 48000, 32000}};
    const bool v1 = version == 3;
    const int bitrate = (v1 ? rates1 : rates2)[br] * 1000;
    const int rate = srs[v1 ? 2 : version == 2 ? 1 : 0][sr];
    return (size_t)((v1 ? 144 : 72) * bitrate / rate + pad);
}
}  // namespace

size_t mp3AudioStart(const std::vector<uint8_t>& b) {
    size_t p = 0;
    // toutes les étiquettes ID3v2 à la suite (avec leur « pied » éventuel)
    while (p + 10 <= b.size() && !std::memcmp(&b[p], "ID3", 3)) {
        const size_t sz = ((size_t)(b[p + 6] & 0x7f) << 21) | ((size_t)(b[p + 7] & 0x7f) << 14) |
                          ((size_t)(b[p + 8] & 0x7f) << 7) | (b[p + 9] & 0x7f);
        p += 10 + sz + ((b[p + 5] & 0x10) ? 10 : 0);
    }
    // un vrai bloc : un en-tête valide suivi d'un autre en-tête valide (ou la fin du fichier)
    for (; p + 4 <= b.size(); ++p) {
        const size_t len = mp3FrameLength(b, p);
        if (len < 24) continue;
        if (p + len == b.size() || (p + len + 4 <= b.size() && mp3FrameLength(b, p + len) > 0)) return p;
    }
    return SIZE_MAX;
}

Mp3Gapless readMp3Gapless(const std::vector<uint8_t>& b) {
    Mp3Gapless g;
    const size_t p = mp3AudioStart(b);
    if (p == SIZE_MAX || p + 200 >= b.size()) return g;
    size_t x = std::string::npos;
    for (size_t i = p + 4; i + 4 < std::min(b.size(), p + 64); ++i)
        if (!std::memcmp(&b[i], "Xing", 4) || !std::memcmp(&b[i], "Info", 4)) { x = i; break; }
    if (x == std::string::npos) return g;
    const uint32_t flags = rd32be(&b[x + 4]);
    size_t q = x + 8;
    if (flags & 1) { g.frames = rd32be(&b[q]); q += 4; }
    if (flags & 2) q += 4;    // taille en octets
    if (flags & 4) q += 100;  // table de positions
    if (flags & 8) q += 4;    // qualité
    // extension LAME (ou FFmpeg « Lavc ») : 9 octets de version, puis le retard et le remplissage à +21
    if (q + 24 > b.size()) return g;
    const uint8_t* t = &b[q];
    const bool known = !std::memcmp(t, "LAME", 4) || !std::memcmp(t, "Lavc", 4) || !std::memcmp(t, "Lavf", 4) || !std::memcmp(t, "GOGO", 4);
    if (!known) return g;
    g.delay = (t[21] << 4) | (t[22] >> 4);
    g.padding = ((t[22] & 0x0F) << 8) | t[23];
    g.found = g.delay < 4096 && g.padding < 4096;
    return g;
}

void trimMp3(Audio& a, const Mp3Gapless& g) {
    if (!g.found) return;
    constexpr int decoderDelay = 529;  // retard propre au décodeur MP3
    const long long perFrame = a.rate >= 32000 ? 1152 : 576;  // MPEG-1 : 1152 échantillons par bloc, MPEG-2 : 576
    const long long total = g.frames > 0 ? g.frames * perFrame : a.frames();
    const int start = g.delay + decoderDelay;
    long long len = total - g.delay - g.padding;
    if (start >= a.frames() || len <= 0) return;
    len = std::min<long long>(len, a.frames() - start);
    a.L = std::vector<float>(a.L.begin() + start, a.L.begin() + start + len);
    a.R = std::vector<float>(a.R.begin() + start, a.R.begin() + start + len);
}

}  // namespace africanloops
