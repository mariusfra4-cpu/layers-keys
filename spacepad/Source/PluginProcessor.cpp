#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace spacepad
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;

const juce::StringArray& SpacepadProcessor::crowdFxNames()
{
    static const juce::StringArray n { "chorus", "delay", "dtime", "dfb", "reverb", "width", "dist", "move", "lowcut", "attack", "fade" };
    return n;
}

// réglages enregistrés dans chaque chant de la setlist
static const juce::StringArray& songParamList()
{
    static const juce::StringArray l = []
    {
        juce::StringArray a { "shimmer", "warm", "air", "reverb", "motion", "width", "sub", "lowcut", "attack", "xfade", "chorus", "delay", "dtime", "tempo",
                              "padvol", "crowdvol", "crowdvol2", "blend", "mix", "loop", "freeze", "swell", "crowdlink", "crowdfollow" };
        for (int s = 0; s < 2; ++s) for (auto& n : SpacepadProcessor::crowdFxNames()) a.add (SpacepadProcessor::crowdParam (s, n.toRawUTF8()));
        return a;
    }();
    return l;
}

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
    add ("crowdvol", "Volume Crowd 1", 0.80f);
    add ("crowdvol2", "Volume Crowd 2", 0.80f);
    add ("blend", "Melange A/B", 0.5f);
    add ("mix", "Dosage Pad/Crowd", 0.5f);
    l.add (std::make_unique<APF> (juce::ParameterID { "attack", 1 }, "Attack", juce::NormalisableRange<float> (0.05f, 8.0f, 0.0f, 0.5f), 2.0f, secs));
    l.add (std::make_unique<APF> (juce::ParameterID { "xfade", 1 }, "Fondu", juce::NormalisableRange<float> (0.1f, 10.0f, 0.0f, 0.5f), 4.0f, secs));
    l.add (std::make_unique<APB> (juce::ParameterID { "loop", 1 }, "Loop", true));
    l.add (std::make_unique<APB> (juce::ParameterID { "freeze", 1 }, "Freeze", false));
    l.add (std::make_unique<APB> (juce::ParameterID { "swell", 1 }, "Swell", false));
    l.add (std::make_unique<APB> (juce::ParameterID { "crowdlink", 1 }, "Crowd lie au pad", false));
    l.add (std::make_unique<APB> (juce::ParameterID { "crowdfollow", 1 }, "Crowd suit la tonalite", true));
    // chorus et delay du pad, tempo du delay
    add ("chorus", "Chorus", 0.0f);
    add ("delay", "Delay", 0.0f);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "dtime", 1 }, "Temps du delay", delayDivisionNames(), 1));
    l.add (std::make_unique<APF> (juce::ParameterID { "tempo", 1 }, "Tempo", juce::NormalisableRange<float> (40.0f, 200.0f, 1.0f), 72.0f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " bpm"; })));
    // effets de chaque crowd
    for (int s = 0; s < 2; ++s)
    {
        const auto pre = "Crowd " + juce::String (s + 1) + " ";
        auto addC = [&] (const char* id, const char* name, float def)
        { l.add (std::make_unique<APF> (juce::ParameterID { crowdParam (s, id), 1 }, pre + name, juce::NormalisableRange<float> (0, 1), def, pct)); };
        addC ("chorus", "Chorus", 0.0f);
        addC ("delay", "Delay", 0.0f);
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { crowdParam (s, "dtime"), 1 }, pre + "Temps du delay", delayDivisionNames(), 1));
        addC ("dfb", "Repetitions", 0.35f);
        addC ("reverb", "Reverb", 0.3f);
        addC ("width", "Width", 0.5f);
        addC ("dist", "Distance", 0.0f);
        addC ("move", "Mouvement", 0.0f);
        addC ("lowcut", "Low cut", 0.1f);
        l.add (std::make_unique<APF> (juce::ParameterID { crowdParam (s, "attack"), 1 }, pre + "Attack", juce::NormalisableRange<float> (0.02f, 8.0f, 0.0f, 0.5f), 0.3f, secs));
        l.add (std::make_unique<APF> (juce::ParameterID { crowdParam (s, "fade"), 1 }, pre + "Fondu", juce::NormalisableRange<float> (0.1f, 10.0f, 0.0f, 0.5f), 1.0f, secs));
    }
    return l;
}

SpacepadProcessor::SpacepadProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Crowds", juce::AudioChannelSet::stereo(), false)),
      juce::Thread ("Spacepad commandes"),
      params (*this, nullptr, "SPACEPAD", createLayout())
{
    for (auto& m : midiMap) m = -1;
    prefs.load();
    ccLast.fill (0);
    autoSelectLibraries();
    refreshMidiSettings();
    startThread (juce::Thread::Priority::normal);
}

