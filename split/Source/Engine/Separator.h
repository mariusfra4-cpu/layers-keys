// Layers Split — chaîne complète de séparation.
//
//   mix ──► Demucs 6 sources : batterie, basse, guitares (→ solo / rythmique), piano + repérage du chant
//    └──► voix : Rapide = voix de Demucs ; Haute = MDX Kim Vocal 2 sur les passages chantés seulement ;
//         Ultra = Kim Vocal 2 + Inst HQ 3 complets, double passage
//         ──► voix lead / chœurs (MDX Karaoké 2, passages chantés seulement) ──► pupitres du chœur
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

enum StemId { stemLead = 0, stemBacking, stemDrums, stemBass, stemGuitar, stemGuitarSolo, stemPiano, stemOther,
              // pupitres du chœur : une décomposition de « Chœurs » (leur somme = Chœurs), pas des stems en plus
              stemSoprano, stemAlto, stemTenor, stemBassVoice, numStems };

inline bool isChoirVoice (int id) { return id >= stemSoprano && id <= stemBassVoice; }

const char* stemName (int id);      // nom français affiché
const char* stemFileName (int id);  // nom de fichier (sans accents)

struct SeparationSettings
{
    Quality quality = Quality::high;
    bool useGpu = true;
    bool splitVocals = true;   // voix lead / chœurs
    bool splitDrumsBass = true;
    bool detailInstruments = true; // guitares et piano (modèle Demucs 6 sources)
    bool splitGuitarSolo = false;  // guitare solo / rythmique (expérimental)
    bool lowMemory = false;        // ordinateur avec peu de mémoire : moins de réserve, pas de résultat provisoire
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

    // Vérifie que les fichiers de modèles nécessaires à ces réglages sont présents.
    bool checkModels (std::string& missing, const SeparationSettings& s) const;

    // Résultat provisoire (instruments prêts, voix pas encore affinée), à 44,1 kHz.
    using Partial = std::function<void (const StemSet&)>;

    // Faux si annulé. Lance std::exception en cas d'erreur.
    bool run (const StereoBuffer& mix, double sampleRate, const SeparationSettings& settings,
              StemSet& result, const Progress& progress, const Partial& onPartial = nullptr);

    // Mesures carte graphique / processeur faites pendant cette session (diagnostic).
    static std::string deviceReport();

    static constexpr double modelRate = 44100.0;

private:
    bool runModel (const char* file, const MdxParams& params, const StereoBuffer& in, StereoBuffer& out,
                   bool denoise, bool gpu, const std::string& label, double from, double to,
                   const Progress& progress, std::string& deviceUsed, const std::vector<char>* active = nullptr);
    bool runDemucs (const StereoBuffer& in, std::vector<StereoBuffer>& out, std::vector<std::string>& names,
                    bool gpu, double overlap, double from, double to, const Progress& progress, std::string& deviceUsed,
                    bool keepArena);

    std::string modelsDir;
};

} // namespace lsplit
