// Layers Split — travail en arrière-plan : ouverture d'un morceau, séparation, analyse, cache.
#pragma once

#include <juce_events/juce_events.h>

#include "AudioIO.h"

#include <atomic>
#include <functional>

namespace lsplit
{

class SeparationJob : private juce::Thread
{
public:
    enum class Kind
    {
        open,       // décoder le fichier (et reprendre les stems du cache s'ils existent)
        separate    // séparer (ou reprendre du cache)
    };

    struct Request
    {
        Kind kind = Kind::open;
        juce::File source;
        SeparationSettings settings;
        PlayDataPtr existing;   // morceau déjà décodé (évite de relire le fichier)
        bool useCache = true;
    };

    struct Result
    {
        bool ok = false;
        bool cancelled = false;
        juce::String error;
        juce::String info;
        PlayDataPtr data;
    };

    using Callback = std::function<void (const Result&)>; // appelé sur le fil des messages

    SeparationJob();
    ~SeparationJob() override;

    // Faux si un travail est déjà en cours.
    bool start (const Request& r, Callback onDone);
    void cancel();
    bool isRunning() const { return isThreadRunning(); }

    double progress() const { return prog.load(); }
    juce::String stepText() const;
    double elapsedSeconds() const;

private:
    void run() override;
    Result doWork();
    void setStep (const juce::String& s, double p);

    Request req;
    Callback callback;
    std::atomic<double> prog { 0.0 };
    std::atomic<double> startMs { 0.0 };
    juce::CriticalSection stepLock;
    juce::String step;

    struct Alive { std::atomic<bool> alive { true }; };
    std::shared_ptr<Alive> alive = std::make_shared<Alive>();
};

} // namespace lsplit
