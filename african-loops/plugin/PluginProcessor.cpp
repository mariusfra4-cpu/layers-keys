#include "PluginProcessor.h"

#include "PluginEditor.h"

using namespace africanloops;

namespace {
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using PID = juce::ParameterID;

juce::File toFile(const std::string& utf8) { return juce::File(juce::String::fromUTF8(utf8.c_str())); }
std::string toUtf8(const juce::File& f) { return f.getFullPathName().toStdString(); }

float lpToHz(float x) { return 200.f * std::pow(100.f, juce::jlimit(0.f, 1.f, x)); }  // 200 Hz .. 20 kHz
float hpToHz(float x) { return 20.f * std::pow(100.f, juce::jlimit(0.f, 1.f, x)); }   // 20 Hz .. 2 kHz
}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AfricanLoopsProcessor::layout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return juce::String(juce::roundToInt(v * 100)) + " %"; });
    auto db = juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
        return v <= 0.0001f ? juce::String("-inf dB") : juce::String(juce::Decibels::gainToDecibels(v), 1) + " dB";
    });
    p.push_back(std::make_unique<APF>(PID{ids::master, 1}, "Master", juce::NormalisableRange<float>(0.f, 1.5f), 1.f, pct));
    p.push_back(std::make_unique<APF>(PID{ids::masterPan, 1}, "Master Pan", juce::NormalisableRange<float>(-1.f, 1.f), 0.f));
    p.push_back(std::make_unique<APB>(PID{ids::masterMute, 1}, "Master Mute", false));
    p.push_back(std::make_unique<APF>(PID{ids::lp, 1}, "LP Filter", juce::NormalisableRange<float>(0.f, 1.f), 1.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            return v >= 0.999f ? juce::String("Off") : juce::String(juce::roundToInt(lpToHz(v))) + " Hz"; })));
    p.push_back(std::make_unique<APF>(PID{ids::hp, 1}, "HP Filter", juce::NormalisableRange<float>(0.f, 1.f), 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            return v <= 0.001f ? juce::String("Off") : juce::String(juce::roundToInt(hpToHz(v))) + " Hz"; })));
    p.push_back(std::make_unique<APF>(PID{ids::warmth, 1}, "Warmth", juce::NormalisableRange<float>(0.f, 1.f), 0.f, pct));
    p.push_back(std::make_unique<APF>(PID{ids::bpm, 1}, "Global BPM", juce::NormalisableRange<float>(40.f, 240.f, 0.01f), 120.f));
    p.push_back(std::make_unique<APB>(PID{ids::presetBpm, 1}, "Preset BPM", false));
    p.push_back(std::make_unique<APB>(PID{ids::syncHost, 1}, "Sync Host", true));
    p.push_back(std::make_unique<APB>(PID{ids::masterFx, 1}, "Master FX", false));
    p.push_back(std::make_unique<APB>(PID{ids::sendFx, 1}, "Send FX", false));
    p.push_back(std::make_unique<APB>(PID{ids::modFx, 1}, "Mod FX", false));
    p.push_back(std::make_unique<APB>(PID{ids::fade, 1}, "Fade", false));
    p.push_back(std::make_unique<APB>(PID{ids::metronome, 1}, "Metronome", false));
    p.push_back(std::make_unique<APC>(PID{ids::signature, 1}, "Signature", juce::StringArray{"4/4", "3/4", "6/8"}, 0));
    p.push_back(std::make_unique<APC>(PID{ids::feel, 1}, "Feel", juce::StringArray{"STANDARD", "DOUBLE", "HALF"}, 0));
    p.push_back(std::make_unique<APB>(PID{ids::mono, 1}, "Mono", false));
    for (int i = 0; i < kNumTracks; ++i) {
        juce::String n = "Ch " + juce::String(i + 1) + " ";
        p.push_back(std::make_unique<APF>(PID{ids::gain(i), 1}, n + "Volume",
                                          juce::NormalisableRange<float>(0.f, 1.5f, 0.f, 0.6f), 1.f, db));
        p.push_back(std::make_unique<APF>(PID{ids::pan(i), 1}, n + "Pan", juce::NormalisableRange<float>(-1.f, 1.f), 0.f));
        p.push_back(std::make_unique<APB>(PID{ids::mute(i), 1}, n + "Mute", false));
        p.push_back(std::make_unique<APB>(PID{ids::solo(i), 1}, n + "Solo", false));
    }
    return {p.begin(), p.end()};
}

