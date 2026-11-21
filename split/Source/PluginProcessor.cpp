#include "PluginProcessor.h"
#include "Core/Text.h"
#include "PluginEditor.h"

using namespace lsplit;

juce::AudioProcessor::BusesProperties LayersSplitProcessor::makeBuses()
{
    auto b = BusesProperties()
                 .withOutput ("Mix", juce::AudioChannelSet::stereo(), true);
    for (int k = 0; k < numStems; ++k)
        b = b.withOutput (lsplit::U (stemName (k)), juce::AudioChannelSet::stereo(), false);
    return b;
}

LayersSplitProcessor::LayersSplitProcessor() : AudioProcessor (makeBuses())
{
    for (auto& l : stemLevel) l = 0.0f;
    startTimer (250);

    if (isStandalone())
    {
        // Layers Split.exe "morceau.mp3" [--separer]
        const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
        juce::MessageManager::callAsync ([this, safe = juce::WeakReference<LayersSplitProcessor> (this), args]
        {
            if (safe == nullptr) return;
            for (const auto& a : args)
            {
                if (a == "--separer" || a == "--separate")
                    autoSeparatePending = true;
                else if (a == "--rapide") settings.quality = Quality::fast;
                else if (a == "--ultra") settings.quality = Quality::ultra;
                else if (a == "--cpu") settings.useGpu = false;
                else if (a == "--solo") settings.splitGuitarSolo = true;
                else if (a == "--simple") settings.detailInstruments = false;
            }
            for (const auto& a : args)
            {
                const juce::File f (a.unquoted());
                if (! a.startsWith ("--") && juce::File::isAbsolutePath (a.unquoted()) && f.existsAsFile())
                {
                    openFile (f);
                    break;
                }
            }
        });
    }
}

LayersSplitProcessor::~LayersSplitProcessor()
{
    stopTimer();
    job.cancel();
}

juce::String LayersSplitProcessor::engineDescription() const
{
    if (! OrtRuntime::isLoaded())
        return lsplit::U ("Moteur IA : prêt au premier usage");
    juce::String s = "ONNX Runtime " + juce::String (OrtRuntime::version());
    s << (OrtRuntime::directMLAvailable() ? lsplit::U (" · GPU DirectML disponible") : lsplit::U (" · processeur"));
    return s;
}

//==============================================================================
void LayersSplitProcessor::setStatus (const juce::String& s, bool error)
{
    status = s;
    statusError = error;
    sendChangeMessage();
}

void LayersSplitProcessor::publish (PlayDataPtr d)
{
    // l'ancienne version reste vivante quelques secondes : le fil audio ne libère jamais de mémoire
    retired = current;
    retiredAt = juce::Time::getMillisecondCounter();
    current = std::move (d);
    {
        const juce::SpinLock::ScopedLockType sl (playLock);
        playData = current.get();
        playPos = 0.0;
        seekRequested = true;
    }
    playing = false;
    sendChangeMessage();
}

void LayersSplitProcessor::openFile (const juce::File& f)
{
    if (job.isRunning())
    {
        setStatus ("Patientez : un traitement est en cours (ou cliquez sur Annuler).", true);
        return;
    }
    if (! f.existsAsFile())
    {
        setStatus ("Fichier introuvable : " + f.getFullPathName(), true);
        return;
    }
    if (! isSupportedAudioFile (f))
    {
        setStatus ("Format non pris en charge. Utilisez WAV, AIFF, FLAC, MP3 ou OGG.", true);
        return;
    }

    SeparationJob::Request r;
    r.kind = SeparationJob::Kind::open;
    r.source = f;
    r.settings = settings;
    setStatus ("Ouverture de " + f.getFileName() + lsplit::U ("…"));
    job.start (r, [this] (const SeparationJob::Result& res) { onJobDone (res); });
    sendChangeMessage();
}

void LayersSplitProcessor::startSeparation()
{
    if (job.isRunning() || current == nullptr)
        return;

    SeparationJob::Request r;
    r.kind = SeparationJob::Kind::separate;
    r.source = current->sourceFile;
    r.settings = settings;
    r.existing = current;
    setStatus (lsplit::U ("Séparation en cours…"));
    job.start (r, [this] (const SeparationJob::Result& res) { onJobDone (res); });
    sendChangeMessage();
}