SpacepadProcessor::~SpacepadProcessor()
{
    stopThread (3000);
    prefs.saveIfDirty();
}

bool SpacepadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    // sortie séparée « Crowds » (facultative) : désactivée ou stéréo
    if (layouts.outputBuses.size() > 1)
    {
        const auto& aux = layouts.outputBuses[1];
        if (! aux.isDisabled() && aux != juce::AudioChannelSet::stereo()) return false;
    }
    return layouts.getMainInputChannelSet().isDisabled() || layouts.getMainInputChannelSet() == out;
}

void SpacepadProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (64, samplesPerBlock);
    for (auto* b : { &mainBuf, &subBuf, &shimBuf, &bBuf, &bSub, &bShim, &crowdBuf, &crowdBuf2 }) b->setSize (2, maxBlock);
    pad.prepare (sampleRate);
    padB.prepare (sampleRate);
    crowd[0].prepare (sampleRate);
    crowd[1].prepare (sampleRate);
    fx.prepare (sampleRate, maxBlock);
    crowdFx[0].prepare (sampleRate, maxBlock);
    crowdFx[1].prepare (sampleRate, maxBlock);
}

// première librairie de pads majeurs et de pads mineurs, si rien n'est encore choisi
void SpacepadProcessor::autoSelectLibraries()
{
    const juce::ScopedLock sl (stateLock);
    for (int mode = 0; mode < 2; ++mode)
    {
        if (padLibsB[mode].isNotEmpty() && store.find (padLibsB[mode]) == nullptr) padLibsB[mode] = {};
        if (padLibs[mode].isNotEmpty() && store.find (padLibs[mode]) != nullptr) continue;
        padLibs[mode] = {};
        for (auto& l : store.snapshot())
            if (l.type == LibType::Pad && l.countMode (mode) > 0) { padLibs[mode] = l.id; break; }
    }
}

void SpacepadProcessor::refreshMidiSettings()
{
    const auto u = uiSettings();
    keyMode = u.midiKeyMode;
    zoneLow = u.zoneLow;
    for (int i = 0; i < actCount; ++i) midiMap[(size_t) i] = u.midiMap[(size_t) i];
    lastSettingsVersion = settingsVersion();
}

void SpacepadProcessor::copyScope (float* dest, int n) const
{
    n = juce::jmin (n, scopeSize);
    const int end = scopePos.load();
    for (int i = 0; i < n; ++i) dest[i] = scope[(size_t) ((end - n + i + scopeSize * 2) % scopeSize)];
}

FxParams SpacepadProcessor::readFx() const
{
    FxParams f;
    f.shimmer = p ("shimmer"); f.warm = p ("warm"); f.air = p ("air"); f.reverb = p ("reverb");
    f.motion = p ("motion"); f.width = p ("width"); f.sub = p ("sub"); f.lowCut = p ("lowcut");
    f.freeze = p ("freeze") > 0.5f; f.swell = p ("swell") > 0.5f;
    f.chorus = p ("chorus"); f.delay = p ("delay"); f.delaySeconds = delaySeconds ((int) p ("dtime"));
    return f;
}

double SpacepadProcessor::tempoBpm() const
{
    const double h = hostBpm.load();
    return h > 0 ? juce::jlimit (30.0, 300.0, h) : (double) p ("tempo");
}

float SpacepadProcessor::delaySeconds (int division) const { return (float) (60.0 / tempoBpm()) * delayDivision (division); }

CrowdFxParams SpacepadProcessor::readCrowdFx (int s) const
{
    CrowdFxParams c;
    c.chorus = p (crowdParam (s, "chorus")); c.delay = p (crowdParam (s, "delay"));
    c.delaySeconds = delaySeconds ((int) p (crowdParam (s, "dtime"))); c.feedback = p (crowdParam (s, "dfb")) * 0.85f;
    c.reverb = p (crowdParam (s, "reverb")); c.width = p (crowdParam (s, "width")); c.distance = p (crowdParam (s, "dist"));
    c.movement = p (crowdParam (s, "move")); c.lowCut = p (crowdParam (s, "lowcut"));
    return c;
}

void SpacepadProcessor::tapTempoAt (double ms)
{
    if (taps > 0 && ms - tapTimes[(taps - 1) % 8] > 2000.0) taps = 0;      // plus de 2 s : on recommence
    tapTimes[taps % 8] = ms;
    ++taps;
    const int n = juce::jmin (taps, 5);
    if (n < 2) return;
    const double span = tapTimes[(taps - 1) % 8] - tapTimes[(taps - n) % 8];
    const double bpm = 60000.0 / (span / (n - 1));
    setParam ("tempo", (float) juce::jlimit (40.0, 200.0, std::round (bpm)));
}

