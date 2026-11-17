#include "Library.h"
#include "Lang.h"
#include <map>
#include <set>
#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

namespace spacepad
{
// ---------------------------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------------------------
int Library::numSounds() const
{
    int n = 0;
    for (auto& c : categories) n += (int) c.sounds.size();
    return n;
}

const Sound* Library::sound (int index) const
{
    if (index < 0) return nullptr;
    for (auto& c : categories)
    {
        if (index < (int) c.sounds.size()) return &c.sounds[(size_t) index];
        index -= (int) c.sounds.size();
    }
    return nullptr;
}

Library::Slot Library::slotFor (int key, int mode) const
{
    Slot best;
    if (key < 0) return best;
    mode = mode == 1 ? 1 : 0;
    int bestCost = 1 << 20;
    const int n = numSounds();
    for (int i = 0; i < n; ++i)
    {
        auto* s = sound (i);
        int sk = s->key >= 0 ? s->key : 0;            // son sans tonalité : pris comme un Do
        int sm = s->mode == 1 ? 1 : 0;
        int diff = ((key - sk) % 12 + 12) % 12;       // 0..11 vers le haut
        int semis = diff > 6 ? diff - 12 : diff;      // -5..+6 : la transposition la plus courte
        // coût : même mode d'abord, puis la plus petite transposition (on préfère descendre)
        int cost = (sm == mode ? 0 : 1000) + (s->key < 0 ? 500 : 0) + std::abs (semis) * 10 + (semis > 0 ? 1 : 0);
        if (cost < bestCost) { bestCost = cost; best.sound = i; best.semitones = semis; best.exact = (semis == 0 && sm == mode && s->key >= 0); }
    }
    return best;
}

int Library::exactSlots (int mode) const
{
    int n = 0;
    for (int k = 0; k < 12; ++k) if (slotFor (k, mode).exact) ++n;
    return n;
}

int Library::countMode (int mode) const
{
    int n = 0;
    for (auto& c : categories)
        for (auto& s : c.sounds)
            if ((s.mode == 1 ? 1 : 0) == mode) ++n;
    return n;
}

juce::String Library::summary() const
{
    const int n = numSounds();
    juce::String s = juce::String (n) + (n > 1 ? L (" sons", " sounds", " sonidos") : L (" son", " sound", " sonido"));
    if (type == LibType::Pad)
    {
        int mj = exactSlots (0), mn = exactSlots (1);
        s << juce::String::fromUTF8 (" \xc2\xb7 ") << juce::String (mj + mn) << "/24 " << L ("tonalit\xc3\xa9s", "keys", "tonalidades");
    }
    else
    {
        s << juce::String::fromUTF8 (" \xc2\xb7 ") << juce::String ((int) categories.size()) << (categories.size() > 1 ? L (" cat\xc3\xa9gories", " categories", " categor\xc3\xad""as") : L (" cat\xc3\xa9gorie", " category", " categor\xc3\xad""a"));
    }
    return s;
}

juce::String makeLibraryId (const juce::String& folder)
{
    return "lib-" + juce::String::toHexString (folder.replaceCharacter ('\\', '/').toLowerCase().hashCode64());
}

bool isAudioFile (const juce::File& f)
{
    if (f.getFileName().startsWithChar ('.')) return false; // fichiers cachés du Mac (._son.wav)
    auto e = f.getFileExtension().toLowerCase();
    return e == ".wav" || e == ".wave" || e == ".aif" || e == ".aiff" || e == ".flac" || e == ".mp3" || e == ".ogg";
}

// ---------------------------------------------------------------------------------------------
// LibraryStore
// ---------------------------------------------------------------------------------------------
juce::File LibraryStore::storageFolder()
{
    juce::Array<juce::File> candidates;
   #if JUCE_MAC
    candidates.add (juce::File ("/Library/Application Support/JB Praise/Spacepad"));
    candidates.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/JB Praise/Spacepad"));
   #elif JUCE_WINDOWS
    auto pd = juce::SystemStats::getEnvironmentVariable ("ProgramData", "C:\\ProgramData");
    candidates.add (juce::File (pd).getChildFile ("JB Praise").getChildFile ("Spacepad"));
    candidates.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("JB Praise").getChildFile ("Spacepad"));
   #else
    candidates.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("JB Praise").getChildFile ("Spacepad"));
   #endif
    if (auto env = juce::SystemStats::getEnvironmentVariable ("SPACEPAD_DATA", {}); env.isNotEmpty())
        candidates.insert (0, juce::File (env));

    // dossier commun : le premier où l'on peut écrire (l'application, le VST3 et l'AU tombent sur le même)
    for (auto& d : candidates)
    {
        if (d.isDirectory() || d.createDirectory())
        {
            auto probe = d.getChildFile (".ecriture");
            if (probe.replaceWithText ("ok")) { probe.deleteFile(); return d; }
            // lecture seule mais déjà rempli : on le garde pour lire
            if (d.getChildFile ("libraries.json").existsAsFile()) return d;
        }
    }
    return candidates.getLast();
}