void LayersSplitProcessor::cancelJob()
{
    job.cancel();
}

void LayersSplitProcessor::onJobDone (const SeparationJob::Result& r)
{
    if (r.cancelled)
    {
        setStatus (lsplit::U ("Traitement annulé."));
        return;
    }
    if (! r.ok)
    {
        setStatus (r.error.isNotEmpty() ? r.error : lsplit::U ("Échec du traitement."), true);
        return;
    }

    publish (r.data);

    if (r.data->hasStems)
    {
        juce::String s = r.info.isNotEmpty() ? r.info
                                             : lsplit::U ("Séparation terminée en ") + formatTime (r.data->separationSeconds)
                                                 + " (" + r.data->deviceUsed + ").";
        setStatus (s, r.info.startsWith ("Attention"));
    }
    else
    {
        setStatus (r.data->sourceFile.getFileName() + lsplit::U (" ouvert. Cliquez sur SÉPARER."));
        if (autoSeparatePending)
        {
            autoSeparatePending = false;
            startSeparation();
        }
    }
}

void LayersSplitProcessor::timerCallback()
{
    if (retired != nullptr && juce::Time::getMillisecondCounter() - retiredAt > 3000)
        retired.reset();
}

//==============================================================================
void LayersSplitProcessor::play()
{
    if (current == nullptr) return;
    if (playPos.load() >= (double) current->length() - 1)
        seekSeconds (0.0);
    playing = true;
}

void LayersSplitProcessor::pause() { playing = false; }

void LayersSplitProcessor::stop()
{
    playing = false;
    seekSeconds (0.0);
}

void LayersSplitProcessor::seekSeconds (double s)
{
    if (current == nullptr) return;
    const double pos = juce::jlimit (0.0, (double) current->length(), s * current->sampleRate);
    playPos = std::floor (pos);
    seekRequested = true;
}

double LayersSplitProcessor::positionSeconds() const
{
    if (current == nullptr || current->sampleRate <= 0) return 0.0;
    return playPos.load() / current->sampleRate;
}

//==============================================================================
void LayersSplitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    deviceRate = sampleRate > 0 ? sampleRate : 44100.0;
    const int maxBlock = juce::jmax (samplesPerBlock, 512);
    // assez pour un fichier jusqu'à 8 fois la fréquence de la carte son
    scratchIn.setSize (2 * (numStems + 1), maxBlock * 8 + 64, false, true, false);
    scratchOut.setSize (2 * (numStems + 1), maxBlock, false, true, false);
    for (auto& i : interp) i.reset();
    seekRequested = true;
}

void LayersSplitProcessor::releaseResources() {}

bool LayersSplitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return layouts.inputBuses.isEmpty();
}

float LayersSplitProcessor::stemGainLinear (int k, bool anySolo) const
{
    const auto& c = controls[(size_t) k];
    if (c.mute.load()) return 0.0f;
    if (anySolo && ! c.solo.load()) return 0.0f;
    return juce::Decibels::decibelsToGain (c.gainDb.load(), -60.0f);
}

void LayersSplitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    buffer.clear();

    const juce::SpinLock::ScopedTryLockType sl (playLock);
    if (! sl.isLocked() || playData == nullptr)
        return;

    const PlayData& d = *playData;
    const double len = (double) d.length();
    if (len <= 0) return;

    // suivi du DAW (plugin)
    if (! isStandalone() && followHost.load())
    {
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                const bool hostPlaying = pos->getIsPlaying();
                if (hostPlaying)
                {
                    if (auto t = pos->getTimeInSeconds())
                    {
                        const double target = *t * d.sampleRate;
                        const double ratio = d.sampleRate / deviceRate;
                        if (! wasHostPlaying || std::abs (target - playPos.load()) > ratio * buffer.getNumSamples() * 2 + 64)
                        {
                            playPos = juce::jlimit (0.0, len, std::floor (target));
                            seekRequested = true;
                        }
                        playing = target >= 0.0 && target < len;
                    }
                }
                else if (wasHostPlaying)
                {
                    playing = false;
                }
                wasHostPlaying = hostPlaying;
            }
        }
    }

    // vu-mètres : retombée progressive
    const float decay = 0.80f;
    for (auto& l : stemLevel) l = l.load() * decay;
    masterLevel = masterLevel.load() * decay;

    if (! playing.load())
        return;

    if (seekRequested.exchange (false))
        for (auto& i : interp) i.reset();

    bool anySolo = false;
    for (int k = 0; k < numStems; ++k) anySolo = anySolo || controls[(size_t) k].solo.load();

    const bool original = listenOriginal.load() || ! d.hasStems;
    const float master = juce::Decibels::decibelsToGain (masterDb.load(), -60.0f);
    float gains[numStems];
    for (int k = 0; k < numStems; ++k)
        gains[k] = d.hasStems && d.stems.present[k] ? stemGainLinear (k, anySolo) : 0.0f;

    // bus de sortie actifs
    const int numBuses = juce::jmin (getBusCount (false), numStems + 1);
    const double ratio = d.sampleRate / deviceRate;
    const int capacityIn = scratchIn.getNumSamples() - 8;
    const int capacityOut = juce::jmax (1, juce::jmin (scratchOut.getNumSamples(), (int) std::floor (capacityIn / ratio) - 4));

    int done = 0;
    const int total = buffer.getNumSamples();
    while (done < total && playing.load())
    {
        const int nOut = juce::jmin (capacityOut, total - done);
        const int needIn = juce::jmin (capacityIn, (int) std::ceil (nOut * ratio) + 4);
        const size_t start = (size_t) playPos.load();

        // 1. préparation des signaux à la fréquence du fichier
        for (int bus = 0; bus < numBuses; ++bus)
        {
            if (bus > 0 && getBus (false, bus) != nullptr && ! getBus (false, bus)->isEnabled())
                continue;
            float* L = scratchIn.getWritePointer (2 * bus);
            float* R = scratchIn.getWritePointer (2 * bus + 1);
            for (int i = 0; i < needIn; ++i)
            {
                const size_t idx = start + (size_t) i;
                float l = 0.0f, r = 0.0f;
                if (idx < d.length())
                {
                    if (bus == 0)
                    {
                        if (original)
                        {
                            l = d.mix.l[idx];
                            r = d.mix.r[idx];
                        }
                        else
                        {
                            for (int k = 0; k < numStems; ++k)
                                if (gains[k] > 0.0f)
                                {
                                    l += d.stems.stems[k].l[idx] * gains[k];
                                    r += d.stems.stems[k].r[idx] * gains[k];
                                }
                        }
                        l *= master;
                        r *= master;
                    }
                    else if (d.hasStems && d.stems.present[bus - 1])
                    {
                        const auto& c = controls[(size_t) (bus - 1)];
                        const float g = c.mute.load() ? 0.0f : juce::Decibels::decibelsToGain (c.gainDb.load(), -60.0f);
                        l = d.stems.stems[bus - 1].l[idx] * g;
                        r = d.stems.stems[bus - 1].r[idx] * g;
                    }
                }
                L[i] = l;
                R[i] = r;
            }
        }

        // niveaux par stem (avant rééchantillonnage, suffisant pour l'affichage)
        if (d.hasStems)
        {
            for (int k = 0; k < numStems; ++k)
            {
                if (! d.stems.present[k]) continue;
                float pk = 0.0f;
                const float g = original ? 0.0f : gains[k];
                for (int i = 0; i < nOut; i += 4)
                {
                    const size_t idx = start + (size_t) i;
                    if (idx >= d.length()) break;
                    pk = juce::jmax (pk, std::abs (d.stems.stems[k].l[idx]), std::abs (d.stems.stems[k].r[idx]));
                }
                stemLevel[(size_t) k] = juce::jmax (stemLevel[(size_t) k].load(), pk * g);
            }
        }

        // 2. rééchantillonnage vers la carte son et écriture
        int consumed = (int) std::lround (nOut * ratio);
        for (int bus = 0; bus < numBuses; ++bus)
        {
            auto* b = getBus (false, bus);
            if (b == nullptr || ! b->isEnabled()) continue;
            const int ch0 = getChannelIndexInProcessBlockBuffer (false, bus, 0);
            if (ch0 + 1 >= buffer.getNumChannels()) continue;
            for (int c = 0; c < 2; ++c)
            {
                float* out = buffer.getWritePointer (ch0 + c, done);
                const float* in = scratchIn.getReadPointer (2 * bus + c);
                if (std::abs (ratio - 1.0) < 1.0e-9)
                {
                    std::copy (in, in + nOut, out);
                    consumed = nOut;
                }
                else
                {
                    consumed = interp[(size_t) (2 * bus + c)].process (ratio, in, out, nOut, needIn, 0);
                }
            }
        }

        float pkMaster = 0.0f;
        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
            pkMaster = juce::jmax (pkMaster, buffer.getMagnitude (c, done, nOut));
        masterLevel = juce::jmax (masterLevel.load(), pkMaster);

        double next = playPos.load() + consumed;
        if (next >= len)
        {
            if (loop.load())
            {
                next = 0.0;
                for (auto& i : interp) i.reset();
            }
            else
            {
                next = len;
                playing = false;
            }
        }
        playPos = next;
        done += nOut;
    }
}

