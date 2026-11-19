#pragma once

#include <cstdint>
#include <memory>

// Ableton Link : African Loops se synchronise avec Ableton Live et les autres
// logiciels / applications Link du réseau (même tempo, mêmes barres de mesure,
// départ et arrêt ensemble). Le code de Link est isolé dans LinkSync.cpp.
class LinkSync {
public:
    LinkSync();
    ~LinkSync();

    // --- depuis l'interface (jamais depuis le fil audio)
    void setEnabled(bool on);
    bool isEnabled() const;
    int peers() const;           // nombre d'autres logiciels connectés
    void setTempo(double bpm);   // tout le monde passe à ce tempo

    // --- depuis le fil audio (sans attente)
    struct State {
        double tempo = 120;
        double beat = 0;   // position en temps (négative avant un départ demandé)
        bool playing = false;
    };
    // quantum = nombre de temps par mesure (les barres de mesure sont communes)
    State audioState(double quantum, std::int64_t latencyMicros) const;
    // Lecture / arrêt pour tout le monde ; le départ se fait sur la prochaine mesure.
    void audioRequestPlay(bool play, double quantum, std::int64_t latencyMicros);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
