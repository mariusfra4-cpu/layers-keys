#include "SeparationJob.h"
#include "../Engine/Dsp.h"
#include "Text.h"

namespace lsplit
{

SeparationJob::SeparationJob() : juce::Thread ("Layers Split - separation") {}

SeparationJob::~SeparationJob()
{
    alive->alive = false;
    signalThreadShouldExit();
    // la séparation vérifie l'annulation à chaque morceau : l'arrêt prend au plus quelques secondes
    stopThread (60000);
}

bool SeparationJob::start (const Request& r, Callback onDone, Callback onPreview)
{
    if (isThreadRunning())
        return false;
    req = r;
    callback = std::move (onDone);
    preview = std::move (onPreview);
    prog = 0.0;
    startMs = juce::Time::getMillisecondCounterHiRes();
    setStep (lsplit::U ("Préparation"), 0.0);
    return startThread (juce::Thread::Priority::low);
}

void SeparationJob::cancel()
{
    signalThreadShouldExit();
}

juce::String SeparationJob::stepText() const
{
    const juce::ScopedLock sl (stepLock);
    return step;
}

double SeparationJob::elapsedSeconds() const
{
    return (juce::Time::getMillisecondCounterHiRes() - startMs.load()) / 1000.0;
}

void SeparationJob::setStep (const juce::String& s, double p)
{
    {
        const juce::ScopedLock sl (stepLock);
        step = s;
    }
    prog = juce::jlimit (0.0, 1.0, p);
}

void SeparationJob::postPreview (PlayDataPtr d, const juce::String& info)
{
    if (! preview) return;
    Result r;
    r.ok = true;
    r.data = std::move (d);
    r.info = info;
    auto cb = preview;
    auto flag = alive;
    juce::MessageManager::callAsync ([cb, flag, r] { if (flag->alive && cb) cb (r); });
}

void SeparationJob::run()
{
    Result res;
    try
    {
        res = doWork();
    }
    catch (const std::bad_alloc&)
    {
        res = {};
        res.error = lsplit::U ("Mémoire insuffisante. Fermez d'autres logiciels ou choisissez un morceau plus court.");
    }
    catch (const std::exception& e)
    {
        res = {};
        res.error = juce::String::fromUTF8 (e.what());
    }
    catch (...)
    {
        res = {};
        res.error = lsplit::U ("Erreur inattendue pendant la séparation.");
    }

    if (threadShouldExit() && ! res.ok)
        res.cancelled = res.error.isEmpty();

    auto cb = callback;
    auto flag = alive;
    juce::MessageManager::callAsync ([cb, flag, res]
    {
        if (flag->alive && cb)
            cb (res);
    });
}

SeparationJob::Result SeparationJob::doWork()
{
    Result res;
    auto data = std::make_shared<PlayData>();

    // 1. le mix
    if (req.existing != nullptr && req.existing->sourceFile == req.source)
    {
        data->sourceFile = req.existing->sourceFile;
        data->sampleRate = req.existing->sampleRate;
        data->mix = req.existing->mix;
        data->mixPeaks = req.existing->mixPeaks;
    }
    else
    {
        setStep ("Lecture du fichier", 0.0);
        juce::String err;
        if (! loadAudioFile (req.source, data->mix, data->sampleRate, err))
        {
            res.error = err;
            return res;
        }
        data->sourceFile = req.source;
        data->mixPeaks = computePeaks (data->mix);
    }
    if (threadShouldExit()) return res;

    // 2. stems déjà calculés ?
    const auto key = cacheKeyFor (req.source, req.settings);
    const auto dir = cacheDirFor (key);
    if (req.useCache && cacheIsComplete (dir))
    {
        setStep (lsplit::U ("Chargement des stems déjà calculés"), 0.5);
        juce::String err;
        if (loadFromCache (dir, *data, err))
        {
            for (int k = 0; k < numStems; ++k)
                if (data->stems.present[k])
                    data->stemPeaks[k] = computePeaks (data->stems.stems[k]);
            res.ok = true;
            res.info = lsplit::U ("Stems repris du cache (déjà séparés).");
            res.data = data;
            setStep (lsplit::U ("Terminé"), 1.0);
            return res;
        }
        dir.deleteRecursively(); // cache abîmé : on refait
        data->stems = StemSet();
        data->hasStems = false;
    }

    if (req.kind == Kind::open)
    {
        // analyse rapide sur le mix seul (loudness, crête)
        data->analysis.lufs = integratedLoudness (data->mix, data->sampleRate);
        data->analysis.peakDb = samplePeakDb (data->mix);
        data->analysis.durationSec = data->durationSec();
        res.ok = true;
        res.data = data;
        setStep (lsplit::U ("Prêt"), 1.0);
        return res;
    }

    // 3. moteur
    setStep (lsplit::U ("Démarrage du moteur IA"), 0.0);
    const auto runtime = findRuntimeDir();
    if (runtime == juce::File())
    {
        res.error = lsplit::U ("Moteur IA introuvable. Réinstallez Layers Split (dossier « models » et onnxruntime manquants).");
        return res;
    }
    std::string err;
    if (! OrtRuntime::load (runtime.getFullPathName().toStdString(), err))
    {
        res.error = juce::String::fromUTF8 (err.c_str());
        return res;
    }

    Separator sep (modelsDirFor (runtime).getFullPathName().toStdString());
    std::string missing;
    if (! sep.checkModels (missing, req.settings))
    {
        res.error = lsplit::U ("Modèles IA manquants : ") + juce::String::fromUTF8 (missing.c_str()) + lsplit::U (". Réinstallez Layers Split.");
        return res;
    }

    const double t0 = juce::Time::getMillisecondCounterHiRes();
    auto settings = req.settings;
    settings.lowMemory = juce::SystemStats::getMemorySizeInMegabytes() < 12 * 1024;
    const bool done = sep.run (data->mix, data->sampleRate, settings, data->stems,
                               [this] (const std::string& s, double p)
                               {
                                   setStep (juce::String::fromUTF8 (s.c_str()), p * 0.95);
                                   return ! threadShouldExit();
                               },
                               [this, &data] (const StemSet& part)
                               {
                                   // instruments prêts : on les montre tout de suite (voix provisoire)
                                   auto p = std::make_shared<PlayData>();
                                   p->sourceFile = data->sourceFile;
                                   p->sampleRate = part.sampleRate;
                                   if (std::abs (part.sampleRate - data->sampleRate) < 0.5)
                                       p->mix = data->mix;
                                   else
                                   {
                                       p->mix.l = resample (data->mix.l.data(), data->mix.size(), data->sampleRate, part.sampleRate);
                                       p->mix.r = resample (data->mix.r.data(), data->mix.size(), data->sampleRate, part.sampleRate);
                                   }
                                   p->mixPeaks = data->mixPeaks;
                                   p->stems = part;
                                   p->hasStems = true;
                                   p->isPreview = true;
                                   p->analysis = data->analysis;
                                   p->hasAnalysis = false;
                                   for (int k = 0; k < numStems; ++k)
                                       if (part.present[k]) p->stemPeaks[k] = computePeaks (part.stems[k]);
                                   postPreview (p, U ("Instruments prêts. Les voix sont en cours d'affinage…"));
                               });
    if (! done || threadShouldExit())
        return res; // annulé

    data->hasStems = true;
    data->deviceUsed = juce::String::fromUTF8 (data->stems.deviceUsed.c_str());
    data->separationSeconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

    // 4. analyse
    setStep (lsplit::U ("Analyse (tempo, tonalité, loudness)"), 0.95);
    auto& a = data->analysis;
    a.durationSec = data->durationSec();
    a.lufs = integratedLoudness (data->mix, data->sampleRate);
    a.peakDb = samplePeakDb (data->mix);
    const auto& st = data->stems;
    a.bpm = estimateTempo (st.present[stemDrums] ? st.stems[stemDrums] : data->mix, data->sampleRate, a.bpmConfidence);
    {
        StereoBuffer harm = data->mix;
        if (st.present[stemDrums])
            for (size_t i = 0; i < harm.size(); ++i)
            {
                harm.l[i] -= st.stems[stemDrums].l[i];
                harm.r[i] -= st.stems[stemDrums].r[i];
            }
        estimateKey (harm, data->sampleRate, a.keyRoot, a.keyMinor, a.keyConfidence);
    }
    data->hasAnalysis = true;
    if (threadShouldExit()) return res;

    for (int k = 0; k < numStems; ++k)
        if (st.present[k])
            data->stemPeaks[k] = computePeaks (st.stems[k]);

    // 5. résultat affiché tout de suite ; puis cache (réouverture instantanée, glisser vers le DAW)
    data->cacheDir = dir;
    postPreview (data, {});
    setStep ("Enregistrement des stems", 0.98);
    juce::String cerr;
    if (saveToCache (dir, *data, cerr))
    {
        data->cacheDir = dir;
        pruneCache();
    }
    else
    {
        dir.deleteRecursively();
        data->cacheDir = juce::File();
        res.info = "Attention : " + cerr;
    }

    res.ok = true;
    res.data = data;
    setStep (lsplit::U ("Terminé"), 1.0);
    return res;
}

} // namespace lsplit
