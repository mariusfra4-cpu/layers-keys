// Spacepad by JB Praise — le cœur du logiciel (application, VST3 et Audio Unit partagent ce code).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Library.h"
#include "PadEngine.h"
#include "Effects.h"
#include "Setlist.h"
#include "CrowdPrefs.h"
#include "Modulation.h"
#include "Licence.h"
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

    // --- licence ---
    Licence& licence() { return lic; }
    Licence::State licenceState() const { return (Licence::State) licState.load(); }
    double demoSecondsLeft() const { return demoLeft.load(); }
    void refreshLicence();                   // relit l'état (après activation, démo…)
    void forceLicenceStateForTests (Licence::State s, double demoSecs = 0) { licState = (int) s; demoLeft = demoSecs; }

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
    // Enregistrement automatique avec le logiciel de musique : pendant que le DAW enregistre, chaque action est
    // gardée à sa place dans le morceau (position en noires) ; quand le DAW joue, Spacepad la rejoue au bon moment.
    struct RecEvent { double ppq = 0; int type = 0, a = 0, b = 0; juce::String lib; };   // type : 0 tonalité, 1 mode, 2 pad, 3 chant, 4 crowd
    enum { evKey = 0, evMode, evPlay, evSong, evCrowd };
    std::vector<RecEvent> recording() const;
    int recordingCount() const;
    int takeCount() const;                   // actions de la prise en cours
    void clearRecording (bool keepKeysOnly = false);
    bool dawRecordingNow() const { return recActive.load(); }
    bool dawReplayingNow() const { return replayActive.load(); }
    double beatsPerBar() const { return hostBeatsPerBar.load(); }
    static juce::String describeEvent (const RecEvent&, const Setlist&, const LibraryStore&);
    void setHostPositionForTests (bool playing, bool recording, double ppq) { testHost = true; hostPlaying = playing; hostRecording = recording; hostPpq = ppq; }

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
    void preloadLikelySounds();              // charge d'avance les sons de la tonalité en cours (Play instantané)
    juce::String lastPreloadSig;
    void pushAuto();
    void dawFollow();                        // fil de commande : début/fin d'enregistrement, lecture des actions
    void recordEvent (int type, int a, int b = 0, const juce::String& lib = {});
    void applyEvent (const RecEvent&);
    void snapshotEvents (double ppq, std::vector<RecEvent>& out);
    mutable juce::CriticalSection recLock;
    std::vector<RecEvent> recEvents, takeEvents;
    double takeStart = 0, lastPlayPpq = -1;
    bool wasPlaying = false, wasRecording = false;
    std::atomic<bool> recActive { false }, replayActive { false }, testHost { false };
    std::atomic<bool> hostPlaying { false }, hostRecording { false }, replaying { false };
    std::atomic<double> hostPpq { 0 }, hostBeatsPerBar { 4 };                         // l'état actuel → paramètres (le logiciel de musique l'enregistre)
    void pullAuto();                         // paramètres changés par le logiciel de musique (lecture de l'automation) → actions
    void setAuto (const char* id, float realValue);
    juce::CriticalSection autoLock;
    int autoDepth = 0, lastSongParam = 0;
    // dernier état envoyé au logiciel de musique : on n'envoie que ce que Spacepad a changé lui-même,
    // pour ne jamais écraser une valeur que le logiciel de musique vient d'écrire (lecture de l'automation)
    int sentKey = -999, sentMode = -999, sentPlay = -999, sentSong = -999, sentOn[2] { -999, -999 }, sentSnd[2] { -999, -999 };
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
    Licence lic;
   #if SPACEPAD_TESTS
    std::atomic<int> licState { (int) Licence::State::Licensed };   // programme de tests : déjà activé (sauf tests de licence)
   #else
    std::atomic<int> licState { (int) Licence::State::Locked };
   #endif
    std::atomic<double> demoLeft { 0 };
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