LibraryStore::LibraryStore()
{
    storeFile = storageFolder().getChildFile ("libraries.json");
    load();
}

static juce::String typeToString (LibType t) { return t == LibType::Crowd ? "crowd" : "pad"; }

juce::var LibraryStore::toVar (const std::vector<Library>& list)
{
    juce::Array<juce::var> arr;
    for (auto& l : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", l.id);
        o->setProperty ("name", l.name);
        o->setProperty ("folder", l.folder);
        o->setProperty ("type", typeToString (l.type));
        o->setProperty ("added", l.addedAt);
        juce::Array<juce::var> cats;
        for (auto& c : l.categories)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty ("name", c.name);
            juce::Array<juce::var> sounds;
            for (auto& s : c.sounds)
            {
                auto* so = new juce::DynamicObject();
                so->setProperty ("path", s.path);
                so->setProperty ("name", s.name);
                so->setProperty ("key", s.key);
                so->setProperty ("mode", s.mode);
                so->setProperty ("src", s.keySource);
                so->setProperty ("sec", s.seconds);
                sounds.add (juce::var (so));
            }
            co->setProperty ("sounds", sounds);
            cats.add (juce::var (co));
        }
        o->setProperty ("categories", cats);
        arr.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("app", "Spacepad by JB Praise");
    root->setProperty ("version", 1);
    root->setProperty ("libraries", arr);
    return juce::var (root);
}

std::vector<Library> LibraryStore::fromVar (const juce::var& v)
{
    std::vector<Library> out;
    if (auto* arr = v["libraries"].getArray())
        for (auto& lv : *arr)
        {
            Library l;
            l.id = lv["id"].toString();
            l.name = lv["name"].toString();
            l.folder = lv["folder"].toString();
            l.type = lv["type"].toString() == "crowd" ? LibType::Crowd : LibType::Pad;
            l.addedAt = (juce::int64) lv["added"];
            if (l.id.isEmpty()) l.id = makeLibraryId (l.folder);
            if (auto* cats = lv["categories"].getArray())
                for (auto& cv : *cats)
                {
                    Category c;
                    c.name = cv["name"].toString();
                    if (auto* ss = cv["sounds"].getArray())
                        for (auto& sv : *ss)
                        {
                            Sound s;
                            s.path = sv["path"].toString();
                            s.name = sv["name"].toString();
                            s.key = sv.hasProperty ("key") ? (int) sv["key"] : -1;
                            s.mode = sv.hasProperty ("mode") ? (int) sv["mode"] : -1;
                            s.keySource = sv["src"].toString();
                            s.seconds = (double) sv["sec"];
                            c.sounds.push_back (s);
                        }
                    l.categories.push_back (std::move (c));
                }
            if (l.name.isNotEmpty()) out.push_back (std::move (l));
        }
    return out;
}

bool LibraryStore::load()
{
    const juce::ScopedLock sl (lock);
    lastSeen = storeFile.getLastModificationTime();
    if (! storeFile.existsAsFile()) { libs.clear(); ++changes; return false; }
    auto v = juce::JSON::parse (storeFile.loadFileAsString());
    if (! v.isObject()) return false; // fichier abîmé : on garde ce qu'on a
    libs.clear();
    for (auto& l : fromVar (v)) libs.push_back (std::make_shared<const Library> (std::move (l)));
    ++changes;
    return true;
}

