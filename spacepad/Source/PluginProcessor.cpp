#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace spacepad
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;

juce::AudioProcessorValueTreeState::ParameterLayout SpacepadProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
    auto secs = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " s"; });
    auto add = [&] (const char* id, const char* name, float def) { l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0, 1), def, pct)); };
    add ("shimmer", "Shimmer", 0.45f);
    add ("warm", "Warm", 0.30f);
    add ("air", "Air", 0.25f);
    add ("reverb", "Reverb", 0.55f);
    add ("motion", "Motion", 0.15f);
    add ("width", "Width", 0.60f);
    add ("sub", "Sub", 0.0f);
    add ("lowcut", "Low cut", 0.15f);
    add ("padvol", "Volume Pad", 0.80f);
    add ("crowdvol", "Volume Crowd", 0.80f);
    l.add (std::make_unique<APF> (juce::ParameterID { "attack", 1 }, "Attack", juce::NormalisableRange<float> (0.05f, 8.0f, 0.0f, 0.5f), 2.0f, secs));
    l.add (std::make_unique<APF> (juce::ParameterID { "xfade", 1 }, "Fondu", juce::NormalisableRange<float> (0.1f, 10.0f, 0.0f, 0.5f), 4.0f, secs));
    l.add (std::make_unique<APB> (juce::ParameterID { "loop", 1 }, "Loop", true));
    l.add (std::make_unique<APB> (juce::ParameterID { "freeze", 1 }, "Freeze", false));
    l.add (std::make_unique<APB> (juce::ParameterID { "swell", 1 }, "Swell", false));
    return l;
}

SpacepadProcessor::SpacepadProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Spacepad commandes"),
      params (*this, nullptr, "SPACEPAD", createLayout())
{
    // première bibliothèque de pads par défaut
    for (auto& l : store.snapshot())
        if (l.type == LibType::Pad) { padLib = l.id; break; }
    startThread (juce::Thread::Priority::normal);
}

SpacepadProcessor::~SpacepadProcessor()
{
    stopThread (3000);
}

bool SpacepadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet().isDisabled() || layouts.getMainInputChannelSet() == out;
}

void SpacepadProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (64, samplesPerBlock);
    for (auto* b : { &mainBuf, &subBuf, &shimBuf, &crowdBuf }) b->setSize (2, maxBlock);
    pad.prepare (sampleRate);
    crowd.prepare (sampleRate);
    fx.prepare (sampleRate, maxBlock);
}

FxParams SpacepadProcessor::readFx() const
{
    FxParams f;
    f.shimmer = p ("shimmer"); f.warm = p ("warm"); f.air = p ("air"); f.reverb = p ("reverb");
    f.motion = p ("motion"); f.width = p ("width"); f.sub = p ("sub"); f.lowCut = p ("lowcut");
    f.freeze = p ("freeze") > 0.5f; f.swell = p ("swell") > 0.5f;
    return f;
}

bool SpacepadProcessor::loopOn() const { return p ("loop") > 0.5f; }

void SpacepadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // MIDI : une touche choisit la tonalité (Do, Do#… quelle que soit l'octave) ; changement de programme = librairie suivante
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) { midiKey = msg.getNoteNumber() % 12; midiSeen = true; }
        else if (msg.isProgramChange()) { midiProgram = msg.getProgramChangeNumber(); midiSeen = true; }
    }
    midi.clear();

    const bool loop = loopOn();
    if (loop != lastLoop.load()) { pad.setLoop (loop); crowd.setLoop (loop); lastLoop = loop; }

    const int total = buffer.getNumSamples();
    const int outCh = buffer.getNumChannels();
    const auto fxp = readFx();
    const float padVol = p ("padvol"), crowdVol = p ("crowdvol");
    float peak = 0;
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        for (auto* b : { &mainBuf, &subBuf, &shimBuf, &crowdBuf }) b->clear (0, n);
        pad.render (mainBuf, &subBuf, &shimBuf, 0, n);
        fx.process (mainBuf, subBuf, shimBuf, n, fxp);
        crowd.render (crowdBuf, nullptr, nullptr, 0, n);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = mainBuf.getWritePointer (ch);
            const float* c = crowdBuf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float x = d[i] * padVol + c[i] * crowdVol;
                // limiteur doux : jamais de saturation dure en sortie
                const float a = std::abs (x);
                if (a > 0.8f) x = std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
                d[i] = x;
                peak = juce::jmax (peak, std::abs (x));
            }
        }
        if (outCh >= 2)
        {
            buffer.copyFrom (0, start, mainBuf, 0, 0, n);
            buffer.copyFrom (1, start, mainBuf, 1, 0, n);
            for (int ch = 2; ch < outCh; ++ch) buffer.clear (ch, start, n);
        }
        else if (outCh == 1)
        {
            buffer.copyFrom (0, start, mainBuf, 0, 0, n);
            buffer.addFrom (0, start, mainBuf, 1, 0, n);
            buffer.applyGain (0, start, n, 0.5f);
        }
    }
    level = juce::jmax (peak, level.load() * 0.85f);
}

// ---------------------------------------------------------------------------------------------
// Commandes
// ---------------------------------------------------------------------------------------------
juce::String SpacepadProcessor::padLibraryId() const { const juce::ScopedLock sl (stateLock); return padLib; }
juce::String SpacepadProcessor::crowdLibraryId() const { const juce::ScopedLock sl (stateLock); return crowdLib; }
juce::String SpacepadProcessor::padSoundName() const { const juce::ScopedLock sl (stateLock); return padSoundNow; }