AfricanLoopsProcessor::AfricanLoopsProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "AfricanLoops", layout()) {
    looper = std::make_unique<Looper>(44100);
}

AfricanLoopsProcessor::~AfricanLoopsProcessor() { pool.removeAllJobs(true, 10000); }

bool AfricanLoopsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void AfricanLoopsProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    {
        std::lock_guard<std::mutex> lk(stateMu);
        if (!looper || std::abs(rate - sampleRate) > 0.5) {
            looper = std::make_unique<Looper>((int)sampleRate);
            if (cur) looper->load(cur->info, cur->stems);
        }
    }
    rate = sampleRate;
    maxBlock = std::max(1, samplesPerBlock);
    work.setSize(2, maxBlock, false, true, true);
    juce::dsp::ProcessSpec spec{sampleRate, (juce::uint32)maxBlock, 2};
    lowpass.prepare(spec);
    lowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    highpass.prepare(spec);
    highpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    comp.prepare(spec);
    comp.setThreshold(-14.f);
    comp.setRatio(3.f);
    comp.setAttack(8.f);
    comp.setRelease(120.f);
    limiter.prepare(spec);
    limiter.setThreshold(-0.5f);
    limiter.setRelease(80.f);
    safety.prepare(spec);
    safety.setThreshold(-0.3f);
    safety.setRelease(60.f);
    reverb.prepare(spec);
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = 0.55f; rp.damping = 0.5f; rp.wetLevel = 0.22f; rp.dryLevel = 1.f; rp.width = 1.f;
    reverb.setParameters(rp);
    chorus.prepare(spec);
    chorus.setRate(0.6f); chorus.setDepth(0.3f); chorus.setCentreDelay(7.f); chorus.setFeedback(0.f); chorus.setMix(0.35f);
    fadeGain.reset(sampleRate, 0.5);
    fadeGain.setCurrentAndTargetValue(1.f);
    lpHz.reset(sampleRate, 0.05);
    hpHz.reset(sampleRate, 0.05);
    warm.reset(sampleRate, 0.05);
    lpHz.setCurrentAndTargetValue(lpToHz(*params.getRawParameterValue(ids::lp)));
    hpHz.setCurrentAndTargetValue(hpToHz(*params.getRawParameterValue(ids::hp)));
}

void AfricanLoopsProcessor::setPlaying(bool on) { wantPlay.store(on); }

void AfricanLoopsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0 || !looper) return;
    if (n > work.getNumSamples()) work.setSize(2, n, false, false, true);  // hôte qui dépasse la taille annoncée

    auto val = [this](const juce::String& id) { return params.getRawParameterValue(id)->load(); };

    // --- tempo et lecture du logiciel hôte
    double hostBpm = 0, hostPpq = -1;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) hostBpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) hostPpq = *q;
        }
    const bool standalone = wrapperType == wrapperType_Standalone;
    const bool sync = val(ids::syncHost) > 0.5f && !standalone && hostBpm >= 20;
    hostSynced.store(sync);

    MixSettings s;
    s.globalBPM = sync ? hostBpm : val(ids::bpm);
    s.presetBPM = val(ids::presetBpm) > 0.5f;
    const int feelIdx = (int)val(ids::feel);
    s.feel = feelIdx == 1 ? 2.0 : feelIdx == 2 ? 0.5 : 1.0;
    const int sig = (int)val(ids::signature);
    s.beatsPerBar = sig == 1 ? 3 : sig == 2 ? 6 : 4;
    s.metronome = val(ids::metronome) > 0.5f;
    s.master = val(ids::master);
    s.masterPan = val(ids::masterPan);
    s.masterMute = val(ids::masterMute) > 0.5f;
    for (int i = 0; i < kNumTracks; ++i) {
        auto& t = s.tracks[(size_t)i];
        t.gain = val(ids::gain(i));
        t.pan = val(ids::pan(i));
        t.mute = val(ids::mute(i)) > 0.5f;
        t.solo = val(ids::solo(i)) > 0.5f;
    }
    // tempo affiché : celui qu'on entend (jamais de verrou ici, on est dans le fil audio)
    const double orig = loopBpm.load();
    shownBPM.store((s.presetBPM && orig > 0 ? orig : s.globalBPM) * s.feel);

    // --- lecture : bouton Play, ou lecture du logiciel hôte quand on est calé dessus
    const bool hostDrives = sync && hostPlaying;
    if (sync && lastHostPlaying && !hostPlaying) wantPlay.store(false);  // l'hôte s'arrête : on s'arrête
    if (hostDrives && !lastHostPlaying) wantPlay.store(true);
    lastHostPlaying = hostPlaying;
    const bool want = wantPlay.load() || hostDrives;
    const bool fade = val(ids::fade) > 0.5f;
    if (want && !looperPlaying) {
        looperPlaying = true;
        stopAfterFade = false;
        if (fade) { fadeGain.setCurrentAndTargetValue(0.f); fadeGain.setTargetValue(1.f); }
        else fadeGain.setCurrentAndTargetValue(1.f);
    } else if (!want && looperPlaying && !stopAfterFade) {
        if (fade) { stopAfterFade = true; fadeGain.setTargetValue(0.f); }
        else looperPlaying = false;
    } else if (want && stopAfterFade) {  // relancé pendant le fondu de fin
        stopAfterFade = false;
        fadeGain.setTargetValue(1.f);
    }
    if (stopAfterFade && !fadeGain.isSmoothing()) {
        looperPlaying = false;
        stopAfterFade = false;
        fadeGain.setCurrentAndTargetValue(1.f);
    }
    s.playing = looperPlaying;
    s.hostBeats = hostDrives && hostPpq >= 0 ? hostPpq : -1;

    float* L = work.getWritePointer(0);
    float* R = work.getWritePointer(1);
    looper->process(s, L, R, n);

    juce::AudioBuffer<float> view(work.getArrayOfWritePointers(), 2, n);
    applyFx(view);

    // fondu d'entrée / de sortie
    if (fadeGain.isSmoothing() || fadeGain.getCurrentValue() < 1.f) {
        for (int i = 0; i < n; ++i) {
            float g = fadeGain.getNextValue();
            L[i] *= g;
            R[i] *= g;
        }
    }
    if (val(ids::mono) > 0.5f)
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0.5f * (L[i] + R[i]);

    // protection : le son ne dépasse jamais le maximum (pas de grésillement
    // dans le logiciel de musique, même avec tous les effets et le volume à fond)
    {
        juce::dsp::AudioBlock<float> blk(view);
        juce::dsp::ProcessContextReplacing<float> ctx(blk);
        safety.process(ctx);
    }
    // plafond doux à -0,3 dB : arrondit les toutes petites crêtes au lieu de les écrêter
    constexpr float knee = 0.8f, ceiling = 0.966f;
    for (auto* ch : {L, R})
        for (int i = 0; i < n; ++i) {
            float a = std::abs(ch[i]);
            if (a > knee) ch[i] = std::copysign(knee + (ceiling - knee) * std::tanh((a - knee) / (ceiling - knee)), ch[i]);
        }

    float pl = outPeakL.load() * 0.85f, pr = outPeakR.load() * 0.85f;
    for (int i = 0; i < n; ++i) {
        // protection : jamais de valeur invalide envoyée au logiciel hôte
        if (!std::isfinite(L[i])) L[i] = 0;
        if (!std::isfinite(R[i])) R[i] = 0;
        pl = std::max(pl, std::abs(L[i]));
        pr = std::max(pr, std::abs(R[i]));
    }
    outPeakL.store(pl);
    outPeakR.store(pr);

    if (buffer.getNumChannels() >= 2) {
        buffer.copyFrom(0, 0, L, n);
        buffer.copyFrom(1, 0, R, n);
    } else if (buffer.getNumChannels() == 1) {
        for (int i = 0; i < n; ++i) buffer.getWritePointer(0)[i] = 0.5f * (L[i] + R[i]);
    }
}

void AfricanLoopsProcessor::applyFx(juce::AudioBuffer<float>& b) {
    auto val = [this](const juce::String& id) { return params.getRawParameterValue(id)->load(); };
    juce::dsp::AudioBlock<float> block(b);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    const int n = b.getNumSamples();

    // filtres globaux
    const float lpv = val(ids::lp), hpv = val(ids::hp);
    lpHz.setTargetValue(lpToHz(lpv));
    hpHz.setTargetValue(hpToHz(hpv));
    lpHz.skip(n);
    hpHz.skip(n);
    if (lpv < 0.999f || lpHz.isSmoothing()) {
        lowpass.setCutoffFrequency(juce::jlimit(20.f, (float)(rate * 0.45), lpHz.getCurrentValue()));
        lowpass.process(ctx);
    }
    if (hpv > 0.001f || hpHz.isSmoothing()) {
        highpass.setCutoffFrequency(juce::jlimit(10.f, (float)(rate * 0.45), hpHz.getCurrentValue()));
        highpass.process(ctx);
    }
    // chaleur : saturation douce, comme une bande ou une console
    warm.setTargetValue(val(ids::warmth));
    if (warm.getTargetValue() > 0.001f || warm.isSmoothing()) {
        for (int i = 0; i < n; ++i) {
            const float w = warm.getNextValue(), drive = 1.f + 3.f * w, norm = 1.f / std::tanh(drive);
            for (int c = 0; c < 2; ++c) {
                float& x = b.getWritePointer(c)[i];
                x += w * (std::tanh(drive * x) * norm - x);
            }
        }
    } else {
        warm.skip(n);
    }
    if (val(ids::modFx) > 0.5f) chorus.process(ctx);
    if (val(ids::sendFx) > 0.5f) reverb.process(ctx);
    if (val(ids::masterFx) > 0.5f) {
        comp.process(ctx);
        b.applyGain(juce::Decibels::decibelsToGain(3.f));  // compensation
        limiter.process(ctx);
    }
}

