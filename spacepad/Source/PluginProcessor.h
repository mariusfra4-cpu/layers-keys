// Spacepad by JB Praise — le cœur du logiciel (application, VST3 et Audio Unit partagent ce code).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Library.h"
#include "PadEngine.h"
#include "Effects.h"
#include "Setlist.h"
#include "CrowdPrefs.h"
#include "Modulation.h"
#include "Lang.h"

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

    // --- pads ---
    void selectPadLibrary (const juce::String& id, int mode);    // pad A, pour le majeur (0) ou le mineur (1)
    void selectPadLibraryB (const juce::String& id, int mode);   // pad B mélangé à A (vide = aucun)
    void pressKey (int key);                 // joue la tonalité ; rappuyer sur la tonalité jouée l'arrête
    void setMode (int mode);                 // 0 majeur, 1 mineur
    void playPad();
    void stopPad();
    void fadeOutAll();                       // « Fin du chant » : tout s'efface en douceur
    void stopAll();

    // --- crowds (2 à la fois, indépendants du pad ou liés à lui) ---
    void playCrowd (const juce::String& libId, int soundIndex, int slot = 0);  // rappuyer sur le son joué l'arrête
    void stopCrowd (int slot);
    void stopCrowd();                        // les deux

    // --- effets des crowds, tempo, écoute seule ---
    static juce::String crowdParam (int slot, const char* name) { return "c" + juce::String ((slot & 1) + 1) + name; }
    static const juce::StringArray& crowdFxNames();      // chorus, delay, dtime, dfb, reverb, width, dist, move, lowcut, attack, fade
    CrowdPrefs& crowdPrefs() { return prefs; }
    juce::String crowdSoundKey (int slot) const;            // son joué dans ce crowd (pour les favoris et ses effets)
    void clearCrowdSoundFx (int slot);                   // oublie les effets mémorisés pour le son de ce crowd
    void setSolo (bool on) { soloOn = on; }              // « Écouter seul » : coupe le pad pour entendre les crowds
    bool solo() const { return soloOn.load(); }
    double tempoBpm() const;                             // tempo du delay (celui du logiciel de musique s'il en donne un)
    bool hostTempo() const { return hostBpm.load() > 0; }
    void tapTempo() { tapTempoAt (juce::Time::getMillisecondCounterHiRes()); }
    void tapTempoAt (double ms);
    int crowdTarget() const { return target.load(); }    // crowd qui recevra le prochain son touché
    void setCrowdTarget (int s) { target = s & 1; }
    float crowdLevel (int slot) const { return crowdLvl[slot & 1].load(); }
    float delaySeconds (int divisionIndex) const;

    // --- setlist ---
    Setlist& setlist() { return songs; }
    Song captureSong (const juce::String& name) const;   // le réglage actuel, pour l'enregistrer comme chant
    void addSongFromCurrent (const juce::String& name);
    void updateSongFromCurrent (int index);
    void gotoSong (int index);               // applique le chant (en fondu si le pad joue)
    void nextSong();
    void prevSong();

    // --- MIDI : apprendre une pédale ou un bouton, ou la zone de tonalité ---
    void learnMidi (int action) { learnAction = action; }
    void learnZone() { learnZoneFlag = true; }
    int learningAction() const { return learnAction.load(); }
    bool learningZone() const { return learnZoneFlag.load(); }

    // --- état (pour l'affichage) ---
    juce::String padLibraryId() const;                    // pad A du mode en cours
    juce::String padLibraryId (int mode) const;
    juce::String padLibraryIdB (int mode) const;
    bool missingPadLibrary() const;
    int currentKey() const { return keyNow.load(); }
    int currentMode() const { return modeNow.load(); }
    bool padActive() const { return padOn.load(); }
    bool padSounding() const { return pad.isPlaying() || padB.isPlaying(); }
    bool crowdSounding (int slot) const { return crowd[slot & 1].isPlaying(); }
    bool crowdSounding() const { return crowd[0].isPlaying() || crowd[1].isPlaying(); }
    juce::String crowdLibraryId (int slot = 0) const;
    int crowdSoundIndex (int slot = 0) const { return crowdIdx[slot & 1].load(); }
    float crowdPosition (int slot = 0) const { return crowd[slot & 1].position(); }
    juce::String crowdSoundName (int slot = 0) const;
    int crowdSemitones (int slot = 0) const { return crowdSemis[slot & 1].load(); }
    void copyScope (float* dest, int n) const;
    float padPosition() const { return pad.position(); }
    std::shared_ptr<const SampleData> padSample() const { return pad.current(); }
    float outputLevel() const { return level.load(); }
    bool midiActivity() { return midiSeen.exchange (false); }
    bool loopOn() const;
    juce::String padSoundName() const;

    LibraryStore& libraries() { return store; }
    SampleLoader& loader() { return sampleLoader; }
    juce::AudioProcessorValueTreeState params;

    void waitIdleForTests (int ms);          // tests : attendre que les chargements soient faits
    void runControlForTests();               // tests : exécuter tout de suite les commandes MIDI en attente

    // Enregistrement dans le logiciel de musique : chaque action (tonalité, mode, jouer, chant, crowds) est aussi un
    // paramètre automatisable. Pendant l'enregistrement, Ableton garde les clics ; à la lecture, il les rejoue.
    struct AutoScope
    {
        explicit AutoScope (SpacepadProcessor& pp) : p (pp), sl (pp.autoLock) { ++p.autoDepth; }
        ~AutoScope() { if (--p.autoDepth == 0) p.pushAuto(); }
        SpacepadProcessor& p;
        const juce::ScopedLock sl;
    };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void run() override;                     // fil de commande : chargements, MIDI, relecture des fichiers communs
    void controlStep();
    void startPadKey (int key, int mode, bool fromSilence);
    void startCrowdSlot (int slot, float fadeIn, float xfade);
    void retransposeCrowds();
    void autoSelectLibraries();
    void refreshMidiSettings();
    FxParams readFx() const;
    CrowdFxParams readCrowdFx (int slot) const;
    CrowdPrefs::Fx crowdFxSnapshot (int slot) const;
    void applyCrowdFx (int slot, const CrowdPrefs::Fx&);
    void newCrowdSound (int slot);
    void trackCrowdFx();
    void pushAuto();                         // l'état actuel → paramètres (le logiciel de musique l'enregistre)
    void pullAuto();                         // paramètres changés par le logiciel de musique (lecture de l'automation) → actions
    void setAuto (const char* id, float realValue);
    juce::CriticalSection autoLock;
    int autoDepth = 0, lastSongParam = 0;
    float p (const char* id) const { return params.getRawParameterValue (id)->load(); }
    float p (const juce::String& id) const { return params.getRawParameterValue (id)->load(); }
    void setParam (const char* id, float realValue);
    void setParam (const juce::String& id, float realValue) { setParam (id.toRawUTF8(), realValue); }

    LibraryStore store;
    Setlist songs;
    SampleLoader sampleLoader;
    PadChannel pad, padB, crowd[2];
    FxChain fx;
    CrowdFx crowdFx[2];
    CrowdPrefs prefs;
    std::atomic<bool> soloOn { false };
    std::atomic<double> hostBpm { 0 };
    std::atomic<float> crowdLvl[2] { { 0 }, { 0 } };
    std::atomic<int> target { 0 };
    juce::CriticalSection fxLock;
    CrowdPrefs::Fx lastFx[2];
    double tapTimes[8] {};
    int taps = 0;
    juce::AudioBuffer<float> mainBuf, subBuf, shimBuf, bBuf, bSub, bShim, crowdBuf, crowdBuf2;
    int maxBlock = 512;
    float lastGA = 1, lastGB = 0, lastMaster = 1;

    mutable juce::CriticalSection stateLock;
    juce::String padLibs[2], padLibsB[2];
    juce::String crowdLib[2], crowdName[2];
    int crowdKey[2] { -1, -1 }, crowdMode[2] { -1, -1 };
    std::atomic<int> crowdIdx[2] { { -1 }, { -1 } }, crowdSemis[2] { { 0 }, { 0 } };
    std::atomic<int> keyNow { 4 }, modeNow { 1 };
    std::atomic<bool> padOn { false };
    std::atomic<int> midiKey { -1 }, midiProgram { -1 };
    std::atomic<int> generation { 0 };
    std::atomic<float> level { 0 };
    std::atomic<bool> midiSeen { false };
    std::atomic<bool> lastLoop { true };
    juce::String padSoundNow;
    std::atomic<int> pendingLoads { 0 };

    // MIDI
    std::atomic<int> keyMode { 0 }, zoneLow { 36 };
    std::array<std::atomic<int>, actCount> midiMap;
    std::atomic<int> learnAction { -1 }, learnedCode { -1 }, learnedZone { -1 };
    std::atomic<bool> learnZoneFlag { false };
    std::atomic<juce::uint32> pendingActions { 0 };
    std::array<std::atomic<float>, 3> contValue {};
    std::atomic<juce::uint32> contFlags { 0 };
    std::array<int, 128> ccLast {};
    int lastSettingsVersion = -1;

    // visualiseur
    static constexpr int scopeSize = 4096;
    std::array<float, scopeSize> scope {};
    std::atomic<int> scopePos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacepadProcessor)
};
}
