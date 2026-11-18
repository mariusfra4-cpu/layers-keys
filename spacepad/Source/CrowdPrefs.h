// Spacepad by JB Praise — préférences des crowds : favoris, derniers joués, et effets réglés pour chaque son.
// Fichier crowds.json du dossier commun : les mêmes dans l'application et le plugin.
#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <set>

namespace spacepad
{
class CrowdPrefs
{
public:
    using Fx = std::map<juce::String, float>;
    static juce::String keyFor (const juce::String& libId, const juce::String& soundPath) { return libId + "|" + soundPath; }
    static juce::File file();
    void load();
    bool saveIfDirty();
    bool reloadIfChanged();

    bool isFavourite (const juce::String& key) const;
    void setFavourite (const juce::String& key, bool on);
    int favouriteCount() const;
    juce::int64 lastPlayed (const juce::String& key) const;    // 0 = jamais
    void markPlayed (const juce::String& key);
    bool hasFx (const juce::String& key) const;
    Fx fx (const juce::String& key) const;
    void setFx (const juce::String& key, const Fx& values);
    void clearFx (const juce::String& key);
    int changeCount() const { return changes.load(); }

private:
    mutable juce::CriticalSection lock;
    std::set<juce::String> favs;
    std::map<juce::String, juce::int64> played;
    std::map<juce::String, Fx> fxs;
    bool dirty = false;
    std::atomic<int> changes { 0 };
    juce::Time lastSeen;
    void touch() { dirty = true; ++changes; }
};
}