// ------------------------------------------------------------------ chargement

bool AfricanLoopsProcessor::loadAudio(const juce::File& f, Audio& out, juce::String& err) {
    if (!f.existsAsFile()) { err = "Fichier introuvable"; return false; }
    std::string e;
    if (f.hasFileExtension("wav;wave") && loadWAV(toUtf8(f), out, e) && out.frames() > 0) return true;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();  // WAV, AIFF, FLAC, Ogg, MP3
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
    if (!reader) { err = e.empty() ? juce::String("Format non reconnu (WAV ou MP3)") : juce::String::fromUTF8(e.c_str()); return false; }
    const auto maxLen = (juce::int64)(reader->sampleRate * kMaxSeconds);
    const int len = (int)std::min(reader->lengthInSamples, maxLen);
    if (len <= 0 || reader->sampleRate < 1000) { err = juce::String::fromUTF8("Fichier vide ou abîmé"); return false; }
    juce::AudioBuffer<float> buf((int)std::max(1u, std::min(2u, reader->numChannels)), len);
    if (!reader->read(&buf, 0, len, 0, true, true)) { err = "Lecture impossible"; return false; }
    out = Audio{};
    out.rate = (int)std::lround(reader->sampleRate);
    out.L.assign(buf.getReadPointer(0), buf.getReadPointer(0) + len);
    const float* r = buf.getReadPointer(buf.getNumChannels() > 1 ? 1 : 0);
    out.R.assign(r, r + len);
    for (auto* v : {&out.L, &out.R})
        for (auto& x : *v)
            if (!std::isfinite(x)) x = 0;
    return true;
}

void AfricanLoopsProcessor::loadLoopAsync(const juce::File& file) {
    const int ticket = ++loadTicket;
    loading.store(true);
    pool.addJob([this, file, ticket] {
        auto c = std::make_shared<Current>();
        c->file = file;
        Audio a;
        juce::String err;
        if (!loadAudio(file, a, err)) {
            if (ticket == loadTicket.load()) {
                std::lock_guard<std::mutex> lk(stateMu);
                error = file.getFileName() + " : " + err;
                loading.store(false);
                ++loads;
            }
            return;
        }
        if (ticket != loadTicket.load()) return;  // une autre loop a été choisie entre-temps
        c->info = separate(a, toUtf8(file), c->stems);
        // forme d'onde réduite (affichage)
        const int points = 1200, frames = a.frames();
        c->overview.assign(points, 0.f);
        for (int i = 0; i < frames; ++i) {
            auto p = (size_t)((juce::int64)i * points / std::max(1, frames));
            auto j = (size_t)i;
            c->overview[p] = std::max(c->overview[p], 0.5f * std::abs(a.L[j] + a.R[j]));
        }
        if (ticket != loadTicket.load()) return;
        publishLoad(c);
    });
}

void AfricanLoopsProcessor::publishLoad(std::shared_ptr<Current> c) {
    {
        std::lock_guard<std::mutex> lk(stateMu);
        cur = std::move(c);
        loopBpm.store(cur->info.tempo.bpm);
        error.clear();
        if (looper) looper->load(cur->info, cur->stems);
    }
    loading.store(false);
    ++loads;
}

std::shared_ptr<const AfricanLoopsProcessor::Current> AfricanLoopsProcessor::current() const {
    std::lock_guard<std::mutex> lk(stateMu);
    return cur;
}

juce::String AfricanLoopsProcessor::loadError() const {
    std::lock_guard<std::mutex> lk(stateMu);
    return error;
}

