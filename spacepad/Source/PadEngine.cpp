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

SampleLoader::~SampleLoader() { shutdown(); }

void SampleLoader::shutdown()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (10000);   // un gros fichier en cours de lecture : on attend qu'il finisse
    const juce::ScopedLock sl (queueLock);
    queue.clear(); preloadQueue.clear();
}

void SampleLoader::request (const juce::String& path, Callback done)
{
    // déjà en mémoire : on démarre tout de suite, sans attendre le fil de chargement (Play instantané)
    std::shared_ptr<const SampleData> hit;
    {
        const juce::ScopedLock sl (cacheLock);
        for (auto& c : cache) if (c->path == path) { hit = c; break; }
    }
    if (hit != nullptr) { if (done) done (hit); return; }
    if (threadShouldExit()) return;   // plugin en train de se fermer
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
    try { return getOrLoad (path); } catch (...) { return {}; }
}

void SampleLoader::preload (const juce::StringArray& paths)
{
    {
        const juce::ScopedLock sl (queueLock);
        preloadQueue.clear();
        for (auto& p : paths) if (! isCached (p)) preloadQueue.push_back (p);
    }
    wake.signal();
}

bool SampleLoader::isCached (const juce::String& path) const
{
    const juce::ScopedLock sl (cacheLock);
    for (auto& c : cache) if (c->path == path) return true;
    return false;
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
    computeLoopPoints (*d);
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
    // mémoire : on garde les 48 derniers sons, sans dépasser environ 1,5 Go
    size_t bytes = 0;
    for (auto& x : cache) bytes += (size_t) x->buffer.getNumSamples() * 2 * sizeof (float);
    while (cache.size() > 1 && (cache.size() > 48 || bytes > (size_t) 1500 * 1024 * 1024))
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
            else if (! preloadQueue.empty()) { job.first = preloadQueue.front(); preloadQueue.pop_front(); has = true; }   // préparation d'avance
        }
        if (! has) { wake.wait (200); continue; }
        // un fichier abîmé ou trop gros (mémoire pleine) ne doit jamais faire tomber le logiciel de musique
        std::shared_ptr<const SampleData> s;
        try { s = getOrLoad (job.first); } catch (...) { s = nullptr; }
        try { if (job.second) job.second (s); } catch (...) {}
    }
}

// ---------------------------------------------------------------------------------------------
// Points de boucle : on écoute l'enveloppe du son (fenêtres de 50 ms) et on garde la partie « pleine »,
// entre la montée du début et la fin qui s'éteint. Le début n'est joué qu'au premier passage ;
// ensuite le son tourne dans cette partie, avec un long fondu, sans qu'on entende « ça recommence ».
// ---------------------------------------------------------------------------------------------
void computeLoopPoints (SampleData& d)
{
    const int L = d.buffer.getNumSamples();
    const double sr = d.sampleRate > 0 ? d.sampleRate : 44100;
    // par défaut (son court) : toute la longueur, jointure fondue sur 1/4 du son (au plus 1,5 s)
    auto fallback = [&]
    {
        d.loopXfade = juce::jmin (L / 4, (int) (1.5 * sr));
        d.loopStart = 0;
        d.loopEnd = L - d.loopXfade;
    };
    const int W = juce::jmax (64, (int) (0.05 * sr));
    const int nw = L / W;
    if (L < (int) (1.5 * sr) || nw < 12) { fallback(); return; }
    std::vector<float> env ((size_t) nw);
    for (int w = 0; w < nw; ++w)
    {
        double e = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* p = d.buffer.getReadPointer (ch, w * W);
            for (int i = 0; i < W; ++i) e += (double) p[i] * p[i];
        }
        env[(size_t) w] = (float) std::sqrt (e / (2.0 * W));
    }
    auto sorted = env;
    std::sort (sorted.begin(), sorted.end());
    const float loud = sorted[(size_t) (nw * 3 / 4)];            // niveau « plein » du son
    if (loud < 1e-5f) { fallback(); return; }
    const float thr = loud * 0.7f;                             // -3 dB sous le niveau plein : on reste dans la partie vraiment pleine
    int a = 0, b = nw - 1;
    while (a < nw && env[(size_t) a] < thr) ++a;
    while (b > a && env[(size_t) b] < thr) --b;
    const int sustainStart = a * W, sustainEnd = (b + 1) * W;
    const int sustain = sustainEnd - sustainStart;
    if (sustain < (int) (1.0 * sr)) { fallback(); return; }
    // fondu de la jointure : jusqu'à 3 s, au plus un tiers de la partie pleine
    const int X = juce::jlimit ((int) (0.05 * sr), (int) (3.0 * sr), sustain / 3);
    d.loopXfade = X;
    d.loopStart = sustainStart;
    d.loopEnd = sustainEnd - X;                                // le fondu prend [loopEnd, loopEnd + X), encore dans la partie pleine
    if (d.loopEnd - d.loopStart < X) fallback();
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

