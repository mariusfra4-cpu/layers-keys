// Layers Split — chaîne complète de séparation.
//
//   mix ──► voix (MDX Kim Vocal 2 [+ Inst HQ 3 en Ultra]) ──► voix lead / chœurs (MDX Karaoké 2)
//    └───► instrumental = mix − voix ──► batterie (kuielab), basse (kuielab), autres = reste
//
// Chaque stem « reste » est calculé par soustraction : la somme des stems redonne
// exactement le mix original, à l'échantillon près.
#pragma once

#include "MdxModel.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace lsplit
{

enum class Quality { fast = 0, high = 1, ultra = 2 };

enum StemId { stemLead = 0, stemBacking, stemDrums, stemBass, stemOther, numStems };

const char* stemName (int id);      // nom français affiché
const char* stemFileName (int id);  // nom de fichier (sans accents)

struct SeparationSettings
{
    Quality quality = Quality::high;
    bool useGpu = true;
    bool splitVocals = true;   // voix lead / chœurs
    bool splitDrumsBass = true;
};

struct StemSet
{
    double sampleRate = 44100.0;
    StereoBuffer stems[numStems];
    bool present[numStems] {};
    std::string deviceUsed; // « GPU (DirectML) » ou « Processeur »
    size_t numSamples() const;
};

class Separator
{
public:
    // texte de l'étape en cours, progression globale [0,1]. Retourne faux pour annuler.
    using Progress = std::function<bool (const std::string&, double)>;

    explicit Separator (std::string modelsDirUtf8);

    // Vérifie que tous les fichiers de modèles sont présents.
    bool checkModels (std::string& missing) const;

    // Faux si annulé. Lance std::exception en cas d'erreur.
    bool run (const StereoBuffer& mix, double sampleRate, const SeparationSettings& settings,
              StemSet& result, const Progress& progress);

    static constexpr double modelRate = 44100.0;

private:
    bool runModel (const char* file, const MdxParams& params, const StereoBuffer& in, StereoBuffer& out,
                   bool denoise, bool gpu, const std::string& label, double from, double to,
                   const Progress& progress, std::string& deviceUsed);

    std::string modelsDir;
};

} // namespace lsplit
