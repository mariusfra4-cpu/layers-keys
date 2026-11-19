#pragma once

#include "LinkSync.h"

#include <AfricanLoops/AfricanLoops.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <mutex>

namespace ids {
inline const juce::String master{"master"}, masterPan{"masterPan"}, masterMute{"masterMute"};
inline const juce::String lp{"lp"}, hp{"hp"}, warmth{"warmth"};
inline const juce::String bpm{"bpm"}, presetBpm{"presetBpm"}, syncHost{"syncHost"}, link{"link"};
inline const juce::String masterFx{"masterFx"}, sendFx{"sendFx"}, modFx{"modFx"}, fade{"fade"};
inline const juce::String metronome{"metronome"}, signature{"signature"}, feel{"feel"}, mono{"mono"};
inline juce::String gain(int i) { return "gain" + juce::String(i + 1); }
inline juce::String pan(int i) { return "pan" + juce::String(i + 1); }
inline juce::String mute(int i) { return "mute" + juce::String(i + 1); }
inline juce::String solo(int i) { return "solo" + juce::String(i + 1); }
}  // namespace ids

// Le « cerveau » du plugin : lecture, réglages, analyse des loops, effets.
class AfricanLoopsProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    AfricanLoopsProcessor();
    ~AfricanLoopsProcessor() override;

    // --- juce::AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "African Loops Pro"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    // --- transport
    void setPlaying(bool on);
    bool isPlaying() const { return wantPlay.load(); }
    bool isHostSynced() const { return hostSynced.load(); }
    bool isLinkActive() const { return linkOn.load(); }
    int linkPeers() const { return peerCount.load(); }
    double displayedBPM() const { return shownBPM.load(); }
    double playheadPosition() const { return looper ? looper->position() : 0; }

    // --- loops
    // Charge une loop (WAV / MP3) en arrière-plan : analyse, séparation, lecture.
    void loadLoopAsync(const juce::File& file);
    bool isLoading() const { return loading.load(); }
    juce::String loadError() const;
    struct Current {
        juce::File file;
        africanloops::LoopInfo info;
        std::vector<africanloops::Stem> stems;
        std::vector<float> overview;  // forme d'onde réduite pour l'affichage
    };
    std::shared_ptr<const Current> current() const;
    int loadCounter() const { return loads.load(); }

    // --- bibliothèque
    void scanFolderAsync(const juce::File& folder);
    bool isScanning() const { return scanning.load(); }
    void scanProgress(int& done, int& total) const { done = scanDone.load(); total = scanTotal.load(); }
    std::shared_ptr<const africanloops::Library> library() const;
    int libraryCounter() const { return libs.load(); }
    juce::File libraryFolder() const;
    void setCategory(const juce::String& path, const juce::String& category);
    void renameCategory(const juce::String& from, const juce::String& to);
    void removeCategory(const juce::String& name);  // ses loops vont dans « Non classés »

    // --- glisser vers le logiciel de musique
    // Fabrique un WAV de ce qu'on entend (track = -1) ou d'une piste seule.
    juce::File bounceToFile(int track);

    // --- vumètres
    void meters(float& l, float& r) const { l = outPeakL.load(); r = outPeakR.load(); }
    float trackPeak(int i) const;

    static bool loadAudio(const juce::File& f, africanloops::Audio& out, juce::String& error);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void applyFx(juce::AudioBuffer<float>& buffer);
    void publishLoad(std::shared_ptr<Current> c);

    std::unique_ptr<africanloops::Looper> looper;
    double rate = 44100;
    int maxBlock = 512;
    juce::AudioBuffer<float> work;

    // Accès direct aux réglages depuis le fil audio (aucun texte créé, aucune recherche).
    struct Raw {
        std::atomic<float>*master, *masterPan, *masterMute, *lp, *hp, *warmth, *bpm, *presetBpm, *syncHost, *link, *masterFx,
            *sendFx, *modFx, *fade, *metronome, *signature, *feel, *mono;
        std::array<std::atomic<float>*, africanloops::kNumTracks> gain, pan, mute, solo;
    } raw{};

    // Un seul fil pour charger les loops (seule la dernière choisie compte) et
    // un fil de basse priorité pour analyser le dossier : le logiciel hôte garde
    // toujours la main.
    juce::ThreadPool loadPool{juce::ThreadPoolOptions{}.withThreadName("AL load").withNumberOfThreads(1)};
    juce::ThreadPool scanPool{juce::ThreadPoolOptions{}.withThreadName("AL scan").withNumberOfThreads(1)
                                  .withDesiredThreadPriority(juce::Thread::Priority::low)};
    // Ableton Link : activé depuis la minuterie (jamais depuis le fil audio)
    void timerCallback() override;
    LinkSync link;
    std::atomic<bool> linkOn{false};
    std::atomic<int> peerCount{0};
    bool lastLinkWant = false;  // fil audio : dernière demande lecture/arrêt envoyée à Link
    float lastBpmParam = -1;    // minuterie : tempo réglé à la main, à envoyer à Link

    juce::uint32 lastScanMs = 0;
    juce::File lastScanDir;
    mutable std::mutex stateMu;  // protège cur, lib, error, folder
    std::shared_ptr<Current> cur;
    std::shared_ptr<africanloops::Library> lib;
    juce::String error;
    juce::File folder;
    std::atomic<bool> loading{false}, scanning{false};
    std::atomic<int> loads{0}, libs{0}, scanDone{0}, scanTotal{0};
    std::atomic<int> loadTicket{0};

    std::atomic<bool> wantPlay{false}, hostSynced{false};
    std::atomic<double> shownBPM{120}, loopBpm{0};
    std::atomic<float> outPeakL{0}, outPeakR{0};
    bool lastHostPlaying = false;

    // effets
    juce::dsp::StateVariableTPTFilter<float> lowpass, highpass;
    juce::dsp::Compressor<float> comp;
    juce::dsp::Limiter<float> limiter;
    juce::dsp::Limiter<float> safety;  // protection finale, toujours active : jamais au-dessus de -0,3 dB
    juce::dsp::Reverb reverb;
    juce::dsp::Chorus<float> chorus;
    juce::SmoothedValue<float> fadeGain{1.f}, lpHz{20000.f}, hpHz{20.f}, warm{0.f};
    bool stopAfterFade = false;
    bool looperPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AfricanLoopsProcessor)
};