void AfricanLoopsProcessor::scanFolderAsync(const juce::File& dir) {
    if (!dir.isDirectory() || scanning.exchange(true)) return;
    scanDone.store(0);
    scanTotal.store(0);
    {
        std::lock_guard<std::mutex> lk(stateMu);
        folder = dir;
    }
    ++libs;
    pool.addJob([this, dir] {
        auto l = std::make_shared<Library>();
        std::string err;
        bool ok = Library::scan(
            toUtf8(dir), *l, err,
            [this](int d, int t, const std::string&) { scanDone.store(d); scanTotal.store(t); },
            [](const std::string& path, Audio& out, std::string& e) {
                juce::String je;
                bool r = loadAudio(toFile(path), out, je);
                e = je.toStdString();
                return r;
            });
        {
            std::lock_guard<std::mutex> lk(stateMu);
            if (ok) lib = l;
            else error = juce::String::fromUTF8(err.c_str());
        }
        scanning.store(false);
        ++libs;
    });
}

std::shared_ptr<const Library> AfricanLoopsProcessor::library() const {
    std::lock_guard<std::mutex> lk(stateMu);
    return lib;
}

juce::File AfricanLoopsProcessor::libraryFolder() const {
    std::lock_guard<std::mutex> lk(stateMu);
    return folder;
}

void AfricanLoopsProcessor::setCategory(const juce::String& path, const juce::String& category) {
    std::lock_guard<std::mutex> lk(stateMu);
    if (!lib) return;
    auto copy = std::make_shared<Library>(*lib);
    if (copy->setCategory(path.toStdString(), category.toStdString())) {
        copy->save();
        lib = copy;
        ++libs;
    }
}

float AfricanLoopsProcessor::trackPeak(int i) const {
    std::lock_guard<std::mutex> lk(stateMu);
    return looper ? looper->trackPeak(i) : 0.f;
}

// ------------------------------------------------------------------ glisser-déposer

juce::File AfricanLoopsProcessor::bounceToFile(int trackIndex) {
    std::shared_ptr<Audio> a;
    double bpm = 0;
    std::shared_ptr<Current> c;
    {
        std::lock_guard<std::mutex> lk(stateMu);
        if (!looper || !cur) return {};
        c = cur;
        a = looper->bounce(trackIndex, 1, bpm);
    }
    if (!a || a->frames() == 0) return {};
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("African Loops");
    dir.createDirectory();
    // ménage : fichiers de plus d'un jour
    for (const auto& old : dir.findChildFiles(juce::File::findFiles, false, "*.wav"))
        if (old.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days(1)) old.deleteFile();
    juce::String part = "Mix";
    if (trackIndex >= 0 && trackIndex < (int)c->stems.size()) part = juce::String(c->stems[(size_t)trackIndex].kind).replace("/", "-");
    juce::String bpmText = std::abs(bpm - std::round(bpm)) < 0.01 ? juce::String((int)std::lround(bpm)) : juce::String(bpm, 2);
    auto name = juce::File::createLegalFileName(c->file.getFileNameWithoutExtension() + " " + part + " " + bpmText + " BPM.wav");
    auto f = dir.getChildFile(name);
    const int beats = (int)std::lround(a->seconds() * bpm / 60.0);
    auto bytes = encodeWAV(*a, bpm, beats);
    if (!f.replaceWithData(bytes.data(), bytes.size())) return {};
    return f;
}

// ------------------------------------------------------------------ état

void AfricanLoopsProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    if (auto c = current()) state.setProperty("loop", c->file.getFullPathName(), nullptr);
    state.setProperty("folder", libraryFolder().getFullPathName(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void AfricanLoopsProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName(params.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    params.replaceState(state);
    juce::File dir(state.getProperty("folder").toString());
    if (dir.isDirectory()) {
        auto l = std::make_shared<Library>();
        if (Library::loadIndex(toUtf8(dir), *l)) {  // index déjà fait : immédiat
            l->root = toUtf8(dir);
            std::lock_guard<std::mutex> lk(stateMu);
            lib = l;
            folder = dir;
            ++libs;
        }
        // puis on regarde si des loops ont été ajoutées ou modifiées depuis
        // (seules celles-là sont analysées)
        scanFolderAsync(dir);
    }
    juce::File loop(state.getProperty("loop").toString());
    if (loop.existsAsFile()) loadLoopAsync(loop);
}

juce::AudioProcessorEditor* AfricanLoopsProcessor::createEditor() { return new AfricanLoopsEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AfricanLoopsProcessor(); }
