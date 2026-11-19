// Spacepad by JB Praise — moteur de lecture : sons en boucle sans clic, fondu entre tonalités,
// transposition, couche une octave plus bas (Sub) et une octave plus haut (Shimmer).
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>

namespace spacepad
{
struct SampleData
{
    juce::String path;
    juce::AudioBuffer<float> buffer;      // stéréo
    double sampleRate = 44100;
    std::vector<float> peaks;             // enveloppe pour l'affichage (512 valeurs, 0..1)
    // boucle : après un premier passage depuis le début, le son tourne dans [loopStart, loopEnd),
    // la jointure est fondue sur loopXfade échantillons (pris juste après loopEnd)
    int loopStart = 0, loopEnd = 0, loopXfade = 0;
};
void computeLoopPoints (SampleData&);     // trouve la partie « pleine » du son (sans la montée ni la fin qui s'éteint)

// Chargement des fichiers dans un fil séparé + mémoire des derniers sons utilisés
class SampleLoader : private juce::Thread
{
public:
    SampleLoader();
    ~SampleLoader() override;

    using Callback = std::function<void (std::shared_ptr<const SampleData>)>;
    void request (const juce::String& path, Callback done);       // appelé depuis l'interface ou le fil audio (sans attente)
    std::shared_ptr<const SampleData> loadNow (const juce::String& path); // chargement direct (tests, préparation)
    void clearCache();

private:
    void run() override;
    std::shared_ptr<const SampleData> getOrLoad (const juce::String& path);

    juce::AudioFormatManager fm;
    juce::CriticalSection queueLock, cacheLock;
    std::deque<std::pair<juce::String, Callback>> queue;
    std::vector<std::shared_ptr<const SampleData>> cache;           // les plus récents à la fin
    juce::WaitableEvent wake;
};

// Lecture d'un son : position, boucle avec fondu enchaîné à la jointure
struct LoopReader
{
    double pos = 0;
    bool looped = false;
    bool finished = false;
    inline float read (const SampleData& s, int ch, bool loop) const;
    inline void advance (const SampleData& s, double rate, bool loop);
};

class PadChannel
{
public:
    void prepare (double hostRate);
    // démarre un son : l'ancien s'efface en "crossfadeSec", le nouveau monte en "fadeInSec"
    void start (std::shared_ptr<const SampleData> s, int semitones, bool loop, float fadeInSec, float crossfadeSec);
    void stop (float fadeSec);
    void setLoop (bool l);
    // ajoute le son dans main (stéréo), la couche grave dans sub, la couche aiguë dans shim
    void render (juce::AudioBuffer<float>& main, juce::AudioBuffer<float>* sub, juce::AudioBuffer<float>* shim, int start, int num);

    bool isPlaying() const { return playing.load(); }
    float position() const { return pos01.load(); }                  // 0..1 (affichage)
    std::shared_ptr<const SampleData> current() const;

private:
    struct Voice
    {
        std::shared_ptr<const SampleData> s;
        LoopReader main, sub, shim;
        double rate = 1;
        float gain = 0, target = 0, step = 0;
        bool loop = true, active = false;
    };
    struct Cmd
    {
        enum Type { Start, Stop, Loop } type = Start;
        std::shared_ptr<const SampleData> sample;
        int semis = 0;
        bool loop = true;
        float fadeIn = 1, xfade = 1;
    };
    void applyCommands();
    std::vector<Cmd> cmds;
    juce::SpinLock cmdLock;

    static constexpr int maxVoices = 6;
    std::array<Voice, maxVoices> voices;
    double hostRate = 48000;
    std::atomic<bool> playing { false };
    std::atomic<float> pos01 { 0 };
    std::shared_ptr<const SampleData> latest;
    mutable juce::SpinLock latestLock;
};

inline float LoopReader::read (const SampleData& s, int ch, bool loop) const
{
    const int L = s.buffer.getNumSamples();
    const float* d = s.buffer.getReadPointer (ch);
    const int A = s.loopStart, E = s.loopEnd, X = s.loopXfade, len = E - A;
    auto value = [&] (int i) -> float
    {
        if (loop && len > 0)
        {
            bool joined = looped;                       // après le premier tour, le début de la boucle est mélangé avec ce qui suit sa fin
            if (i >= E) { i -= len; joined = true; }
            if (i < 0 || i >= L) return 0.0f;
            if (joined && i >= A && i < A + X && X > 0 && i + len < L)
            {
                const float t = (float) (i - A) / (float) X;
                const float in = std::sin (t * juce::MathConstants<float>::halfPi);
                const float out = std::cos (t * juce::MathConstants<float>::halfPi);
                return d[i] * in + d[i + len] * out;
            }
            return d[i];
        }
        return (i >= 0 && i < L) ? d[i] : 0.0f;
    };
    const int i0 = (int) pos;
    const float fr = (float) (pos - i0);
    return value (i0) + (value (i0 + 1) - value (i0)) * fr;
}

inline void LoopReader::advance (const SampleData& s, double rate, bool loop)
{
    pos += rate;
    const int L = s.buffer.getNumSamples();
    const int len = s.loopEnd - s.loopStart;
    if (loop && len > 0)
    {
        while (pos >= s.loopEnd) { pos -= len; looped = true; }
    }
    else if (pos >= L)
        finished = true;
}
}