juce::String SpacepadProcessor::crowdSoundKey (int slot) const
{
    slot &= 1;
    const auto lib = store.find (crowdLibraryId (slot));
    if (lib == nullptr) return {};
    auto* snd = lib->sound (crowdIdx[slot].load());
    return snd != nullptr ? CrowdPrefs::keyFor (lib->id, snd->path) : juce::String();
}

CrowdPrefs::Fx SpacepadProcessor::crowdFxSnapshot (int s) const
{
    CrowdPrefs::Fx m;
    for (auto& n : crowdFxNames())
        if (auto* prm = params.getParameter (crowdParam (s, n.toRawUTF8()))) m[n] = prm->convertFrom0to1 (prm->getValue());
    return m;
}

void SpacepadProcessor::applyCrowdFx (int s, const CrowdPrefs::Fx& m)
{
    for (auto& kv : m) if (crowdFxNames().contains (kv.first)) setParam (crowdParam (s, kv.first.toRawUTF8()), kv.second);
}

// un nouveau son arrive dans ce crowd : il reprend les effets réglés pour lui la dernière fois
void SpacepadProcessor::newCrowdSound (int s)
{
    const auto key = crowdSoundKey (s);
    const juce::ScopedLock sl (fxLock);
    if (key.isNotEmpty())
    {
        prefs.markPlayed (key);
        if (prefs.hasFx (key)) applyCrowdFx (s, prefs.fx (key));
    }
    lastFx[s] = crowdFxSnapshot (s);
}

void SpacepadProcessor::clearCrowdSoundFx (int s)
{
    const auto key = crowdSoundKey (s);
    if (key.isNotEmpty()) prefs.clearFx (key);
}

// un bouton d'effet crowd a été tourné : on le mémorise pour le son qui joue dans ce crowd
void SpacepadProcessor::trackCrowdFx()
{
    const juce::ScopedLock sl (fxLock);
    for (int s = 0; s < 2; ++s)
    {
        if (crowdIdx[s].load() < 0) continue;
        const auto now = crowdFxSnapshot (s);
        bool same = now.size() == lastFx[s].size();
        if (same) for (auto& kv : now) { auto it = lastFx[s].find (kv.first); if (it == lastFx[s].end() || std::abs (it->second - kv.second) > 1e-4f) { same = false; break; } }
        if (same) continue;
        lastFx[s] = now;
        const auto key = crowdSoundKey (s);
        if (key.isNotEmpty()) prefs.setFx (key, now);
    }
}

bool SpacepadProcessor::loopOn() const { return p ("loop") > 0.5f; }

