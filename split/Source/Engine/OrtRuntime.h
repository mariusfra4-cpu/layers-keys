// Layers Split — chargement dynamique d'ONNX Runtime.
// La bibliothèque est chargée par son chemin complet (jamais par le chemin de recherche
// de Windows) : un hôte VST ou Windows lui-même (System32) peut contenir une autre
// version d'onnxruntime.dll, qui ne doit jamais être utilisée par erreur.
#pragma once

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

#include <memory>
#include <mutex>
#include <string>

namespace lsplit
{

class OrtRuntime
{
public:
    // Charge onnxruntime (et DirectML.dll sous Windows) depuis le dossier indiqué.
    // Peut être appelé plusieurs fois : seul le premier appel réussi compte.
    static bool load (const std::string& runtimeDirUtf8, std::string& error);
    static bool isLoaded();

    // Environnement partagé par toutes les sessions.
    static Ort::Env& env();

    // Ajoute le fournisseur DirectML (carte graphique) aux options. Faux si indisponible.
    static bool appendDirectML (Ort::SessionOptions& options, std::string& error);
    static bool directMLAvailable();

    static std::string version();

    // Réglages communs : un cœur laissé libre pour l'interface et le son, pas d'attente active.
    // keepArena : garde la réserve mémoire interne (plus rapide pour Demucs, plus gourmand).
    static void configure (Ort::SessionOptions& options, bool keepArena = false);

private:
    static std::mutex& mutex();
};

// Conversion UTF-8 -> chemin natif attendu par ONNX Runtime (wchar_t sous Windows).
std::basic_string<ORTCHAR_T> toOrtPath (const std::string& utf8);

} // namespace lsplit
