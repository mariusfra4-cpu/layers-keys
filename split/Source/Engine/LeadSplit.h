// Layers Split — séparation « solo / accompagnement » d'un stem d'instrument (guitare solo / rythmique).
//
// Principe : un solo joue une seule note à la fois et domine l'accompagnement. Pour chaque trame,
// on cherche la fréquence fondamentale la plus saillante (somme des harmoniques), on la suit dans
// le temps, puis on extrait ses harmoniques par un masque spectral doux. Là où aucune mélodie ne
// domine, rien n'est extrait. Accompagnement = stem − solo (somme exacte).
#pragma once

#include "MdxModel.h"

namespace lsplit
{

struct LeadSplitSettings
{
    double minHz = 140.0;      // la guitare solo joue rarement plus grave
    double maxHz = 1400.0;
    double threshold = 0.48;   // part d'énergie captée par la mélodie pour la considérer comme un solo
};

// Faux si annulé. sr = 44100 attendu (tout autre taux fonctionne aussi).
bool splitLead (const StereoBuffer& in, double sr, StereoBuffer& lead,
                const std::function<bool (double)>& progress, const LeadSplitSettings& s = {});

} // namespace lsplit
