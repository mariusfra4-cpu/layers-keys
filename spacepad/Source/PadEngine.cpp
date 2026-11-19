#include "PadEngine.h"
#include "AudioOpen.h"

namespace spacepad
{
// ---------------------------------------------------------------------------------------------
// SampleLoader
// ---------------------------------------------------------------------------------------------
SampleLoader::SampleLoader() : juce::Thread ("Spacepad chargement")
{
    registerAllFormats (fm);
    startThread (juce::Thread::Priority::normal);
}

SampleLoader::~SampleLoader()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
}

void SampleLoader::request (const juce::String& path, Callback done)
{
    {
        const juce::ScopedLock sl (queueLock);
        // une nouvelle demande pour le même canal remplace les anciennes encore en attente
        queue.emplace_back (path, std::move (done));
        while (queue.size() > 4) queue.pop_front();
    }
    wake.signal();
}

void SampleLoader::clearCache()
{
    const juce::ScopedLock sl (cacheLock);
    cache.clear();
}

std::shared_ptr<const SampleData> SampleLoader::loadNow (const juce::String& path)
{
    return getOrLoad (path);
}

std::shared_ptr<const SampleData> SampleLoader::getOrLoad (const juce::String& path)
{
    {
        const juce::ScopedLock sl (cacheLock);
        for (size_t i = 0; i < cache.size(); ++i)
            if (cache[i]->path == path)
            {
                auto s = cache[i];
                cache.erase (cache.begin() + (long) i);
                cache.push_back (s);
                return s;
            }
    }
    auto rd = openAudioReader (fm, juce::File (path));
    if (rd == nullptr || rd->lengthInSamples <= 0 || rd->sampleRate <= 0) return {};

    auto d = std::make_shared<SampleData>();
    d->path = path;
    d->sampleRate = rd->sampleRate;
    const int n = (int) juce::jmin (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 60 * 12)); // 12 minutes au plus
    d->buffer.setSize (2, n);
    if (rd->numChannels >= 2)
        rd->read (&d->buffer, 0, n, 0, true, true);
    else
    {
        rd->read (&d->buffer, 0, n, 0, true, false);
        d->buffer.copyFrom (1, 0, d->buffer, 0, 0, n);
    }
    // enveloppe pour l'affichage
    const int P = 512;
    d->peaks.assign (P, 0.0f);
    float mx = 1e-6f;
    for (int p = 0; p < P; ++p)
    {
        const int a = (int) ((juce::int64) n * p / P), b = juce::jmax (a + 1, (int) ((juce::int64) n * (p + 1) / P));
        float v = 0;
        for (int ch = 0; ch < 2; ++ch)
            v = juce::jmax (v, d->buffer.getMagnitude (ch, a, juce::jmin (b, n) - a));
        d->peaks[(size_t) p] = v;
        mx = juce::jmax (mx, v);
    }
    for (auto& v : d->peaks) v /= mx;

    std::shared_ptr<const SampleData> c = d;
    const juce::ScopedLock sl (cacheLock);
    cache.push_back (c);
    // mémoire : on garde les 10 derniers sons, sans dépasser environ 1,5 Go
    size_t bytes = 0;
    for (auto& x : cache) bytes += (size_t) x->buffer.getNumSamples() * 2 * sizeof (float);
    while (cache.size() > 1 && (cache.size() > 10 || bytes > (size_t) 1500 * 1024 * 1024))
    {
        bytes -= (size_t) cache.front()->buffer.getNumSamples() * 2 * sizeof (float);
        cache.erase (cache.begin());
    }
    return c;
}

void SampleLoader::run()
{
    while (! threadShouldExit())
    {
        std::pair<juce::String, Callback> job;
        bool has = false;
        {
            const juce::ScopedLock sl (queueLock);
            if (! queue.empty()) { job = std::move (queue.front()); queue.pop_front(); has = true; }
        }
        if (! has) { wake.wait (200); continue; }
        auto s = getOrLoad (job.first);
        if (job.second) job.second (s);
    }
}

// ---------------------------------------------------------------------------------------------
// PadChannel
// ---------------------------------------------------------------------------------------------
void PadChannel::prepare (double rate)
{
    hostRate = rate > 0 ? rate : 48000;
}

std::shared_ptr<const SampleData> PadChannel::current() const
{
    const juce::SpinLock::ScopedLockType sl (latestLock);
    return latest;
}

void PadChannel::start (std::shared_ptr<const SampleData> s, int semitones, bool loop, float fadeInSec, float crossfadeSec)
{
    if (s == nullptr || s->buffer.getNumSamples() < 16) return;
    Cmd c;
    c.type = Cmd::Start; c.sample = std::move (s); c.semis = semitones; c.loop = loop; c.fadeIn = fadeInSec; c.xfade = crossfadeSec;
    const juce::SpinLock::ScopedLockType sl (cmdLock);
    cmds.push_back (std::move (c));
}

