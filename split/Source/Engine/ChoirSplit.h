// Layers Split — séparation d'un stem de chœur en pupitres : soprano, alto, ténor, basse.
//
// Principe : dans chaque trame, on détecte jusqu'à 4 notes chantées en même temps (réseau Basic Pitch). Les notes sont rangées de
// la plus aiguë à la plus grave et attribuées aux pupitres en respectant leur ordre et la continuité
// mélodique de chaque voix. Chaque pupitre reçoit les harmoniques de sa note (masque spectral doux) ;
// le reste (souffle, consonnes) est partagé entre les voix qui chantent. Somme des pupitres = chœur.
#pragma once

#include "MdxModel.h"

namespace lsplit
{

enum ChoirVoice { voiceSoprano = 0, voiceAlto, voiceTenor, voiceBass, numVoices };

const char* choirVoiceName (int v);

// out : numVoices pupitres. Faux si annulé.
// basicPitchModel : chemin du modèle « basic_pitch.onnx » (détection des notes).
bool splitChoir (const StereoBuffer& in, double sr, StereoBuffer out[numVoices],
                 const std::string& basicPitchModel, const std::function<bool (double)>& progress);

} // namespace lsplit
