// Layers Split — processeur audio (application autonome et plugin VST3).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/SeparationJob.h"

#include <array>
#include <atomic>

class LayersSplitProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::Timer
{
public:
    LayersSplitProcessor();
    ~LayersSplitProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Layers Split"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Commandes (fil des messages)
    void openFile (const juce::File& f);
    void startSeparation();
    void cancelJob();
    bool isBusy() const { return job.isRunning(); }

    lsplit::SeparationSettings settings;   // modifiés uniquement depuis le fil des messages

    lsplit::PlayDataPtr getData() const { return current; }
    const lsplit::SeparationJob& getJob() const { return job; }
    juce::String getStatus() const { return status; }
    bool statusIsError() const { return statusError; }
    bool autoSeparatePending = false;

    // Transport
    void play();
    void pause();
    void stop();
    void togglePlay() { isPlaying() ? pause() : play(); }
    bool isPlaying() const { return playing.load(); }
    void seekSeconds (double s);
    double positionSeconds() const;

    std::atomic<bool> followHost { true };  // plugin : suit la lecture du DAW
    std::atomic<bool> listenOriginal { false }; // A/B : écoute du mix original
    std::atomic<bool> loop { false };
    std::atomic<bool> choirDetached { false };  // chœurs détachés en pupitres (soprano, alto, ténor, basse)

    // Ce stem fait-il partie du mixage ? (les pupitres remplacent « Chœurs » quand ils sont détachés)
    bool stemInMix (const lsplit::PlayData& d, int k) const;
    bool anySoloInMix (const lsplit::PlayData& d) const;

    // Console : un réglage par stem
    struct StemControl
    {
        std::atomic<float> gainDb { 0.0f };
        std::atomic<bool> mute { false }, solo { false };
    };
    std::array<StemControl, lsplit::numStems> controls;
    std::atomic<float> masterDb { 0.0f };

    // Niveaux pour les vu-mètres (lus par l'interface)
    std::array<std::atomic<float>, lsplit::numStems> stemLevel {};
    std::atomic<float> masterLevel { 0.0f };

    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    juce::String engineDescription() const;

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    void publish (lsplit::PlayDataPtr d);
    void onJobDone (const lsplit::SeparationJob::Result& r);
    void setStatus (const juce::String& s, bool error = false);
    float stemGainLinear (int k, bool anySolo) const;

    lsplit::SeparationJob job;
    lsplit::PlayDataPtr current;     // fil des messages
    lsplit::PlayDataPtr retired;     // gardé un moment pour ne jamais libérer la mémoire dans le fil audio
    juce::uint32 retiredAt = 0;

    juce::SpinLock playLock;
    const lsplit::PlayData* playData = nullptr; // lu par le fil audio, protégé par playLock

    std::atomic<bool> playing { false };
    std::atomic<double> playPos { 0.0 };      // en échantillons du fichier
    std::atomic<bool> seekRequested { false };
    double deviceRate = 44100.0;
    double lastHostTime = -1.0;
    bool wasHostPlaying = false;

    std::array<juce::LagrangeInterpolator, 2 * (lsplit::numStems + 1)> interp;
    juce::AudioBuffer<float> scratchIn, scratchOut;

    juce::String status;
    bool statusError = false;
    juce::String pendingStateFile;

    JUCE_DECLARE_WEAK_REFERENCEABLE (LayersSplitProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayersSplitProcessor)
};
