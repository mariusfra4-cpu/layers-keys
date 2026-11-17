// Spacepad by JB Praise — le cœur du logiciel (application, VST3 et Audio Unit partagent ce code).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Library.h"
#include "PadEngine.h"
#include "Effects.h"

namespace spacepad
{
class SpacepadProcessor : public juce::AudioProcessor,
                          private juce::Thread
{
public:
    SpacepadProcessor();
    ~SpacepadProcessor() override;

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Spacepad"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Spacepad"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- commandes (depuis l'interface) ---
    void selectPadLibrary (const juce::String& id);
    void pressKey (int key);                 // joue la tonalité ; rappuyer sur la tonalité jouée l'arrête
    void setMode (int mode);                 // 0 majeur, 1 mineur
    void playPad();                          // relance la tonalité courante
    void stopPad();
    void playCrowd (const juce::String& libId, int soundIndex); // rappuyer sur le son joué l'arrête
    void stopCrowd();
    void stopAll();

    // --- état (pour l'affichage) ---
    juce::String padLibraryId() const;
    int currentKey() const { return keyNow.load(); }
    int currentMode() const { return modeNow.load(); }
    bool padActive() const { return padOn.load(); }
    bool padSounding() const { return pad.isPlaying(); }
    bool crowdSounding() const { return crowd.isPlaying(); }
    juce::String crowdLibraryId() const;
    int crowdSoundIndex() const { return crowdIndex.load(); }
    float padPosition() const { return pad.position(); }
    std::shared_ptr<const SampleData> padSample() const { return pad.current(); }
    float outputLevel() const { return level.load(); }
    bool midiActivity() { return midiSeen.exchange (false); }
    bool loopOn() const;
    juce::String padSoundName() const;       // nom du fichier joué

    LibraryStore& libraries() { return store; }
    SampleLoader& loader() { return sampleLoader; }
    juce::AudioProcessorValueTreeState params;

    // pour les tests : attendre que les chargements en cours soient faits
    void waitIdleForTests (int ms);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void run() override;                     // fil de commande : chargements, MIDI, relecture des bibliothèques
    void startPadKey (int key, int mode, bool fromSilence);
    FxParams readFx() const;
    float p (const char* id) const { return params.getRawParameterValue (id)->load(); }

    LibraryStore store;
    SampleLoader sampleLoader;
    PadChannel pad, crowd;
    FxChain fx;
    juce::AudioBuffer<float> mainBuf, subBuf, shimBuf, crowdBuf;
    int maxBlock = 512;

    mutable juce::CriticalSection stateLock;
    juce::String padLib, crowdLib;
    std::atomic<int> keyNow { 4 }, modeNow { 1 }, crowdIndex { -1 };
    std::atomic<bool> padOn { false };
    std::atomic<int> midiKey { -1 }, midiProgram { -1 };
    std::atomic<int> generation { 0 };
    std::atomic<float> level { 0 };
    std::atomic<bool> midiSeen { false };
    std::atomic<bool> lastLoop { true };
    juce::String padSoundNow;
    std::atomic<int> pendingLoads { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacepadProcessor)
};
}
