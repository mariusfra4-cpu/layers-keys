#include "Setlist.h"
#include "Library.h"

namespace spacepad
{
Setlist::Setlist() { load(); }

juce::File Setlist::file() { return LibraryStore::storageFolder().getChildFile ("setlist.json"); }

bool Setlist::load()
{
    const juce::ScopedLock sl (lock);
    auto f = file();
    lastSeen = f.getLastModificationTime();
    list.clear();
    ++changes;
    if (! f.existsAsFile()) return false;
    auto v = juce::JSON::parse (f.loadFileAsString());
    setName = v["name"].toString();
    currentIndex = (int) v.getProperty ("current", -1);
    if (auto* arr = v["songs"].getArray())
        for (auto& sv : *arr)
        {
            Song s;
            s.name = sv["name"].toString();
            s.key = juce::jlimit (0, 11, (int) sv.getProperty ("key", 4));
            s.mode = (int) sv.getProperty ("mode", 1) == 1 ? 1 : 0;
            for (int m = 0; m < 2; ++m)
            {
                s.padA[m] = sv[m == 0 ? "padAMajor" : "padAMinor"].toString();
                s.padB[m] = sv[m == 0 ? "padBMajor" : "padBMinor"].toString();
                s.crowdLib[m] = sv[m == 0 ? "crowd1Lib" : "crowd2Lib"].toString();
                s.crowdIndex[m] = (int) sv.getProperty (m == 0 ? "crowd1" : "crowd2", -1);
            }
            if (auto* po = sv["params"].getDynamicObject())
                for (auto& p : po->getProperties()) s.params[p.name.toString()] = (float) (double) p.value;
            list.push_back (s);
        }
    if (currentIndex.load() >= (int) list.size()) currentIndex = (int) list.size() - 1;
    return true;
}

bool Setlist::save()
{
    const juce::ScopedLock sl (lock);
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", setName);
    root->setProperty ("current", currentIndex.load());
    juce::Array<juce::var> arr;
    for (auto& s : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", s.name);
        o->setProperty ("key", s.key);
        o->setProperty ("mode", s.mode);
        o->setProperty ("padAMajor", s.padA[0]); o->setProperty ("padAMinor", s.padA[1]);
        o->setProperty ("padBMajor", s.padB[0]); o->setProperty ("padBMinor", s.padB[1]);
        o->setProperty ("crowd1Lib", s.crowdLib[0]); o->setProperty ("crowd1", s.crowdIndex[0]);
        o->setProperty ("crowd2Lib", s.crowdLib[1]); o->setProperty ("crowd2", s.crowdIndex[1]);
        auto* po = new juce::DynamicObject();
        for (auto& kv : s.params) po->setProperty (kv.first, kv.second);
        o->setProperty ("params", juce::var (po));
        arr.add (juce::var (o));
    }
    root->setProperty ("songs", arr);
    auto f = file();
    if (! writeSharedFile (f, juce::JSON::toString (juce::var (root)))) { dirty = true; return false; }
    dirty = false;
    lastSeen = f.getLastModificationTime();
    ++changes;
    return true;
}

bool Setlist::reloadIfChanged()
{
    {
        const juce::ScopedLock sl (lock);
        if (dirty) { save(); return false; }     // nos changements pas encore écrits : on ne relit pas l'ancien fichier
        if (file().getLastModificationTime() == lastSeen) return false;
    }
    return load();
}

std::vector<Song> Setlist::songs() const { const juce::ScopedLock sl (lock); return list; }
juce::String Setlist::name() const { const juce::ScopedLock sl (lock); return setName; }

void Setlist::setCurrent (int i)
{
    { const juce::ScopedLock sl (lock); currentIndex = juce::jlimit (-1, (int) list.size() - 1, i); }
    save();
}
void Setlist::add (const Song& s) { { const juce::ScopedLock sl (lock); list.push_back (s); currentIndex = (int) list.size() - 1; } save(); }
void Setlist::update (int i, const Song& s) { { const juce::ScopedLock sl (lock); if (i >= 0 && i < (int) list.size()) { auto n = list[(size_t) i].name; list[(size_t) i] = s; list[(size_t) i].name = n; } } save(); }
void Setlist::remove (int i)
{
    {
        const juce::ScopedLock sl (lock);
        if (i < 0 || i >= (int) list.size()) return;
        list.erase (list.begin() + i);
        if (currentIndex.load() >= (int) list.size()) currentIndex = (int) list.size() - 1;
    }
    save();
}
void Setlist::move (int i, int d)
{
    {
        const juce::ScopedLock sl (lock);
        const int j = i + d;
        if (i < 0 || j < 0 || i >= (int) list.size() || j >= (int) list.size()) return;
        std::swap (list[(size_t) i], list[(size_t) j]);
        if (currentIndex.load() == i) currentIndex = j; else if (currentIndex.load() == j) currentIndex = i;
    }
    save();
}
void Setlist::rename (int i, const juce::String& n) { { const juce::ScopedLock sl (lock); if (i >= 0 && i < (int) list.size()) list[(size_t) i].name = n; } save(); }
}