void SpacepadProcessor::setParam (const char* id, float v)
{
    if (auto* prm = params.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

// ---------------------------------------------------------------------------------------------
// Son
// ---------------------------------------------------------------------------------------------
void SpacepadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    {
        double bpm = 0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm()) bpm = *b;
        hostBpm = bpm;
    }
    // MIDI : pédales et boutons assignés, puis notes qui choisissent la tonalité (selon le réglage)
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        int code = -1;
        if (msg.isController()) code = 1000 + msg.getControllerNumber();
        else if (msg.isNoteOn()) code = 2000 + msg.getNoteNumber();
        else if (msg.isProgramChange()) { midiProgram = msg.getProgramChangeNumber(); midiSeen = true; continue; }
        else continue;
        midiSeen = true;
        if (msg.isNoteOn() && learnZoneFlag.load()) { learnedZone = msg.getNoteNumber(); learnZoneFlag = false; continue; }
        if (learnAction.load() >= 0) { learnedCode = code; continue; }
        int action = -1;
        for (int a = 0; a < actCount; ++a) if (midiMap[(size_t) a].load() == code) { action = a; break; }
        if (action >= 0)
        {
            if (action >= actPadVolume)
            {
                const float v = msg.isController() ? msg.getControllerValue() / 127.0f : 1.0f;
                contValue[(size_t) (action - actPadVolume)] = v;
                contFlags.fetch_or (1u << (action - actPadVolume));
            }
            else if (msg.isController())
            {
                const int cc = msg.getControllerNumber(), v = msg.getControllerValue();
                if (v >= 64 && ccLast[(size_t) cc] < 64) pendingActions.fetch_or (1u << action);   // pédale enfoncée
            }
            else
                pendingActions.fetch_or (1u << action);
        }
        if (msg.isController()) { ccLast[(size_t) msg.getControllerNumber()] = msg.getControllerValue(); continue; }
        if (action >= 0) continue;                       // une note assignée à une action ne change pas la tonalité
        const int note = msg.getNoteNumber(), km = keyMode.load(), z = zoneLow.load();
        if (km == 2 || (km == 0 && note >= z && note < z + 12)) midiKey = note % 12;
    }
    midi.clear();

    const bool loop = loopOn();
    if (loop != lastLoop.load()) { pad.setLoop (loop); padB.setLoop (loop); crowd[0].setLoop (loop); crowd[1].setLoop (loop); lastLoop = loop; }

    const int total = buffer.getNumSamples();
    auto mainOut = getBusBuffer (buffer, false, 0);
    const bool auxOn = getBusCount (false) > 1 && getBus (false, 1) != nullptr && getBus (false, 1)->isEnabled() && getBus (false, 1)->getNumberOfChannels() >= 2;
    juce::AudioBuffer<float> auxOut;
    if (auxOn) auxOut = getBusBuffer (buffer, false, 1);
    const auto fxp = readFx();
    const float padVol = p ("padvol"), cv1 = p ("crowdvol"), cv2 = p ("crowdvol2");
    const float mix = p ("mix");
    const float padMix = soloOn.load() ? 0.0f : juce::jmin (1.0f, 2.0f * (1.0f - mix)), crowdMix = juce::jmin (1.0f, 2.0f * mix);
    const CrowdFxParams cfx[2] = { readCrowdFx (0), readCrowdFx (1) };
    float cpk[2] = { 0, 0 };
    // mélange A/B à puissance constante (B absent : A seul)
    const bool hasB = padB.isPlaying();
    const float blend = p ("blend");
    const float gA = hasB ? std::cos (blend * juce::MathConstants<float>::halfPi) * 1.41421356f : 1.0f;
    const float gB = hasB ? std::sin (blend * juce::MathConstants<float>::halfPi) * 1.41421356f : 0.0f;
    float peak = 0;
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        for (auto* b : { &mainBuf, &subBuf, &shimBuf, &bBuf, &bSub, &bShim, &crowdBuf, &crowdBuf2 }) b->clear (0, n);
        pad.render (mainBuf, &subBuf, &shimBuf, 0, n);
        padB.render (bBuf, &bSub, &bShim, 0, n);
        // A et B passent ensemble dans les effets
        for (int ch = 0; ch < 2; ++ch)
        {
            mainBuf.applyGainRamp (ch, 0, n, lastGA, juce::jmin (gA, 1.0f));
            subBuf.applyGainRamp (ch, 0, n, lastGA, juce::jmin (gA, 1.0f));
            shimBuf.applyGainRamp (ch, 0, n, lastGA, juce::jmin (gA, 1.0f));
            mainBuf.addFromWithRamp (ch, 0, bBuf.getReadPointer (ch), n, lastGB, juce::jmin (gB, 1.0f));
            subBuf.addFromWithRamp (ch, 0, bSub.getReadPointer (ch), n, lastGB, juce::jmin (gB, 1.0f));
            shimBuf.addFromWithRamp (ch, 0, bShim.getReadPointer (ch), n, lastGB, juce::jmin (gB, 1.0f));
        }
        lastGA = juce::jmin (gA, 1.0f); lastGB = juce::jmin (gB, 1.0f);
        fx.process (mainBuf, subBuf, shimBuf, n, fxp);
        crowd[0].render (crowdBuf, nullptr, nullptr, 0, n);
        crowd[1].render (crowdBuf2, nullptr, nullptr, 0, n);
        crowdFx[0].process (crowdBuf, n, cfx[0]);
        crowdFx[1].process (crowdBuf2, n, cfx[1]);
        cpk[0] = juce::jmax (cpk[0], crowdBuf.getMagnitude (0, n) * p ("crowdvol") * crowdMix);
        cpk[1] = juce::jmax (cpk[1], crowdBuf2.getMagnitude (0, n) * p ("crowdvol2") * crowdMix);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = mainBuf.getWritePointer (ch);
            float* c = crowdBuf.getWritePointer (ch);
            const float* c2 = crowdBuf2.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float pv = d[i] * padVol * padMix;
                float cr = (c[i] * cv1 + c2[i] * cv2) * crowdMix;
                // limiteur doux : jamais de saturation dure en sortie
                auto limit = [] (float x) { const float a = std::abs (x); return a > 0.8f ? std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x) : x; };
                if (auxOn) { d[i] = limit (pv); c[i] = limit (cr); }
                else { d[i] = limit (pv + cr); c[i] = 0; }
                peak = juce::jmax (peak, std::abs (d[i]), std::abs (c[i]));
            }
        }
        // copie pour le visualiseur (tout ce qui sonne)
        {
            int w = scopePos.load();
            const float* l = mainBuf.getReadPointer (0); const float* r = mainBuf.getReadPointer (1);
            const float* cl = crowdBuf.getReadPointer (0); const float* cr = crowdBuf.getReadPointer (1);
            for (int i = 0; i < n; ++i) { scope[(size_t) w] = 0.5f * (l[i] + r[i] + cl[i] + cr[i]); w = (w + 1) % scopeSize; }
            scopePos = w;
        }
        const int outCh = mainOut.getNumChannels();
        if (outCh >= 2)
        {
            mainOut.copyFrom (0, start, mainBuf, 0, 0, n);
            mainOut.copyFrom (1, start, mainBuf, 1, 0, n);
        }
        else if (outCh == 1)
        {
            mainOut.copyFrom (0, start, mainBuf, 0, 0, n);
            mainOut.addFrom (0, start, mainBuf, 1, 0, n);
            mainOut.applyGain (0, start, n, 0.5f);
        }
        if (auxOn)
        {
            auxOut.copyFrom (0, start, crowdBuf, 0, 0, n);
            auxOut.copyFrom (1, start, crowdBuf, 1, 0, n);
        }
    }
    for (int ch = mainOut.getNumChannels() + (auxOn ? 2 : 0); ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, total);
    level = juce::jmax (peak, level.load() * 0.85f);
    for (int s = 0; s < 2; ++s) crowdLvl[s] = juce::jmax (cpk[s], crowdLvl[s].load() * 0.85f);
}

