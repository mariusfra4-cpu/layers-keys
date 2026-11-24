#pragma once

#include <juce_core/juce_core.h>

// Langue de l'interface : français, anglais, espagnol, portugais.
// L'interface regarde current() et se redessine quand elle change.
// Le choix est gardé sur l'ordinateur (même réglage pour le VST3 et la version seule).
// Les textes du code sont écrits en français ; tr() donne la traduction.
namespace lang {

enum Id { Fr = 0, En, Es, Pt, Count };

Id current();
void set(Id l);
juce::String nativeName(Id l);  // « Français », « English »…

// Traduction d'un texte français (rendu tel quel s'il n'est pas dans la table).
juce::String tr(const juce::String& french);
// Pour les tests : nombre de textes, et textes sans traduction.
int tableSize();
juce::StringArray missing();

}  // namespace lang

#define TR(s) lang::tr(juce::String::fromUTF8(s))