bool LibraryStore::save()
{
    const juce::ScopedLock sl (lock);
    std::vector<Library> plain;
    for (auto& l : libs) plain.push_back (*l);
    storeFile.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (storeFile);
    if (! tmp.getFile().replaceWithText (juce::JSON::toString (toVar (plain), false))) return false;
    if (! tmp.overwriteTargetFileWithTemporary()) return false;
   #if ! JUCE_WINDOWS
    storeFile.setReadOnly (false);
    ::chmod (storeFile.getFullPathName().toRawUTF8(), 0666); // modifiable par tous les comptes du Mac
   #endif
    lastSeen = storeFile.getLastModificationTime();
    ++changes;
    return true;
}

bool LibraryStore::reloadIfChanged()
{
    auto t = storeFile.getLastModificationTime();
    {
        const juce::ScopedLock sl (lock);
        if (t == lastSeen) return false;
    }
    return load();
}

void LibraryStore::addOrReplace (const std::vector<Library>& add)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& l : add)
        {
            auto copy = l;
            if (copy.id.isEmpty()) copy.id = makeLibraryId (copy.folder);
            if (copy.addedAt == 0) copy.addedAt = juce::Time::currentTimeMillis();
            auto it = std::find_if (libs.begin(), libs.end(), [&] (auto& x) { return x->id == copy.id; });
            if (it != libs.end()) *it = std::make_shared<const Library> (std::move (copy));
            else libs.push_back (std::make_shared<const Library> (std::move (copy)));
        }
    }
    save();
}

void LibraryStore::remove (const juce::String& id)
{
    {
        const juce::ScopedLock sl (lock);
        libs.erase (std::remove_if (libs.begin(), libs.end(), [&] (auto& x) { return x->id == id; }), libs.end());
    }
    save();
}

void LibraryStore::setType (const juce::String& id, LibType t)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& l : libs)
            if (l->id == id) { auto c = *l; c.type = t; l = std::make_shared<const Library> (std::move (c)); }
    }
    save();
}

std::vector<Library> LibraryStore::snapshot() const
{
    const juce::ScopedLock sl (lock);
    std::vector<Library> out;
    for (auto& l : libs) out.push_back (*l);
    return out;
}

std::shared_ptr<const Library> LibraryStore::find (const juce::String& id) const
{
    const juce::ScopedLock sl (lock);
    for (auto& l : libs) if (l->id == id) return l;
    return {};
}

// ---------------------------------------------------------------------------------------------
// FolderAnalyzer
// ---------------------------------------------------------------------------------------------
FolderAnalyzer::FolderAnalyzer (std::function<bool()> s) : stop (std::move (s))
{
    fm.registerBasicFormats();
}

static juce::String foldName (const juce::String& s)
{
    auto l = s.toLowerCase();
    return l.replace (juce::String::fromUTF8 ("\xc3\xa9"), "e").replace (juce::String::fromUTF8 ("\xc3\xa8"), "e");
}

bool FolderAnalyzer::nameHintsCrowd (const juce::String& n)
{
    auto f = foldName (n);
    for (auto w : { "crowd", "foule", "fx", "sfx", "effect", "effet", "natural", "naturel", "ambience", "ambiance", "voice", "voix", "vocal",
                    "prayer", "priere", "applause", "applaud", "congregation", "assemblee", "people", "riser", "impact", "whoosh", "sweep", "noise" })
        if (f.contains (w)) return true;
    return false;
}

bool FolderAnalyzer::nameHintsPad (const juce::String& n)
{
    auto f = foldName (n);
    return f.contains ("pad") || f.contains ("nappe") || f.contains ("drone") || f.contains ("texture");
}