// ---------------------------------------------------------------------------------------------
// Pads
// ---------------------------------------------------------------------------------------------
juce::String SpacepadProcessor::padLibraryId() const { return padLibraryId (modeNow.load()); }
juce::String SpacepadProcessor::padLibraryId (int mode) const { const juce::ScopedLock sl (stateLock); return padLibs[mode == 1 ? 1 : 0]; }
juce::String SpacepadProcessor::padLibraryIdB (int mode) const { const juce::ScopedLock sl (stateLock); return padLibsB[mode == 1 ? 1 : 0]; }
bool SpacepadProcessor::missingPadLibrary() const { return store.find (padLibraryId()) == nullptr; }
juce::String SpacepadProcessor::crowdSoundName (int s) const { const juce::ScopedLock sl (stateLock); return crowdName[s & 1]; }
juce::String SpacepadProcessor::crowdLibraryId (int s) const { const juce::ScopedLock sl (stateLock); return crowdLib[s & 1]; }
juce::String SpacepadProcessor::padSoundName() const { const juce::ScopedLock sl (stateLock); return padSoundNow; }

void SpacepadProcessor::startPadKey (int key, int mode, bool fromSilence)
{
    auto lib = store.find (padLibraryId (mode));
    auto libB = store.find (padLibraryIdB (mode));
    const int gen = ++generation;
    const float attack = p ("attack"), xf = p ("xfade");
    const bool loop = loopOn();
    if (lib == nullptr || key < 0)
    {
        // pas de librairie pour ce mode : on ne joue jamais un accord du mauvais mode
        pad.stop (0.6f);
        padB.stop (0.6f);
        return;
    }
    auto play = [&] (const Library& l, PadChannel& ch, bool isA)
    {
        auto slot = l.slotFor (key, mode);
        auto* s = l.sound (slot.sound);
        if (s == nullptr) { ch.stop (0.6f); return; }
        if (isA) { const juce::ScopedLock sl (stateLock); padSoundNow = s->name; }
        ++pendingLoads;
        sampleLoader.request (s->path, [this, gen, slot, attack, xf, loop, fromSilence, &ch] (std::shared_ptr<const SampleData> d)
        {
            if (d != nullptr && gen == generation.load())
                ch.start (d, slot.semitones, loop, attack, fromSilence ? 0.01f : xf);
            --pendingLoads;
        });
    };
    play (*lib, pad, true);
    if (libB != nullptr) play (*libB, padB, false);
    else padB.stop (juce::jmax (0.3f, xf));
    retransposeCrowds();
}

void SpacepadProcessor::selectPadLibrary (const juce::String& id, int mode)
{
    mode = mode == 1 ? 1 : 0;
    {
        const juce::ScopedLock sl (stateLock);
        if (padLibs[mode] == id) return;
        padLibs[mode] = id;
    }
    if (padOn && mode == modeNow.load()) startPadKey (keyNow, modeNow, false); // la nouvelle librairie prend le relais en fondu
}

