#include "Lang.h"
#include "Library.h"

namespace spacepad
{
static std::atomic<int> current { -1 };
static juce::Time lastSeen;
static juce::CriticalSection langLock;

static juce::File settingsFile() { return LibraryStore::storageFolder().getChildFile ("settings.json"); }

static Lang systemLang()
{
    auto l = juce::SystemStats::getUserLanguage().toLowerCase();
    if (l.startsWith ("fr")) return Lang::FR;
    if (l.startsWith ("es")) return Lang::ES;
    return Lang::EN;
}

static Lang fromCode (const juce::String& c, Lang fallback)
{
    if (c == "fr") return Lang::FR;
    if (c == "en") return Lang::EN;
    if (c == "es") return Lang::ES;
    return fallback;
}

static void loadSetting()
{
    auto f = settingsFile();
    lastSeen = f.getLastModificationTime();
    Lang l = systemLang();
    if (f.existsAsFile())
        l = fromCode (juce::JSON::parse (f.loadFileAsString())["language"].toString(), l);
    current = (int) l;
}

Lang lang()
{
    if (current.load() < 0)
    {
        const juce::ScopedLock sl (langLock);
        if (current.load() < 0) loadSetting();
    }
    return (Lang) current.load();
}

void setLang (Lang l)
{
    const juce::ScopedLock sl (langLock);
    current = (int) l;
    auto f = settingsFile();
    auto v = f.existsAsFile() ? juce::JSON::parse (f.loadFileAsString()) : juce::var();
    if (! v.isObject()) v = juce::var (new juce::DynamicObject());
    static const char* codes[] = { "fr", "en", "es" };
    v.getDynamicObject()->setProperty ("language", codes[(int) l]);
    f.replaceWithText (juce::JSON::toString (v));
    lastSeen = f.getLastModificationTime();
}

bool reloadLangIfChanged()
{
    const juce::ScopedLock sl (langLock);
    auto f = settingsFile();
    if (! f.existsAsFile() || f.getLastModificationTime() == lastSeen) return false;
    const int before = current.load();
    loadSetting();
    return before != current.load();
}

juce::String L (const char* fr, const char* en, const char* es)
{
    switch (lang())
    {
        case Lang::EN: return juce::String::fromUTF8 (en);
        case Lang::ES: return juce::String::fromUTF8 (es);
        default:       return juce::String::fromUTF8 (fr);
    }
}

juce::String noteName (int k)
{
    if (k < 0 || k > 11) return "-";
    static const char* fr[] = { "Do", "Do#", "R\xc3\xa9", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    static const char* en[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* es[] = { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    const auto l = lang();
    return juce::String::fromUTF8 (l == Lang::EN ? en[k] : l == Lang::ES ? es[k] : fr[k]);
}

juce::String modeWord (int mode)
{
    return mode == 1 ? L ("mineur", "minor", "menor") : L ("majeur", "major", "mayor");
}
}
