#include "Lang.h"
#include "Library.h"

namespace spacepad
{
static std::atomic<int> current { -1 };
static std::atomic<int> version { 1 };
static juce::Time lastSeen;
static juce::CriticalSection langLock;
static UiSettings ui;
static bool uiLoaded = false;

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

// à appeler avec langLock tenu
static void loadSetting()
{
    auto f = settingsFile();
    lastSeen = f.getLastModificationTime();
    Lang l = systemLang();
    UiSettings u;
    if (f.existsAsFile())
    {
        auto v = juce::JSON::parse (f.loadFileAsString());
        l = fromCode (v["language"].toString(), l);
        if (auto* ui2 = v["ui"].getDynamicObject())
        {
            auto gi = [&] (const char* k, int def, int lo, int hi) { return ui2->hasProperty (k) ? juce::jlimit (lo, hi, (int) ui2->getProperty (k)) : def; };
            auto gf = [&] (const char* k, float def) { return ui2->hasProperty (k) ? juce::jlimit (0.0f, 1.0f, (float) (double) ui2->getProperty (k)) : def; };
            auto gb = [&] (const char* k, bool def) { return ui2->hasProperty (k) ? (bool) ui2->getProperty (k) : def; };
            u.accent = gi ("accent", 0, 0, 6);
            u.theme = gi ("theme", 0, 0, 3);
            u.background = gi ("background", 0, 0, 2);
            u.brightness = gf ("brightness", 0.5f);
            u.speed = gf ("speed", 0.5f);
            u.viz = gi ("viz", 0, 0, 3);
            u.solfegeNames = gb ("solfege", false);
            u.scale = gi ("scale", 100, 90, 150);
            u.animations = gb ("animations", true);
            u.keepAwake = gb ("keepAwake", true);
            u.midiKeyMode = gi ("midiKeyMode", 0, 0, 2);
            u.zoneLow = gi ("zoneLow", 36, 0, 116);
            u.fadeSeconds = ui2->hasProperty ("fadeSeconds") ? juce::jlimit (1.0f, 30.0f, (float) (double) ui2->getProperty ("fadeSeconds")) : 8.0f;
            u.copySounds = gb ("copySounds", false);
            u.centerView = juce::jlimit (0, 1, (int) ui2->getProperty ("centerView"));
            u.fxPage = juce::jlimit (0, 1, (int) ui2->getProperty ("fxPage"));
            u.crowdBg = juce::jlimit (0, 3, (int) ui2->getProperty ("crowdBg"));
            if (auto* arr = ui2->getProperty ("midiMap").getArray())
                for (int i = 0; i < (int) u.midiMap.size() && i < arr->size(); ++i) u.midiMap[(size_t) i] = (int) (*arr)[i];
        }
    }
    current = (int) l;
    ui = u;
    uiLoaded = true;
    ++version;
}

static void ensureLoaded()
{
    if (current.load() < 0 || ! uiLoaded)
    {
        const juce::ScopedLock sl (langLock);
        if (current.load() < 0 || ! uiLoaded) loadSetting();
    }
}

// à appeler avec langLock tenu
static void saveAll()
{
    auto f = settingsFile();
    auto v = f.existsAsFile() ? juce::JSON::parse (f.loadFileAsString()) : juce::var();
    if (! v.isObject()) v = juce::var (new juce::DynamicObject());
    static const char* codes[] = { "fr", "en", "es" };
    v.getDynamicObject()->setProperty ("language", codes[juce::jlimit (0, 2, current.load())]);
    auto* o = new juce::DynamicObject();
    o->setProperty ("accent", ui.accent);
    o->setProperty ("theme", ui.theme);
    o->setProperty ("background", ui.background);
    o->setProperty ("brightness", ui.brightness);
    o->setProperty ("speed", ui.speed);
    o->setProperty ("viz", ui.viz);
    o->setProperty ("solfege", ui.solfegeNames);
    o->setProperty ("scale", ui.scale);
    o->setProperty ("animations", ui.animations);
    o->setProperty ("keepAwake", ui.keepAwake);
    o->setProperty ("midiKeyMode", ui.midiKeyMode);
    o->setProperty ("zoneLow", ui.zoneLow);
    o->setProperty ("fadeSeconds", ui.fadeSeconds);
    o->setProperty ("copySounds", ui.copySounds);
    o->setProperty ("centerView", ui.centerView);
    o->setProperty ("fxPage", ui.fxPage);
    o->setProperty ("crowdBg", ui.crowdBg);
    juce::Array<juce::var> mm;
    for (auto v : ui.midiMap) mm.add (v);
    o->setProperty ("midiMap", mm);
    v.getDynamicObject()->setProperty ("ui", juce::var (o));
    f.replaceWithText (juce::JSON::toString (v));
    lastSeen = f.getLastModificationTime();
    ++version;
}

Lang lang()
{
    ensureLoaded();
    return (Lang) current.load();
}

void setLang (Lang l)
{
    ensureLoaded();
    const juce::ScopedLock sl (langLock);
    current = (int) l;
    saveAll();
}

UiSettings uiSettings()
{
    ensureLoaded();
    const juce::ScopedLock sl (langLock);
    return ui;
}

void setUiSettings (const UiSettings& u)
{
    ensureLoaded();
    const juce::ScopedLock sl (langLock);
    ui = u;
    saveAll();
}

int settingsVersion() { return version.load(); }

bool reloadLangIfChanged()
{
    const juce::ScopedLock sl (langLock);
    auto f = settingsFile();
    if (! f.existsAsFile() || f.getLastModificationTime() == lastSeen) return false;
    loadSetting();
    return true;
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

static const char* const solFr[] = { "Do", "Do#", "R\xc3\xa9", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
static const char* const solEs[] = { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
static const char* const letters[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

juce::String noteName (int k)
{
    if (k < 0 || k > 11) return "-";
    const auto l = lang();
    return juce::String::fromUTF8 (l == Lang::EN ? letters[k] : l == Lang::ES ? solEs[k] : solFr[k]);
}

juce::String solfege (int k)
{
    if (k < 0 || k > 11) return "-";
    return juce::String::fromUTF8 (lang() == Lang::ES ? solEs[k] : solFr[k]);
}

juce::String modeWord (int mode)
{
    return mode == 1 ? L ("mineur", "minor", "menor") : L ("majeur", "major", "mayor");
}
}

namespace spacepad
{
juce::String midiActionName (int a)
{
    switch (a)
    {
        case actPlayStop:    return L ("Jouer / arr\xc3\xaater le pad", "Play / stop the pad", "Tocar / detener el pad");
        case actNextSong:    return L ("Chant suivant", "Next song", "Canci\xc3\xb3n siguiente");
        case actPrevSong:    return L ("Chant pr\xc3\xa9""c\xc3\xa9""dent", "Previous song", "Canci\xc3\xb3n anterior");
        case actModeToggle:  return L ("Majeur \xe2\x87\x84 mineur (crochet)", "Major \xe2\x87\x84 minor (hook)", "Mayor \xe2\x87\x84 menor (gancho)");
        case actFreeze:      return "Freeze";
        case actSwell:       return "Swell";
        case actFadeEnd:     return L ("Fin du chant", "End of song", "Fin de la canci\xc3\xb3n");
        case actStopAll:     return L ("Tout arr\xc3\xaater", "Stop all", "Detener todo");
        case actPadVolume:   return L ("Volume du pad", "Pad volume", "Volumen del pad");
        case actCrowdVolume: return L ("Volume des crowds", "Crowds volume", "Volumen de los crowds");
        case actMix:         return L ("Dosage pad / crowd", "Pad / crowd balance", "Balance pad / crowd");
        default:             return {};
    }
}

juce::String midiCodeName (int code)
{
    static const char* n[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (code >= 2000) { const int note = code - 2000; return L ("Note ", "Note ", "Nota ") + juce::String (n[note % 12]) + juce::String (note / 12 - 1) + " (" + juce::String (note) + ")"; }
    if (code >= 1000) return "CC " + juce::String (code - 1000);
    return L ("Non assign\xc3\xa9", "Not assigned", "Sin asignar");
}
}