void SpacepadProcessor::selectPadLibraryB (const juce::String& id, int mode)
{
    mode = mode == 1 ? 1 : 0;
    {
        const juce::ScopedLock sl (stateLock);
        if (padLibsB[mode] == id) return;
        padLibsB[mode] = id;
    }
    if (padOn && mode == modeNow.load()) startPadKey (keyNow, modeNow, false);
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
    if (fromSilence && p ("crowdlink") > 0.5f)
        for (int s = 0; s < 2; ++s) if (crowdIdx[s].load() >= 0 && ! crowd[s].isPlaying()) startCrowdSlot (s, p (crowdParam (s, "attack")), 0.5f);
}

void SpacepadProcessor::setMode (int mode)
{
    mode = mode == 1 ? 1 : 0;
    if (modeNow.exchange (mode) == mode) return;
    if (padOn) startPadKey (keyNow, mode, false);
    else retransposeCrowds();
}

void SpacepadProcessor::playPad()
{
    if (padOn) return;
    padOn = true;
    startPadKey (keyNow, modeNow, true);
    if (p ("crowdlink") > 0.5f)
        for (int s = 0; s < 2; ++s) if (crowdIdx[s].load() >= 0 && ! crowd[s].isPlaying()) startCrowdSlot (s, p (crowdParam (s, "attack")), 0.5f);
}

void SpacepadProcessor::stopPad()
{
    padOn = false;
    ++generation;
    const float t = juce::jmax (0.3f, p ("xfade") * 0.5f);
    pad.stop (t);
    padB.stop (t);
    if (p ("crowdlink") > 0.5f) { crowd[0].stop (t); crowd[1].stop (t); }   // crowd lié : s'arrête avec le pad (sa sélection est gardée)
}

void SpacepadProcessor::fadeOutAll()
{
    const float t = juce::jlimit (1.0f, 30.0f, uiSettings().fadeSeconds);
    padOn = false;
    ++generation;
    pad.stop (t); padB.stop (t); crowd[0].stop (t); crowd[1].stop (t);
}

void SpacepadProcessor::stopAll()
{
    stopPad();
    stopCrowd();
}

// ---------------------------------------------------------------------------------------------
// Crowds
// ---------------------------------------------------------------------------------------------
void SpacepadProcessor::startCrowdSlot (int slot, float fadeIn, float xfade)
{
    juce::String libId; int index;
    {
        const juce::ScopedLock sl (stateLock);
        libId = crowdLib[slot];
    }
    index = crowdIdx[slot].load();
    auto lib = store.find (libId);
    if (lib == nullptr || index < 0) return;
    auto* s = lib->sound (index);
    if (s == nullptr) return;
    // suit la tonalité du pad : un crowd enregistré en C joue en Em si le pad est en Em (transposition la plus courte)
    int semis = 0;
    if (p ("crowdfollow") > 0.5f && s->key >= 0)
    {
        const int d = ((keyNow.load() - s->key) % 12 + 12) % 12;
        semis = d > 6 ? d - 12 : d;
    }
    {
        const juce::ScopedLock sl (stateLock);
        crowdName[slot] = s->name; crowdKey[slot] = s->key; crowdMode[slot] = s->mode;
    }
    crowdSemis[slot] = semis;
    const bool loop = loopOn();
    ++pendingLoads;
    sampleLoader.request (s->path, [this, slot, semis, loop, fadeIn, xfade] (std::shared_ptr<const SampleData> d)
    {
        if (d != nullptr) crowd[slot].start (d, semis, loop, fadeIn, xfade);
        --pendingLoads;
    });
}

void SpacepadProcessor::retransposeCrowds()
{
    if (p ("crowdfollow") <= 0.5f) return;
    for (int s = 0; s < 2; ++s)
    {
        int k;
        { const juce::ScopedLock sl (stateLock); k = crowdKey[s]; }
        if (k < 0 || crowdIdx[s].load() < 0 || ! crowd[s].isPlaying()) continue;
        const int d = ((keyNow.load() - k) % 12 + 12) % 12;
        if ((d > 6 ? d - 12 : d) != crowdSemis[s].load()) startCrowdSlot (s, 0.05f, juce::jmax (0.3f, p ("xfade")));
    }
}

void SpacepadProcessor::playCrowd (const juce::String& libId, int index, int slot)
{
    slot &= 1;
    if (crowd[slot].isPlaying() && crowdIdx[slot].load() == index && crowdLibraryId (slot) == libId)
    {
        stopCrowd (slot);
        return;
    }
    if (store.find (libId) == nullptr) return;
    {
        const juce::ScopedLock sl (stateLock);
        crowdLib[slot] = libId;
    }
    crowdIdx[slot] = index;
    newCrowdSound (slot);
    startCrowdSlot (slot, p (crowdParam (slot, "attack")), p (crowdParam (slot, "fade")));
}

