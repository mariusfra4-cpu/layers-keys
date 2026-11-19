#include "AudioOpen.h"

namespace spacepad
{
void registerAllFormats (juce::AudioFormatManager& fm)
{
    if (fm.getNumKnownFormats() == 0) fm.registerBasicFormats();   // + WMA sous Windows, M4A/AAC/CAF sur Mac
}

const juce::StringArray& audioExtensions()
{
    static const juce::StringArray ext = []
    {
        juce::StringArray e { ".wav", ".wave", ".bwf", ".aif", ".aiff", ".aifc", ".flac", ".mp3", ".ogg" };
        juce::AudioFormatManager fm;
        registerAllFormats (fm);
        for (int i = 0; i < fm.getNumKnownFormats(); ++i)
            for (auto x : fm.getKnownFormat (i)->getFileExtensions())
            {
                x = x.trim().toLowerCase();
                if (x.isNotEmpty() && ! x.startsWithChar ('.')) x = "." + x;
                if (x.length() > 1) e.addIfNotAlreadyThere (x);
            }
        return e;
    }();
    return ext;
}

juce::String audioWildcard()
{
    juce::StringArray w;
    for (auto& e : audioExtensions()) w.add ("*" + e);
    return w.joinIntoString (";");
}

// ---------------------------------------------------------------------------------------------
// Lecteur de secours : le son entier décodé en mémoire
// ---------------------------------------------------------------------------------------------
namespace
{
class MemoryReader : public juce::AudioFormatReader
{
public:
    MemoryReader (juce::AudioBuffer<float>&& b, double rate) : juce::AudioFormatReader (nullptr, "Spacepad WAV"), data (std::move (b))
    {
        sampleRate = rate;
        numChannels = (unsigned int) data.getNumChannels();
        lengthInSamples = data.getNumSamples();
        bitsPerSample = 32;
        usesFloatingPointData = true;
    }
    bool readSamples (int* const* dest, int numDest, int startOffset, juce::int64 startSample, int num) override
    {
        for (int ch = 0; ch < numDest; ++ch)
        {
            if (dest[ch] == nullptr) continue;
            auto* out = reinterpret_cast<float*> (dest[ch]) + startOffset;
            const int src = juce::jmin (ch, data.getNumChannels() - 1);
            for (int i = 0; i < num; ++i)
            {
                const auto s = startSample + i;
                out[i] = (s >= 0 && s < lengthInSamples) ? data.getSample (src, (int) s) : 0.0f;
            }
        }
        return true;
    }
private:
    juce::AudioBuffer<float> data;
};

inline juce::uint16 u16 (const juce::uint8* p) { return (juce::uint16) (p[0] | (p[1] << 8)); }
inline juce::uint32 u32 (const juce::uint8* p) { return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24); }
inline juce::int16 s16 (const juce::uint8* p) { return (juce::int16) u16 (p); }

float alaw (juce::uint8 a)
{
    a ^= 0x55;
    int t = (a & 0x0f) << 4;
    const int seg = (a & 0x70) >> 4;
    if (seg == 0) t += 8; else if (seg == 1) t += 0x108; else { t += 0x108; t <<= seg - 1; }
    return (float) ((a & 0x80) ? t : -t) / 32768.0f;
}

float ulaw (juce::uint8 u)
{
    u = (juce::uint8) ~u;
    int t = ((u & 0x0f) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return (float) ((u & 0x80) ? (0x84 - t) : (t - 0x84)) / 32768.0f;
}

const int imaIndex[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
const int imaStep[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
                          253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
                          3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };

struct Ima
{
    int pred = 0, index = 0;
    int next (int nib)
    {
        const int step = imaStep[index];
        int diff = step >> 3;
        if (nib & 1) diff += step >> 2;
        if (nib & 2) diff += step >> 1;
        if (nib & 4) diff += step;
        pred += (nib & 8) ? -diff : diff;
        pred = juce::jlimit (-32768, 32767, pred);
        index = juce::jlimit (0, 88, index + imaIndex[nib & 15]);
        return pred;
    }
};

const int msAdapt[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
const int msCoefDefault[7][2] = { { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 }, { 240, 0 }, { 460, -208 }, { 392, -232 } };

// décode le WAV en mémoire ; vide si impossible (reason dit pourquoi)
juce::AudioBuffer<float> decodeWav (const juce::MemoryBlock& mb, double& rate, juce::String& reason)
{
    juce::AudioBuffer<float> out;
    const auto* d = static_cast<const juce::uint8*> (mb.getData());
    const size_t len = mb.getSize();
    if (len < 12 || ! ((memcmp (d, "RIFF", 4) == 0 || memcmp (d, "RF64", 4) == 0 || memcmp (d, "BW64", 4) == 0) && memcmp (d + 8, "WAVE", 4) == 0))
    { reason = "ce n'est pas un vrai fichier WAV"; return out; }
    int tag = 0, chans = 0, bits = 0, blockAlign = 0;
    rate = 0;
    std::vector<std::array<int, 2>> coefs;
    size_t dataOff = 0, dataLen = 0;
    juce::uint64 ds64Data = 0;
    size_t pos = 12;
    while (pos + 8 <= len)
    {
        const auto* c = d + pos;
        juce::uint64 sz = u32 (c + 4);
        const size_t body = pos + 8;
        if (memcmp (c, "ds64", 4) == 0 && body + 16 <= len) ds64Data = (juce::uint64) u32 (d + body + 8) | ((juce::uint64) u32 (d + body + 12) << 32);
        if (memcmp (c, "fmt ", 4) == 0 && body + 16 <= len)
        {
            tag = u16 (d + body); chans = u16 (d + body + 2); rate = (double) u32 (d + body + 4);
            blockAlign = u16 (d + body + 12); bits = u16 (d + body + 14);
            if (tag == 0xFFFE && sz >= 40 && body + 26 <= len) tag = u16 (d + body + 24);            // WAVE_FORMAT_EXTENSIBLE
            if (tag == 2 && sz >= 22 && body + 22 <= len)
            {
                const int n = u16 (d + body + 20);
                for (int i = 0; i < n && body + 22 + (size_t) i * 4 + 4 <= len; ++i)
                    coefs.push_back ({ s16 (d + body + 22 + i * 4), s16 (d + body + 24 + i * 4) });
            }
        }
        if (memcmp (c, "data", 4) == 0)
        {
            dataOff = body;
            if (sz == 0xFFFFFFFFu && ds64Data > 0) sz = ds64Data;
            // taille absente (0), illisible ou plus grande que le fichier : on lit jusqu'au bout
            dataLen = (sz == 0 || body + sz > len) ? len - body : (size_t) sz;
            break;
        }
        if (body + sz > len) break;
        pos = body + (size_t) sz + (sz & 1);
    }
    if (dataOff == 0 || chans <= 0 || chans > 32 || rate < 1000 || rate > 768000) { reason = juce::String::fromUTF8 ("en-t\xc3\xaate WAV incomplet"); return out; }
    const auto* p = d + dataOff;
    if (tag == 1 || tag == 3 || tag == 6 || tag == 7)
    {
        const int bps = juce::jmax (1, (bits + 7) / 8);
        const int frame = bps * chans;
        const int n = (int) juce::jmin ((juce::int64) (dataLen / (size_t) frame), (juce::int64) (rate * 60 * 12));
        if (n <= 0) { reason = "aucun son dans le fichier"; return out; }
        if (! ((tag == 1 && bps >= 1 && bps <= 4) || (tag == 3 && (bps == 4 || bps == 8)) || ((tag == 6 || tag == 7) && bps == 1)))
        { reason = "format WAV non pris en charge (" + juce::String (bits) + " bits)"; return out; }
        out.setSize (chans, n);
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < chans; ++ch)
            {
                const auto* s = p + (size_t) i * frame + (size_t) ch * bps;
                float v = 0;
                if (tag == 1)
                {
                    if (bps == 1) v = ((int) s[0] - 128) / 128.0f;
                    else if (bps == 2) v = s16 (s) / 32768.0f;
                    else if (bps == 3) v = (float) ((juce::int32) ((juce::uint32) s[0] << 8 | (juce::uint32) s[1] << 16 | (juce::uint32) s[2] << 24) >> 8) / 8388608.0f;
                    else v = (float) ((double) (juce::int32) u32 (s) / 2147483648.0);
                }
                else if (tag == 3)
                {
                    if (bps == 4) { float f; memcpy (&f, s, 4); v = f; }
                    else { double f; memcpy (&f, s, 8); v = (float) f; }
                }
                else v = tag == 6 ? alaw (s[0]) : ulaw (s[0]);
                out.setSample (ch, i, std::isfinite (v) ? v : 0.0f);
            }
        return out;
    }
    if ((tag == 0x11 || tag == 2) && blockAlign > 4 * chans && chans <= 2)
    {
        const int blocks = (int) (dataLen / (size_t) blockAlign);
        std::vector<std::vector<float>> s ((size_t) chans);
        for (int b = 0; b < blocks; ++b)
        {
            const auto* blk = p + (size_t) b * blockAlign;
            if (tag == 0x11)
            {
                // IMA ADPCM
                Ima st[2];
                for (int ch = 0; ch < chans; ++ch)
                {
                    st[ch].pred = s16 (blk + ch * 4);
                    st[ch].index = juce::jlimit (0, 88, (int) blk[ch * 4 + 2]);
                    s[(size_t) ch].push_back (st[ch].pred / 32768.0f);
                }
                const auto* q = blk + 4 * chans;
                const int bytes = blockAlign - 4 * chans;
                if (chans == 1)
                    for (int i = 0; i < bytes; ++i)
                    {
                        s[0].push_back (st[0].next (q[i] & 15) / 32768.0f);
                        s[0].push_back (st[0].next (q[i] >> 4) / 32768.0f);
                    }
                else
                    for (int g = 0; g + 8 <= bytes; g += 8)
                        for (int ch = 0; ch < 2; ++ch)
                            for (int i = 0; i < 4; ++i)
                            {
                                const auto byte = q[g + ch * 4 + i];
                                s[(size_t) ch].push_back (st[ch].next (byte & 15) / 32768.0f);
                                s[(size_t) ch].push_back (st[ch].next (byte >> 4) / 32768.0f);
                            }
            }
            else
            {
                // MS ADPCM
                if (coefs.empty()) for (auto& c : msCoefDefault) coefs.push_back ({ c[0], c[1] });
                int pi[2] {}, delta[2] {}, s1[2] {}, s2[2] {};
                size_t o = 0;
                for (int ch = 0; ch < chans; ++ch) pi[ch] = juce::jlimit (0, (int) coefs.size() - 1, (int) blk[o++]);
                for (int ch = 0; ch < chans; ++ch) { delta[ch] = s16 (blk + o); o += 2; }
                for (int ch = 0; ch < chans; ++ch) { s1[ch] = s16 (blk + o); o += 2; }
                for (int ch = 0; ch < chans; ++ch) { s2[ch] = s16 (blk + o); o += 2; }
                for (int ch = 0; ch < chans; ++ch) { s[(size_t) ch].push_back (s2[ch] / 32768.0f); s[(size_t) ch].push_back (s1[ch] / 32768.0f); }
                int ch = 0;
                for (; o < (size_t) blockAlign; ++o)
                    for (int half = 0; half < 2; ++half)
                    {
                        int nib = half == 0 ? (blk[o] >> 4) : (blk[o] & 15);
                        const int sn = nib >= 8 ? nib - 16 : nib;
                        int pr = (s1[ch] * coefs[(size_t) pi[ch]][0] + s2[ch] * coefs[(size_t) pi[ch]][1]) / 256 + sn * delta[ch];
                        pr = juce::jlimit (-32768, 32767, pr);
                        s2[ch] = s1[ch]; s1[ch] = pr;
                        delta[ch] = juce::jmax (16, msAdapt[nib] * delta[ch] / 256);
                        s[(size_t) ch].push_back (pr / 32768.0f);
                        ch = (ch + 1) % chans;
                    }
            }
        }
        size_t n = s[0].size();
        for (auto& v : s) n = juce::jmin (n, v.size());
        if (n == 0) { reason = "aucun son dans le fichier"; return out; }
        out.setSize (chans, (int) n);
        for (int ch = 0; ch < chans; ++ch) out.copyFrom (ch, 0, s[(size_t) ch].data(), (int) n);
        return out;
    }
    reason = "format WAV non pris en charge (code " + juce::String (tag) + ")";
    return out;
}
}

std::unique_ptr<juce::AudioFormatReader> openAudioReader (juce::AudioFormatManager& fm, const juce::File& f, juce::String* reason)
{
    if (! f.existsAsFile()) { if (reason) *reason = "fichier introuvable"; return nullptr; }
    registerAllFormats (fm);
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd != nullptr && rd->lengthInSamples > 0 && rd->sampleRate > 0 && rd->numChannels > 0) return rd;
    rd.reset();
    // les lecteurs standard ont refusé : on essaie notre lecteur WAV (même si l'extension est fausse)
    juce::MemoryBlock mb;
    if (f.getSize() > 0 && f.getSize() < (juce::int64) 2000 * 1024 * 1024 && f.loadFileAsData (mb))
    {
        double rate = 0;
        juce::String why;
        auto buf = decodeWav (mb, rate, why);
        if (buf.getNumSamples() > 0) return std::make_unique<MemoryReader> (std::move (buf), rate);
        if (reason) *reason = why;
    }
    else if (reason) *reason = "fichier vide ou trop gros";
    return nullptr;
}
}