void PadChannel::start (std::shared_ptr<const SampleData> s, int semitones, bool loop, float fadeInSec, float crossfadeSec, bool fromFullPart)
{
    if (s == nullptr || s->buffer.getNumSamples() < 16) return;
    if (s->loopEnd <= s->loopStart)
    {
        // son préparé sans points de boucle : on les calcule (copie, hors du fil audio)
        auto withLoop = std::make_shared<SampleData> (*s);
        computeLoopPoints (*withLoop);
        s = withLoop;
    }
    Cmd c;
    c.type = Cmd::Start; c.sample = std::move (s); c.semis = semitones; c.loop = loop; c.fadeIn = fadeInSec; c.xfade = crossfadeSec; c.fromFull = fromFullPart;
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

void PadChannel::retire (std::shared_ptr<const SampleData>& s)
{
    if (s == nullptr) return;
    if (pendingCount < trashSize) pendingTrash[pendingCount++] = std::move (s);
    else s.reset();   // jamais en pratique (64 sons en attente)
}

void PadChannel::flushRetired()
{
    if (pendingCount == 0) return;
    const juce::SpinLock::ScopedTryLockType sl (trashLock);
    if (! sl.isLocked()) return;   // réessayé au bloc suivant
    while (pendingCount > 0 && trashCount < trashSize) trash[trashCount++] = std::move (pendingTrash[--pendingCount]);
}

void PadChannel::releaseRetired()
{
    std::array<std::shared_ptr<const SampleData>, trashSize> out;
    {
        const juce::SpinLock::ScopedLockType sl (trashLock);
        for (size_t i = 0; i < trashCount; ++i) out[i] = std::move (trash[i]);
        trashCount = 0;
    }
}   // "out" libère ici, dans le fil de contrôle

void PadChannel::applyCommands()
{
    {
        const juce::SpinLock::ScopedTryLockType sl (cmdLock);
        if (! sl.isLocked() || cmds.empty()) return;
        cmds.swap (audioCmds);   // audioCmds est vide et garde sa capacité : aucune allocation
    }
    for (auto& c : audioCmds)
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
        retire (v.s);
        v.s = c.sample;
        v.main = {}; v.sub = {}; v.shim = {};
        if (c.fromFull && c.loop && v.s->loopEnd > v.s->loopStart)
            v.main.pos = v.sub.pos = v.shim.pos = (double) v.s->loopStart;   // le pad part déjà « plein »
        v.loop = c.loop;
        v.rate = (v.s->sampleRate / hostRate) * std::pow (2.0, c.semis / 12.0);
        // monte sur le fondu entre tonalités si un son jouait déjà, sinon sur l'attaque
        const float inSec = juce::jmax (0.005f, anyAudible ? juce::jmax (c.xfade, 0.01f) : c.fadeIn);
        v.gain = 0; v.target = 1; v.step = 1.0f / (float) (inSec * hostRate);
        v.soft = ! anyAudible;
        v.active = true;
        std::shared_ptr<const SampleData> old;
        {
            const juce::SpinLock::ScopedLockType sl (latestLock);
            old = std::move (latest);
            latest = v.s;
        }
        retire (old);
    }
    for (auto& c : audioCmds) retire (c.sample);
    audioCmds.clear();   // garde sa capacité
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
            // entrée depuis le silence : part tout doucement puis monte (courbe en S) ;
            // changement de tonalité : à puissance constante (pas de creux) ; descente : courbe douce (fin bien propre)
            const float x = v.gain;
            const float g = (v.target > 0 && ! v.soft) ? std::sin (x * juce::MathConstants<float>::halfPi) : x * x * (3.0f - 2.0f * x);
            o0[i] += g * v.main.read (s, 0, v.loop);
            o1[i] += g * v.main.read (s, 1, v.loop);
            v.main.advance (s, v.rate, v.loop);
            if (s0 != nullptr)
            {
                s0[i] += g * v.sub.read (s, 0, true);
                s1[i] += g * v.sub.read (s, 1, true);
                v.sub.advance (s, v.rate * 0.5, true);
            }
            if (h0 != nullptr)
            {
                h0[i] += g * v.shim.read (s, 0, true);
                h1[i] += g * v.shim.read (s, 1, true);
                v.shim.advance (s, v.rate * 2.0, true);
            }
            if (v.main.finished) { v.active = false; break; }
        }
        if (! v.active) { retire (v.s); continue; }   // libéré plus tard, hors du fil audio
        any = true;
        if (v.target > 0) newest = &v;
    }
    flushRetired();
    playing = any;
    if (newest != nullptr)
        pos01 = (float) (newest->main.pos / juce::jmax (1, newest->s->buffer.getNumSamples()));
}
}
