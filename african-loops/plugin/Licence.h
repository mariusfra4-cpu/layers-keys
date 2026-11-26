#pragma once
// Licence d'African Loops Pro : une clé = un ordinateur, sans Internet, démo de 3 jours.
// L'état est commun à l'ordinateur : la version seule et le VST3 / AU partagent la même licence.

#include <juce_core/juce_core.h>

#include <atomic>
#include <mutex>

class Licence {
public:
    enum class State { Locked, Demo, Licensed };
    struct Status {
        State state = State::Locked;
        juce::int64 demoLeftSeconds = 0;
        bool demoUsed = false;  // la démo a déjà été lancée (et elle est finie si Locked)
        bool tamper = false;    // fichiers de démo modifiés à la main
    };

    static Licence& get();

    juce::String code();                     // JBA-XXXX-XXXX-XXXX-XXXX
    Status status();                         // relit l'état (fil de l'interface)
    juce::String activate(const juce::String& key);  // "" si c'est bon, sinon le message d'erreur (en français)
    void startDemo();
    bool canPlay() const { return playing.load(std::memory_order_relaxed); }  // pour le fil audio
    static juce::String formatLeft(juce::int64 seconds);  // « 2 j 14 h »

    // --- tests seulement
    void useFolderForTests(const juce::File& dir, const juce::String& machineId, std::function<juce::int64()> now = {});
    void forceForTests(bool licensed);

    static constexpr juce::int64 kDemoSeconds = 3 * 24 * 3600;

private:
    Licence();
    juce::String machineId();
    juce::Array<juce::File> dirs() const;
    bool readLicence();
    struct Demo {
        juce::int64 start = 0, last = 0;
        bool ok = false;
    };
    bool readDemo(const juce::File& dir, Demo& d);
    void writeDemo(juce::int64 start, juce::int64 last);
    juce::String demoMac(juce::int64 start, juce::int64 last);
    juce::int64 now() const;

    std::mutex mu;
    juce::String cachedCode, testMachine;
    juce::File testDir;
    std::function<juce::int64()> testNow;
    bool licensed = false, checked = false;
    int forced = -1;
    std::atomic<bool> playing{false};
};