//==============================================================================
juce::AudioProcessorEditor* LayersSplitProcessor::createEditor()
{
    return new LayersSplitEditor (*this);
}

void LayersSplitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement x ("LayersSplit");
    x.setAttribute ("version", 1);
    if (current != nullptr)
        x.setAttribute ("file", current->sourceFile.getFullPathName());
    else if (pendingStateFile.isNotEmpty())
        x.setAttribute ("file", pendingStateFile);
    x.setAttribute ("quality", (int) settings.quality);
    x.setAttribute ("gpu", settings.useGpu);
    x.setAttribute ("splitVocals", settings.splitVocals);
    x.setAttribute ("splitDrumsBass", settings.splitDrumsBass);
    x.setAttribute ("detailInstruments", settings.detailInstruments);
    x.setAttribute ("splitGuitarSolo", settings.splitGuitarSolo);
    x.setAttribute ("followHost", followHost.load());
    x.setAttribute ("loop", loop.load());
    x.setAttribute ("master", masterDb.load());
    for (int k = 0; k < numStems; ++k)
    {
        auto* c = x.createNewChildElement ("Stem");
        c->setAttribute ("id", k);
        c->setAttribute ("gain", controls[(size_t) k].gainDb.load());
        c->setAttribute ("mute", controls[(size_t) k].mute.load());
        c->setAttribute ("solo", controls[(size_t) k].solo.load());
    }
    copyXmlToBinary (x, destData);
}

void LayersSplitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto x = getXmlFromBinary (data, sizeInBytes);
    if (x == nullptr || ! x->hasTagName ("LayersSplit"))
        return;

    const auto file = x->getStringAttribute ("file");
    const int quality = juce::jlimit (0, 2, x->getIntAttribute ("quality", 1));
    const bool gpu = x->getBoolAttribute ("gpu", true);
    const bool sv = x->getBoolAttribute ("splitVocals", true);
    const bool sdb = x->getBoolAttribute ("splitDrumsBass", true);
    const bool di = x->getBoolAttribute ("detailInstruments", true);
    const bool gs = x->getBoolAttribute ("splitGuitarSolo", false);
    followHost = x->getBoolAttribute ("followHost", true);
    loop = x->getBoolAttribute ("loop", false);
    masterDb = (float) x->getDoubleAttribute ("master", 0.0);
    for (auto* c : x->getChildWithTagNameIterator ("Stem"))
    {
        const int k = c->getIntAttribute ("id", -1);
        if (k < 0 || k >= numStems) continue;
        controls[(size_t) k].gainDb = (float) c->getDoubleAttribute ("gain", 0.0);
        controls[(size_t) k].mute = c->getBoolAttribute ("mute", false);
        controls[(size_t) k].solo = c->getBoolAttribute ("solo", false);
    }

    pendingStateFile = file;
    juce::MessageManager::callAsync ([this, safe = juce::WeakReference<LayersSplitProcessor> (this), file, quality, gpu, sv, sdb, di, gs]
    {
        if (safe == nullptr) return;
        settings.quality = (Quality) quality;
        settings.useGpu = gpu;
        settings.splitVocals = sv;
        settings.splitDrumsBass = sdb;
        settings.detailInstruments = di;
        settings.splitGuitarSolo = gs;
        if (file.isNotEmpty())
        {
            const juce::File f (file);
            if (f.existsAsFile())
                openFile (f); // reprend les stems du cache s'ils existent : aucun recalcul
            else
                setStatus ("Le morceau de ce projet est introuvable : " + file, true);
        }
        sendChangeMessage();
    });
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LayersSplitProcessor();
}