void SpacepadProcessor::stopCrowd (int slot)
{
    slot &= 1;
    crowdIdx[slot] = -1;
    crowd[slot].stop (p (crowdParam (slot, "fade")));
}

void SpacepadProcessor::stopCrowd() { stopCrowd (0); stopCrowd (1); }

// ---------------------------------------------------------------------------------------------
// Setlist
// ---------------------------------------------------------------------------------------------
Song SpacepadProcessor::captureSong (const juce::String& name) const
{
    Song s;
    s.name = name;
    s.key = keyNow.load();
    s.mode = modeNow.load();
    {
        const juce::ScopedLock sl (stateLock);
        for (int m = 0; m < 2; ++m) { s.padA[m] = padLibs[m]; s.padB[m] = padLibsB[m]; s.crowdLib[m] = crowdLib[m]; }
    }
    for (int i = 0; i < 2; ++i) s.crowdIndex[i] = crowdIdx[i].load();
    for (auto& id : songParamList())
        if (auto* prm = params.getParameter (id)) s.params[id] = prm->convertFrom0to1 (prm->getValue());
    return s;
}

void SpacepadProcessor::addSongFromCurrent (const juce::String& name) { songs.add (captureSong (name)); }
void SpacepadProcessor::updateSongFromCurrent (int index) { songs.update (index, captureSong ({})); }

void SpacepadProcessor::gotoSong (int index)
{
    auto list = songs.songs();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& s = list[(size_t) index];
    songs.setCurrent (index);
    for (auto& kv : s.params) setParam (kv.first.toRawUTF8(), kv.second);
    const bool playing = padOn.load();
    {
        const juce::ScopedLock sl (stateLock);
        for (int m = 0; m < 2; ++m)
        {
            if (s.padA[m].isNotEmpty() && store.find (s.padA[m]) != nullptr) padLibs[m] = s.padA[m];
            padLibsB[m] = store.find (s.padB[m]) != nullptr ? s.padB[m] : juce::String();
        }
    }
    keyNow = s.key;
    modeNow = s.mode;
    // crowds du chant
    for (int c = 0; c < 2; ++c)
    {
        const bool has = s.crowdIndex[c] >= 0 && store.find (s.crowdLib[c]) != nullptr;
        const bool wasPlaying = crowd[c].isPlaying();
        if (! has) { if (wasPlaying || crowdIdx[c].load() >= 0) stopCrowd (c); continue; }
        { const juce::ScopedLock sl (stateLock); crowdLib[c] = s.crowdLib[c]; }
        crowdIdx[c] = s.crowdIndex[c];
        { const juce::ScopedLock fl (fxLock); lastFx[c] = crowdFxSnapshot (c); }   // les effets du chant priment
        if (wasPlaying || (playing && p ("crowdlink") > 0.5f)) startCrowdSlot (c, p (crowdParam (c, "attack")), juce::jmax (0.3f, p (crowdParam (c, "fade"))));
    }
    if (playing) startPadKey (s.key, s.mode, false);     // le chant suivant arrive en fondu
}

void SpacepadProcessor::nextSong()
{
    const int n = (int) songs.songs().size();
    if (n == 0) return;
    gotoSong (juce::jmin (n - 1, songs.current() + 1));
}

void SpacepadProcessor::prevSong()
{
    if (songs.songs().empty()) return;
    gotoSong (juce::jmax (0, songs.current() - 1));
}

// ---------------------------------------------------------------------------------------------
// Fil de commande
// ---------------------------------------------------------------------------------------------
void SpacepadProcessor::waitIdleForTests (int ms)
{
    auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (pendingLoads.load() > 0 && juce::Time::getMillisecondCounter() < end)
        juce::Thread::sleep (5);
}

void SpacepadProcessor::runControlForTests() { controlStep(); trackCrowdFx(); }