static juce::Array<juce::File> audioIn (const juce::File& dir, bool recursive)
{
    juce::Array<juce::File> out;
    for (auto& e : juce::RangedDirectoryIterator (dir, recursive, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
        if (isAudioFile (e.getFile())) out.add (e.getFile());
    out.sort();
    return out;
}

static juce::Array<juce::File> subDirs (const juce::File& dir)
{
    juce::Array<juce::File> out;
    for (auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findDirectories | juce::File::ignoreHiddenFiles))
        if (! e.getFile().getFileName().startsWithChar ('.') && ! e.getFile().getFileName().startsWith ("__MACOSX"))
            out.add (e.getFile());
    out.sort();
    return out;
}

std::vector<Library> FolderAnalyzer::analyze (const juce::File& root)
{
    totalFiles = audioIn (root, true).size();
    doneFiles = 0;
    if (onProgress) onProgress (0.0, root.getFileName());
    auto libs = analyzeDir (root, 0);
    if (onProgress) onProgress (1.0, L ("Analyse termin\xc3\xa9""e", "Analysis complete", "An\xc3\xa1lisis terminado"));
    return libs;
}

std::vector<Library> FolderAnalyzer::analyzeAs (const juce::File& root, LibType type)
{
    forced = true;
    forcedType = type;
    totalFiles = audioIn (root, true).size();
    doneFiles = 0;
    if (onProgress) onProgress (0.0, root.getFileName());
    std::vector<Library> libs;
    if (type == LibType::Crowd)
    {
        // Crowd : une seule librairie au nom du dossier, une catégorie par sous-dossier
        auto lib = buildLibrary (root);
        if (lib.numSounds() > 0) libs.push_back (std::move (lib));
    }
    else
        libs = analyzeDir (root, 0);
    if (onProgress) onProgress (1.0, L ("Analyse termin\xc3\xa9""e", "Analysis complete", "An\xc3\xa1lisis terminado"));
    forced = false;
    return libs;
}

std::vector<Library> FolderAnalyzer::analyzeDir (const juce::File& dir, int depth)
{
    std::vector<Library> out;
    if ((stop && stop()) || depth > 8) return out;

    auto here = audioIn (dir, false);
    juce::Array<juce::File> subs;
    for (auto& s : subDirs (dir))
        if (! audioIn (s, true).isEmpty()) subs.add (s);

    // dossiers "Majeur" / "Mineur" à l'intérieur : c'est une seule librairie de pads
    bool modeFolders = ! subs.isEmpty();
    for (auto& s : subs) if (modeFromFolderName (s.getFileName()) < 0) modeFolders = false;

    if (! here.isEmpty() || modeFolders)
    {
        auto lib = buildLibrary (dir);
        if (lib.numSounds() > 0) out.push_back (std::move (lib));
        return out;
    }

    std::vector<std::vector<Library>> parts;
    for (auto& s : subs) parts.push_back (analyzeDir (s, depth + 1));

    // "Mes Crowds/Worship FX, Natural…" : une seule Crowd Library avec une catégorie par sous-dossier
    bool allSingleCrowd = ! parts.empty();
    for (auto& p : parts) if (p.size() != 1 || p[0].type != LibType::Crowd) allSingleCrowd = false;
    if (! forced && (allSingleCrowd || (nameHintsCrowd (dir.getFileName()) && ! nameHintsPad (dir.getFileName()) && ! parts.empty())))
    {
        Library merged;
        merged.name = dir.getFileName();
        merged.folder = dir.getFullPathName();
        merged.id = makeLibraryId (merged.folder);
        merged.type = LibType::Crowd;
        for (auto& p : parts)
            for (auto& l : p)
                for (auto& c : l.categories)
                {
                    Category cc = c;
                    cc.name = c.name.isEmpty() ? l.name : l.name + " / " + c.name;
                    merged.categories.push_back (std::move (cc));
                }
        if (merged.numSounds() > 0) out.push_back (std::move (merged));
        return out;
    }
    for (auto& p : parts) for (auto& l : p) out.push_back (std::move (l));
    return out;
}

Library FolderAnalyzer::buildLibrary (const juce::File& dir)
{
    Library lib;
    lib.name = dir.getFileName();
    lib.folder = dir.getFullPathName();
    lib.id = makeLibraryId (lib.folder);

    std::map<juce::String, Category> cats;
    for (auto& f : audioIn (dir, true))
    {
        auto rel = f.getParentDirectory().getRelativePathFrom (dir);
        juce::String catName = (rel == "." || rel.isEmpty()) ? juce::String() : rel.replaceCharacter ('\\', '/').replace ("/", " / ");
        Sound s;
        s.path = f.getFullPathName();
        s.name = f.getFileNameWithoutExtension();
        if (std::unique_ptr<juce::AudioFormatReader> rd { fm.createReaderFor (f) })
            s.seconds = rd->sampleRate > 0 ? (double) rd->lengthInSamples / rd->sampleRate : 0;
        else
            continue; // fichier illisible : ignoré
        cats[catName].name = catName;
        cats[catName].sounds.push_back (s);
    }
    for (auto& kv : cats) lib.categories.push_back (std::move (kv.second));
    lib.type = decideType (lib, dir);
    detectKeys (lib);
    return lib;
}

LibType FolderAnalyzer::decideType (const Library& lib, const juce::File& dir)
{
    if (forced) return forcedType;
    const auto n = dir.getFileName();
    if (nameHintsPad (n)) return LibType::Pad;
    if (nameHintsCrowd (n)) return LibType::Crowd;

    // tonalités écrites dans les noms de fichiers ?
    int keyed = 0; std::set<int> distinct;
    for (auto& c : lib.categories)
        for (auto& s : c.sounds)
        {
            auto k = keyFromName (s.name);
            if (k.valid()) { ++keyed; distinct.insert (k.key + 12 * juce::jmax (0, k.mode)); }
        }
    const int total = lib.numSounds();
    if (distinct.size() >= 3 || (total > 0 && keyed * 2 >= total && keyed > 0)) return LibType::Pad;

    // sinon : on écoute quelques sons. Des notes nettes = pads ; un son "plat" = foule, ambiance, effets
    float sum = 0; int count = 0;
    for (int i = 0; i < total && count < 6; i += juce::jmax (1, total / 6))
    {
        auto r = keyFromFile (fm, juce::File (lib.sound (i)->path), 12.0);
        sum += r.tonalStrength; ++count;
    }
    return (count > 0 && sum / (float) count >= 0.45f) ? LibType::Pad : LibType::Crowd;
}

void FolderAnalyzer::detectKeys (Library& lib)
{
    for (auto& c : lib.categories)
    {
        const int catMode = modeFromFolderName (c.name);
        for (auto& s : c.sounds)
        {
            if (stop && stop()) return;
            ++doneFiles;
            if (onProgress) onProgress (totalFiles > 0 ? juce::jmin (0.99, (double) doneFiles / totalFiles) : 0.5, s.name);

            auto k = keyFromName (s.name);
            if (k.valid())
            {
                s.key = k.key; s.mode = k.mode; s.keySource = "nom";
                if (s.mode < 0 && catMode >= 0) s.mode = catMode;
                if (s.mode < 0 && lib.type == LibType::Pad)
                {
                    // le nom donne la note mais pas majeur/mineur : on écoute
                    auto r = keyFromFile (fm, juce::File (s.path));
                    if (r.key.valid())
                    {
                        // la note écrite fait foi ; le mode vient du son (relatif mineur reconnu aussi)
                        if (r.key.key == s.key) s.mode = r.key.mode;
                        else if (r.key.mode == 1 && (r.key.key + 3) % 12 == s.key) s.mode = 0;
                        else if (r.key.mode == 0 && (s.key + 3) % 12 == r.key.key) s.mode = 1;
                        else s.mode = r.key.mode;
                    }
                    if (s.mode < 0) s.mode = 0;
                }
                continue;
            }
            auto r = keyFromFile (fm, juce::File (s.path));
            const bool keep = lib.type == LibType::Pad ? r.key.valid() : (r.key.valid() && r.tonalStrength >= 0.5f);
            if (keep)
            {
                s.key = r.key.key;
                s.mode = catMode >= 0 ? catMode : r.key.mode;
                s.keySource = "son";
            }
        }
    }
}
}