void SpacepadProcessor::startPadKey (int key, int mode, bool fromSilence)
{
    auto lib = store.find (padLibraryId());
    if (lib == nullptr || key < 0) return;
    auto slot = lib->slotFor (key, mode);
    auto* s = lib->sound (slot.sound);
    if (s == nullptr) return;
    {
        const juce::ScopedLock sl (stateLock);
        padSoundNow = s->name;
    }
    const int gen = ++generation;
    const float attack = p ("attack"), xf = p ("xfade");
    const bool loop = loopOn();
    ++pendingLoads;
    sampleLoader.request (s->path, [this, gen, slot, attack, xf, loop, fromSilence] (std::shared_ptr<const SampleData> d)
    {
        if (d != nullptr && gen == generation.load())
            pad.start (d, slot.semitones, loop, attack, fromSilence ? 0.01f : xf);
        --pendingLoads;
    });
}

void SpacepadProcessor::selectPadLibrary (const juce::String& id)
{
    {
        const juce::ScopedLock sl (stateLock);
        if (padLib == id) return;
        padLib = id;
    }
    if (padOn) startPadKey (keyNow, modeNow, false); // la nouvelle librairie prend le relais en fondu
}

void SpacepadProcessor::pressKey (int key)
{
    key = ((key % 12) + 12) % 12;
    if (padOn && key == keyNow.load())
    {
        stopPad();
        return;
    }
    const bool fromSilence = ! padOn.load();
    keyNow = key;
    padOn = true;
    startPadKey (key, modeNow, fromSilence);
}

void SpacepadProcessor::setMode (int mode)
{
    mode = mode == 1 ? 1 : 0;
    if (modeNow.exchange (mode) == mode) return;
    if (padOn) startPadKey (keyNow, mode, false);
}

void SpacepadProcessor::playPad()
{
    if (padOn) return;
    padOn = true;
    startPadKey (keyNow, modeNow, true);
}

void SpacepadProcessor::stopPad()
{
    padOn = false;
    ++generation;
    pad.stop (juce::jmax (0.3f, p ("xfade") * 0.5f));
}

void SpacepadProcessor::playCrowd (const juce::String& libId, int index)
{
    if (crowd.isPlaying() && crowdIndex.load() == index && crowdLibraryId() == libId)
    {
        stopCrowd();
        return;
    }
    auto lib = store.find (libId);
    if (lib == nullptr) return;
    auto* s = lib->sound (index);
    if (s == nullptr) return;
    {
        const juce::ScopedLock sl (stateLock);
        crowdLib = libId;
    }
    crowdIndex = index;
    const bool loop = loopOn();
    ++pendingLoads;
    sampleLoader.request (s->path, [this, loop] (std::shared_ptr<const SampleData> d)
    {
        if (d != nullptr) crowd.start (d, 0, loop, 0.05f, 0.8f);
        --pendingLoads;
    });
}

void SpacepadProcessor::stopCrowd()
{
    crowdIndex = -1;
    crowd.stop (1.0f);
}

void SpacepadProcessor::stopAll()
{
    stopPad();
    stopCrowd();
}

void SpacepadProcessor::waitIdleForTests (int ms)
{
    auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (pendingLoads.load() > 0 && juce::Time::getMillisecondCounter() < end)
        juce::Thread::sleep (5);
}

// fil de commande : MIDI → tonalité, programme → librairie, et relecture des bibliothèques enregistrées ailleurs
void SpacepadProcessor::run()
{
    int tick = 0;
    while (! threadShouldExit())
    {
        wait (5);
        const int k = midiKey.exchange (-1);
        if (k >= 0 && ! (padOn && k == keyNow.load()))
        {
            const bool fromSilence = ! padOn.load();
            keyNow = k; padOn = true;
            startPadKey (k, modeNow, fromSilence);
        }
        const int prog = midiProgram.exchange (-1);
        if (prog >= 0)
        {
            std::vector<juce::String> pads;
            for (auto& l : store.snapshot()) if (l.type == LibType::Pad) pads.push_back (l.id);
            if (! pads.empty()) selectPadLibrary (pads[(size_t) prog % pads.size()]);
        }
        if (++tick % 200 == 0) // environ 1 fois par seconde
        {
            if (store.reloadIfChanged() && padLibraryId().isEmpty())
                for (auto& l : store.snapshot())
                    if (l.type == LibType::Pad) { const juce::ScopedLock sl (stateLock); padLib = l.id; break; }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Sauvegarde dans le projet (Logic, Ableton…)
// ---------------------------------------------------------------------------------------------
void SpacepadProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty ("padLibrary", padLibraryId(), nullptr);
    state.setProperty ("crowdLibrary", crowdLibraryId(), nullptr);
    state.setProperty ("key", keyNow.load(), nullptr);
    state.setProperty ("mode", modeNow.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SpacepadProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid() || ! state.hasType (params.state.getType())) return;
        params.replaceState (state);
        const juce::ScopedLock sl (stateLock);
        auto lib = state.getProperty ("padLibrary").toString();
        if (lib.isNotEmpty() && store.find (lib) != nullptr) padLib = lib;
        crowdLib = state.getProperty ("crowdLibrary").toString();
        keyNow = juce::jlimit (0, 11, (int) state.getProperty ("key", 4));
        modeNow = (int) state.getProperty ("mode", 1) == 1 ? 1 : 0;
    }
}

juce::AudioProcessorEditor* SpacepadProcessor::createEditor()
{
    return new SpacepadEditor (*this);
}
}

// point d'entrée des plugins
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new spacepad::SpacepadProcessor();
}