void SpacepadProcessor::controlStep()
{
    if (settingsVersion() != lastSettingsVersion) refreshMidiSettings();
    // apprentissage : la prochaine pédale / le prochain bouton touché est assigné à l'action choisie
    const int code = learnedCode.exchange (-1);
    const int act = learnAction.load();
    if (code >= 0 && act >= 0)
    {
        auto u = uiSettings();
        for (auto& m : u.midiMap) if (m == code) m = -1;
        u.midiMap[(size_t) act] = code;
        setUiSettings (u);
        learnAction = -1;
        refreshMidiSettings();
    }
    const int zn = learnedZone.exchange (-1);
    if (zn >= 0) { auto u = uiSettings(); u.zoneLow = zn; u.midiKeyMode = 0; setUiSettings (u); refreshMidiSettings(); }

    // actions des pédales
    const auto pend = pendingActions.exchange (0);
    if (pend & (1u << actPlayStop)) { if (padOn) stopPad(); else playPad(); }
    if (pend & (1u << actNextSong)) nextSong();
    if (pend & (1u << actPrevSong)) prevSong();
    if (pend & (1u << actModeToggle)) setMode (modeNow.load() == 1 ? 0 : 1);
    if (pend & (1u << actFreeze)) setParam ("freeze", p ("freeze") > 0.5f ? 0.0f : 1.0f);
    if (pend & (1u << actSwell)) setParam ("swell", p ("swell") > 0.5f ? 0.0f : 1.0f);
    if (pend & (1u << actFadeEnd)) fadeOutAll();
    if (pend & (1u << actStopAll)) stopAll();
    const auto cf = contFlags.exchange (0);
    if (cf & 1u) setParam ("padvol", contValue[0].load());
    if (cf & 2u) { setParam ("crowdvol", contValue[1].load()); setParam ("crowdvol2", contValue[1].load()); }
    if (cf & 4u) setParam ("mix", contValue[2].load());

    // touche de la zone de commande : choisit la tonalité
    const int k = midiKey.exchange (-1);
    if (k >= 0 && ! (padOn && k == keyNow.load()))
    {
        const bool fromSilence = ! padOn.load();
        keyNow = k; padOn = true;
        startPadKey (k, modeNow, fromSilence);
    }
    // changement de programme MIDI : chant N de la setlist (sinon librairie suivante)
    const int prog = midiProgram.exchange (-1);
    if (prog >= 0)
    {
        if (! songs.songs().empty()) gotoSong (juce::jmin (prog, (int) songs.songs().size() - 1));
        else
        {
            const int m = modeNow.load();
            std::vector<juce::String> pads;
            for (auto& l : store.snapshot()) if (l.type == LibType::Pad && l.countMode (m) > 0) pads.push_back (l.id);
            if (! pads.empty()) selectPadLibrary (pads[(size_t) prog % pads.size()], m);
        }
    }
}

void SpacepadProcessor::run()
{
    int tick = 0;
    while (! threadShouldExit())
    {
        wait (5);
        controlStep();
        if (tick % 40 == 0) trackCrowdFx();
        if (tick % 100 == 0) prefs.saveIfDirty();
        if (++tick % 200 == 0) // environ 1 fois par seconde : bibliothèques ou setlist changées ailleurs
        {
            if (store.reloadIfChanged()) autoSelectLibraries();
            songs.reloadIfChanged();
            prefs.reloadIfChanged();
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Sauvegarde dans le projet (Logic, Ableton…)
// ---------------------------------------------------------------------------------------------
void SpacepadProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty ("padLibraryMajor", padLibraryId (0), nullptr);
    state.setProperty ("padLibraryMinor", padLibraryId (1), nullptr);
    state.setProperty ("padLibraryBMajor", padLibraryIdB (0), nullptr);
    state.setProperty ("padLibraryBMinor", padLibraryIdB (1), nullptr);
    state.setProperty ("crowdLibrary", crowdLibraryId (0), nullptr);
    state.setProperty ("crowdIndex", crowdIdx[0].load(), nullptr);
    state.setProperty ("crowdLibrary2", crowdLibraryId (1), nullptr);
    state.setProperty ("crowdIndex2", crowdIdx[1].load(), nullptr);
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
        {
            const juce::ScopedLock sl (stateLock);
            const char* keys[] = { "padLibraryMajor", "padLibraryMinor" };
            const char* keysB[] = { "padLibraryBMajor", "padLibraryBMinor" };
            for (int m = 0; m < 2; ++m)
            {
                auto lib = state.getProperty (keys[m]).toString();
                if (lib.isEmpty()) lib = state.getProperty ("padLibrary").toString();   // projets de la première version
                if (lib.isNotEmpty() && store.find (lib) != nullptr) padLibs[m] = lib;
                auto libB = state.getProperty (keysB[m]).toString();
                padLibsB[m] = store.find (libB) != nullptr ? libB : juce::String();
            }
            crowdLib[0] = state.getProperty ("crowdLibrary").toString();
            crowdLib[1] = state.getProperty ("crowdLibrary2").toString();
            keyNow = juce::jlimit (0, 11, (int) state.getProperty ("key", 4));
            modeNow = (int) state.getProperty ("mode", 1) == 1 ? 1 : 0;
        }
        crowdIdx[0] = (int) state.getProperty ("crowdIndex", -1);
        crowdIdx[1] = (int) state.getProperty ("crowdIndex2", -1);
        autoSelectLibraries();
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
