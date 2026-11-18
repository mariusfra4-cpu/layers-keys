// Spacepad by JB Praise — la liste des chants (setlist) : pour chaque chant, la tonalité, les pads, les crowds et
// les effets. Fichier setlist.json du dossier commun : la même liste dans l'application et le plugin.
#pragma once
#include <juce_core/juce_core.h>
#include <map>

namespace spacepad
{
struct Song
{
    juce::String name;
    int key = 4, mode = 1;
    juce::String padA[2], padB[2];               // librairies (majeur, mineur) ; B = second pad mélangé (facultatif)
    juce::String crowdLib[2];                     // crowds 1 et 2
    int crowdIndex[2] { -1, -1 };
    std::map<juce::String, float> params;         // effets, volumes, dosages (valeurs réelles)
};

class Setlist
{
public:
    Setlist();
    static juce::File file();
    bool load();
    bool save();
    bool reloadIfChanged();

    std::vector<Song> songs() const;
    int current() const { return currentIndex.load(); }
    void setCurrent (int i);
    void add (const Song& s);
    void update (int i, const Song& s);
    void remove (int i);
    void move (int i, int delta);
    void rename (int i, const juce::String& name);
    juce::String name() const;
    int changeCount() const { return changes.load(); }

private:
    mutable juce::CriticalSection lock;
    std::vector<Song> list;
    juce::String setName;
    std::atomic<int> currentIndex { -1 }, changes { 0 };
    juce::Time lastSeen;
};
}
