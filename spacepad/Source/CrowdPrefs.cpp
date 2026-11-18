#include "CrowdPrefs.h"
#include "Library.h"

namespace spacepad
{
juce::File CrowdPrefs::file() { return LibraryStore::storageFolder().getChildFile ("crowds.json"); }

void CrowdPrefs::load()
{
    auto f = file();
    auto v = juce::JSON::parse (f.loadFileAsString());
    const juce::ScopedLock sl (lock);
    favs.clear(); played.clear(); fxs.clear();
    if (auto* a = v["favourites"].getArray()) for (auto& x : *a) favs.insert (x.toString());
    if (auto* o = v["played"].getDynamicObject()) for (auto& kv : o->getProperties()) played[kv.name.toString()] = (juce::int64) kv.value;
    if (auto* o = v["fx"].getDynamicObject())
        for (auto& kv : o->getProperties())
            if (auto* po = kv.value.getDynamicObject())
            {
                Fx m;
                for (auto& pv : po->getProperties()) m[pv.name.toString()] = (float) (double) pv.value;
                fxs[kv.name.toString()] = m;
            }
    lastSeen = f.getLastModificationTime();
    dirty = false;
    ++changes;
}

bool CrowdPrefs::saveIfDirty()
{
    juce::var root;
    {
        const juce::ScopedLock sl (lock);
        if (! dirty) return false;
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> fa;
        for (auto& k : favs) fa.add (k);
        o->setProperty ("favourites", fa);
        auto* po = new juce::DynamicObject();
        // on garde les 300 derniers sons joués
        std::vector<std::pair<juce::int64, juce::String>> recent;
        for (auto& kv : played) recent.push_back ({ kv.second, kv.first });
        std::sort (recent.rbegin(), recent.rend());
        for (size_t i = 0; i < recent.size() && i < 300; ++i) po->setProperty (recent[i].second, recent[i].first);
        o->setProperty ("played", juce::var (po));
        auto* xo = new juce::DynamicObject();
        for (auto& kv : fxs)
        {
            auto* e = new juce::DynamicObject();
            for (auto& pv : kv.second) e->setProperty (pv.first, pv.second);
            xo->setProperty (kv.first, juce::var (e));
        }
        o->setProperty ("fx", juce::var (xo));
        root = juce::var (o);
        dirty = false;
    }
    auto f = file();
    f.getParentDirectory().createDirectory();
    const bool ok = f.replaceWithText (juce::JSON::toString (root));
    const juce::ScopedLock sl (lock);
    lastSeen = f.getLastModificationTime();
    return ok;
}

bool CrowdPrefs::reloadIfChanged()
{
    auto f = file();
    {
        const juce::ScopedLock sl (lock);
        if (dirty || ! f.existsAsFile() || f.getLastModificationTime() == lastSeen) return false;
    }
    load();
    return true;
}

bool CrowdPrefs::isFavourite (const juce::String& k) const { const juce::ScopedLock sl (lock); return favs.count (k) > 0; }
int CrowdPrefs::favouriteCount() const { const juce::ScopedLock sl (lock); return (int) favs.size(); }
void CrowdPrefs::setFavourite (const juce::String& k, bool on)
{
    const juce::ScopedLock sl (lock);
    if (on) favs.insert (k); else favs.erase (k);
    touch();
}
juce::int64 CrowdPrefs::lastPlayed (const juce::String& k) const { const juce::ScopedLock sl (lock); auto it = played.find (k); return it == played.end() ? 0 : it->second; }
void CrowdPrefs::markPlayed (const juce::String& k)
{
    const juce::ScopedLock sl (lock);
    played[k] = juce::Time::currentTimeMillis();
    touch();
}
bool CrowdPrefs::hasFx (const juce::String& k) const { const juce::ScopedLock sl (lock); return fxs.count (k) > 0; }
CrowdPrefs::Fx CrowdPrefs::fx (const juce::String& k) const { const juce::ScopedLock sl (lock); auto it = fxs.find (k); return it == fxs.end() ? Fx() : it->second; }
void CrowdPrefs::setFx (const juce::String& k, const Fx& v) { const juce::ScopedLock sl (lock); fxs[k] = v; touch(); }
void CrowdPrefs::clearFx (const juce::String& k) { const juce::ScopedLock sl (lock); fxs.erase (k); touch(); }
}
