#include "KeyDetect.h"
#include <juce_dsp/juce_dsp.h>

namespace spacepad
{
static const char* const kNames[12]   = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char* const kNamesFr[12] = { "Do", "Do#", "R\xc3\xa9", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };

juce::String keyName (int k)   { return (k >= 0 && k < 12) ? juce::String (kNames[k]) : juce::String ("-"); }
juce::String keyNameFr (int k) { return (k >= 0 && k < 12) ? juce::String::fromUTF8 (kNamesFr[k]) : juce::String ("-"); }
juce::String keyLabel (int k, int mode) { return keyName (k) + (mode == 1 ? "m" : ""); }

// ---------------------------------------------------------------------------------------------
// Tonalité dans un nom
// ---------------------------------------------------------------------------------------------
namespace
{
juce::String fold (juce::String s)
{
    // minuscules sans accents, pour comparer "Ré", "re", "RÉ", "dièse"…
    s = s.toLowerCase();
    const juce::String from = juce::String::fromUTF8 ("\xc3\xa9\xc3\xa8\xc3\xaa\xc3\xa0\xc3\xa2\xc3\xae\xc3\xaf\xc3\xb4\xc3\xbb\xc3\xb9");
    const juce::String to   = "eeeaaiiouu";
    juce::String out;
    for (auto c : s)
    {
        auto i = from.indexOfChar (c);
        out << (i >= 0 ? to[i] : c);
    }
    return out;
}

bool isMinorWord (const juce::String& f) { return f == "m" || f == "min" || f == "minor" || f == "mineur" || f == "menor" || f == "moll" || f == "mi"; }
bool isMajorWord (const juce::String& f) { return f == "maj" || f == "major" || f == "majeur" || f == "mayor" || f == "dur" || f == "ma"; }
bool isSharpWord (const juce::String& f) { return f == "sharp" || f == "diese" || f == "#"; }
bool isFlatWord  (const juce::String& f) { return f == "flat" || f == "bemol"; }

int letterToKey (juce::juce_wchar c)
{
    switch (juce::CharacterFunctions::toUpperCase (c))
    {
        case 'C': return 0; case 'D': return 2; case 'E': return 4; case 'F': return 5;
        case 'G': return 7; case 'A': return 9; case 'B': return 11; default: return -1;
    }
}

struct Match { int key = -1, mode = -1; float strength = 0; };

// "C", "C#", "Db", "Ebm", "F#min", "Am", "C3", "AM" (A majeur)…
Match parseEnglish (const juce::String& tok)
{
    Match m;
    if (tok.isEmpty()) return m;
    int k = letterToKey (tok[0]);
    if (k < 0) return m;
    juce::String rest = tok.substring (1);
    bool hasAcc = false;
    if (rest.startsWith ("#") || rest.startsWith (juce::String::fromUTF8 ("\xe2\x99\xaf"))) { k += 1; hasAcc = true; rest = rest.startsWith ("#") ? rest.substring (1) : rest.substring (1); }
    else if (rest.startsWith (juce::String::fromUTF8 ("\xe2\x99\xad"))) { k -= 1; hasAcc = true; rest = rest.substring (1); }
    else if (rest.startsWith ("b") && ! rest.startsWith ("ba")) { k -= 1; hasAcc = true; rest = rest.substring (1); }
    // chiffre d'octave à la fin (C3, Eb2)
    while (rest.isNotEmpty() && juce::CharacterFunctions::isDigit (rest.getLastCharacter()))
        rest = rest.dropLastCharacters (1);
    int mode = -1;
    if (rest == "M") mode = 0;
    else if (rest.isNotEmpty())
    {
        auto f = fold (rest);
        if (isMinorWord (f)) mode = 1;
        else if (isMajorWord (f)) mode = 0;
        else return {}; // autre mot ("Choir", "Bass"…) : pas une tonalité
    }
    m.key = (k + 12) % 12;
    m.mode = mode;
    const bool single = tok.length() == 1;
    m.strength = (hasAcc || mode >= 0) ? 2.0f : (single ? (juce::CharacterFunctions::isUpperCase (tok[0]) ? 0.9f : 0.4f) : 1.0f);
    return m;
}

// "Do", "Ré", "Mib", "Fa#", "Solm", "La mineur"…
Match parseFrench (const juce::String& tok)
{
    static const std::pair<const char*, int> roots[] = { { "sol", 7 }, { "do", 0 }, { "re", 2 }, { "mi", 4 }, { "fa", 5 }, { "la", 9 }, { "si", 11 } };
    auto f = fold (tok);
    for (auto& r : roots)
    {
        juce::String root (r.first);
        if (! f.startsWith (root)) continue;
        auto rest = f.substring (root.length());
        int k = r.second;
        bool acc = false;
        if (rest.startsWith ("#")) { k++; acc = true; rest = rest.substring (1); }
        else if (rest.startsWith ("diese")) { k++; acc = true; rest = rest.substring (5); }
        else if (rest.startsWith ("bemol")) { k--; acc = true; rest = rest.substring (5); }
        else if (rest.startsWith ("b") && rest.length() <= 4) { k--; acc = true; rest = rest.substring (1); }
        int mode = -1;
        if (rest.isNotEmpty())
        {
            if (rest == "m" || rest == "min" || rest == "mineur" || rest == "menor") mode = 1;
            else if (rest == "maj" || rest == "majeur" || rest == "mayor") mode = 0;
            else return {};
        }
        Match m;
        m.key = (k + 12) % 12;
        m.mode = mode;
        m.strength = (acc || mode >= 0) ? 1.5f : 0.8f;
        return m;
    }
    return {};
}
} // namespace

KeyInfo keyFromName (const juce::String& nameIn)
{
    juce::String name = nameIn;
    for (auto c : juce::String ("_-.,()[]{}+&=~'\"/\\|"))
        name = name.replaceCharacter (c, ' ');
    auto tokens = juce::StringArray::fromTokens (name, " \t", "");
    tokens.removeEmptyStrings();

    // dernier mot qui n'est pas un nombre : une note seule ("A", "la", "Do") n'est crue qu'à cet endroit
    int lastWord = -1;
    for (int i = 0; i < tokens.size(); ++i)
        if (! tokens[i].containsOnly ("0123456789")) lastWord = i;

    Match best;
    for (int i = 0; i < tokens.size(); ++i)
    {
        Match m = parseEnglish (tokens[i]);
        bool french = false;
        if (m.key < 0) { m = parseFrench (tokens[i]); french = m.key >= 0; }
        if (m.key < 0) continue;

        // mots suivants : "C sharp minor", "Mi bémol mineur", "E minor", "D maj"
        for (int j = i + 1; j < juce::jmin (tokens.size(), i + 3); ++j)
        {
            auto f = fold (tokens[j]);
            if (isSharpWord (f)) { m.key = (m.key + 1) % 12; m.strength = juce::jmax (m.strength, 2.0f); }
            else if (isFlatWord (f)) { m.key = (m.key + 11) % 12; m.strength = juce::jmax (m.strength, 2.0f); }
            else if (m.mode < 0 && (isMinorWord (f) && f != "mi")) { m.mode = 1; m.strength = juce::jmax (m.strength, 2.0f); }
            else if (m.mode < 0 && isMajorWord (f)) { m.mode = 0; m.strength = juce::jmax (m.strength, 2.0f); }
            else break;
        }
        if (m.strength < 1.0f && i != lastWord) continue;
        // un nom français ne gagne que si aucune note anglaise n'est écrite
        if (french && best.key >= 0 && best.strength >= 1.0f) continue;
        if (m.strength >= best.strength) best = m; // à égalité : la dernière note du nom gagne
    }
    KeyInfo k;
    if (best.key >= 0 && best.strength >= 0.4f) { k.key = best.key; k.mode = best.mode; k.confidence = 1.0f; }
    return k;
}

int modeFromFolderName (const juce::String& nameIn)
{
    auto name = nameIn;
    for (auto c : juce::String ("_-.,()[]{}+&"))
        name = name.replaceCharacter (c, ' ');
    auto tokens = juce::StringArray::fromTokens (name, " ", "");
    for (auto& t : tokens)
    {
        auto f = fold (t);
        if (f == "minor" || f == "mineur" || f == "minors" || f == "mineurs" || f == "menor" || f == "menores" || f == "min" || f == "moll") return 1;
        if (f == "major" || f == "majeur" || f == "majors" || f == "majeurs" || f == "mayor" || f == "mayores" || f == "maj" || f == "dur") return 0;
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------
// Tonalité à l'écoute
// ---------------------------------------------------------------------------------------------
std::array<float, 12> chromaOf (const juce::AudioBuffer<float>& mono, double sr)
{
    std::array<float, 12> chroma {};
    constexpr int order = 14, N = 1 << order, hop = N / 2;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win (N, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> buf ((size_t) N * 2);
    const int len = mono.getNumSamples();
    const float* x = mono.getReadPointer (0);
    const double binHz = sr / N;
    const int lo = (int) std::ceil (60.0 / binHz), hi = juce::jmin (N / 2 - 2, (int) (2200.0 / binHz));

    // à l'avance : classe de note de chaque bin
    std::vector<int> pc ((size_t) N / 2, -1);
    for (int b = lo; b <= hi; ++b)
    {
        double midi = 69.0 + 12.0 * std::log2 ((b * binHz) / 440.0);
        int n = (int) std::lround (midi);
        if (std::abs (midi - n) < 0.35) pc[(size_t) b] = ((n % 12) + 12) % 12;
    }
    for (int start = 0; start + N <= juce::jmax (len, N); start += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N && start + i < len; ++i) buf[(size_t) i] = x[start + i];
        win.multiplyWithWindowingTable (buf.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        float frameMax = 1e-9f;
        for (int b = lo; b <= hi; ++b) frameMax = juce::jmax (frameMax, buf[(size_t) b]);
        for (int b = lo + 1; b < hi; ++b)
        {
            const float v = buf[(size_t) b];
            if (pc[(size_t) b] < 0 || v < buf[(size_t) b - 1] || v < buf[(size_t) b + 1] || v < frameMax * 0.01f) continue; // pics seulement
            chroma[(size_t) pc[(size_t) b]] += std::sqrt (v / frameMax);
        }
        if (start + N >= len) break;
    }
    return chroma;
}

static float pearson (const float* a, const float* b, int n)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < n; ++i) { num += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) * (a[i] - ma); db += (b[i] - mb) * (b[i] - mb); }
    return (da <= 0 || db <= 0) ? 0.0f : (float) (num / std::sqrt (da * db));
}

AudioKeyResult keyFromAudio (const juce::AudioBuffer<float>& mono, double sr)
{
    AudioKeyResult r;
    if (mono.getNumSamples() < 2048 || sr <= 0) return r;
    auto chroma = chromaOf (mono, sr);
    float sum = 0, mx = 0;
    for (auto v : chroma) { sum += v; mx = juce::jmax (mx, v); }
    if (sum <= 1e-6f) return r;

    // profils de Krumhansl–Kessler (majeur / mineur), tonique en position 0
    static const float major[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float minor[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    float best = -2; int bestKey = 0, bestMode = 0;
    for (int mode = 0; mode < 2; ++mode)
        for (int k = 0; k < 12; ++k)
        {
            float rot[12];
            for (int i = 0; i < 12; ++i) rot[i] = chroma[(size_t) ((i + k) % 12)];
            float c = pearson (rot, mode == 0 ? major : minor, 12);
            if (c > best) { best = c; bestKey = k; bestMode = mode; }
        }
    // un son "plat" (bruit, foule, ambiance) n'a pas de note qui ressort
    const float peakiness = mx / (sum / 12.0f);
    r.tonalStrength = juce::jlimit (0.0f, 1.0f, best * juce::jlimit (0.0f, 1.0f, (peakiness - 1.3f) / 1.2f));
    r.key.key = bestKey;
    r.key.mode = bestMode;
    r.key.confidence = r.tonalStrength;
    return r;
}

AudioKeyResult keyFromFile (juce::AudioFormatManager& fm, const juce::File& f, double maxSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr || rd->sampleRate <= 0 || rd->lengthInSamples <= 0) return {};
    const auto total = rd->lengthInSamples;
    const auto want = (juce::int64) (maxSeconds * rd->sampleRate);
    juce::int64 start = juce::jmin ((juce::int64) (total / 10), (juce::int64) (3.0 * rd->sampleRate)); // après l'attaque
    if (total - start < want) start = juce::jmax ((juce::int64) 0, total - want);
    const int n = (int) juce::jmin (want, total - start);
    const int ch = (int) juce::jmin ((unsigned int) 2, rd->numChannels);
    juce::AudioBuffer<float> tmp (ch, n);
    rd->read (&tmp, 0, n, start, true, ch > 1);
    juce::AudioBuffer<float> mono (1, n);
    mono.copyFrom (0, 0, tmp, 0, 0, n);
    if (ch > 1) { mono.addFrom (0, 0, tmp, 1, 0, n); mono.applyGain (0.5f); }
    return keyFromAudio (mono, rd->sampleRate);
}
}