void PadChannel::stop (float fadeSec)
{
    Cmd c; c.type = Cmd::Stop; c.xfade = fadeSec;
    const juce::SpinLock::ScopedLockType sl (cmdLock);
    cmds.push_back (std::move (c));
}

void PadChannel::setLoop (bool l)
{
    Cmd c; c.type = Cmd::Loop; c.loop = l;
    const juce::SpinLock::ScopedLockType sl (cmdLock);
    cmds.push_back (std::move (c));
}

void PadChannel::applyCommands()
{
    std::vector<Cmd> todo;
    {
        const juce::SpinLock::ScopedTryLockType sl (cmdLock);
        if (! sl.isLocked() || cmds.empty()) return;
        todo.swap (cmds);   // pas d'allocation : le vecteur vide garde sa capacité de l'autre côté
    }
    for (auto& c : todo)
    {
        if (c.type == Cmd::Loop)
        {
            for (auto& v : voices) if (v.active && v.target > 0) v.loop = c.loop;
            continue;
        }
        const float outSec = juce::jmax (0.01f, c.xfade);
        bool anyAudible = false;
        for (auto& v : voices)
            if (v.active && v.target > 0)
            {
                anyAudible = anyAudible || v.gain > 0.01f;
                v.target = 0;
                v.step = -juce::jmax (v.gain, 0.001f) / (float) (outSec * hostRate);
            }
        if (c.type == Cmd::Stop) continue;

        // voix libre, sinon la plus faible
        Voice* nv = nullptr;
        for (auto& v : voices) if (! v.active) { nv = &v; break; }
        if (nv == nullptr)
        {
            nv = &voices[0];
            for (auto& v : voices) if (v.gain < nv->gain) nv = &v;
        }
        Voice& v = *nv;
        v.s = c.sample;
        v.main = {}; v.sub = {}; v.shim = {};
        v.loop = c.loop;
        const int L = v.s->buffer.getNumSamples();
        v.xfade = juce::jmin (L / 4, (int) (1.5 * v.s->sampleRate));
        v.rate = (v.s->sampleRate / hostRate) * std::pow (2.0, c.semis / 12.0);
        // monte sur le fondu entre tonalités si un son jouait déjà, sinon sur l'attaque
        const float inSec = juce::jmax (0.005f, anyAudible ? juce::jmax (c.xfade, 0.01f) : c.fadeIn);
        v.gain = 0; v.target = 1; v.step = 1.0f / (float) (inSec * hostRate);
        v.active = true;
        const juce::SpinLock::ScopedLockType sl (latestLock);
        latest = v.s;
    }
    // les anciennes données (sons libérés) sont rendues hors de ce bloc : "todo" garde les pointeurs jusqu'ici seulement
}

void PadChannel::render (juce::AudioBuffer<float>& out, juce::AudioBuffer<float>* sub, juce::AudioBuffer<float>* shim, int start, int num)
{
    applyCommands();
    bool any = false;
    const Voice* newest = nullptr;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const SampleData& s = *v.s;
        float* o0 = out.getWritePointer (0, start);
        float* o1 = out.getWritePointer (1, start);
        float* s0 = sub ? sub->getWritePointer (0, start) : nullptr;
        float* s1 = sub ? sub->getWritePointer (1, start) : nullptr;
        float* h0 = shim ? shim->getWritePointer (0, start) : nullptr;
        float* h1 = shim ? shim->getWritePointer (1, start) : nullptr;
        for (int i = 0; i < num; ++i)
        {
            v.gain += v.step;
            if (v.step > 0 && v.gain >= v.target) { v.gain = v.target; v.step = 0; }
            if (v.step < 0 && v.gain <= 0) { v.gain = 0; v.active = false; break; }
            const float g = v.gain * v.gain * (3.0f - 2.0f * v.gain); // courbe douce
            o0[i] += g * v.main.read (s, 0, v.loop, v.xfade);
            o1[i] += g * v.main.read (s, 1, v.loop, v.xfade);
            v.main.advance (s, v.rate, v.loop, v.xfade);
            if (s0 != nullptr)
            {
                s0[i] += g * v.sub.read (s, 0, true, v.xfade);
                s1[i] += g * v.sub.read (s, 1, true, v.xfade);
                v.sub.advance (s, v.rate * 0.5, true, v.xfade);
            }
            if (h0 != nullptr)
            {
                h0[i] += g * v.shim.read (s, 0, true, v.xfade);
                h1[i] += g * v.shim.read (s, 1, true, v.xfade);
                v.shim.advance (s, v.rate * 2.0, true, v.xfade);
            }
            if (v.main.finished) { v.active = false; break; }
        }
        if (! v.active) { v.s.reset(); continue; } // libéré dans le fil audio : rare (fin de fondu)
        any = true;
        if (v.target > 0) newest = &v;
    }
    playing = any;
    if (newest != nullptr)
        pos01 = (float) (newest->main.pos / juce::jmax (1, newest->s->buffer.getNumSamples()));
}
}
