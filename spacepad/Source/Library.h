// Spacepad by JB Praise — bibliothèques (Pad Library et Crowd Library).
// - analyse d'un dossier : nom, type (pad ou crowd), catégories, tonalité de chaque son
// - enregistrement dans un fichier commun à l'application, au VST3 et à l'Audio Unit
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "KeyDetect.h"

namespace spacepad
{
enum class LibType { Pad, Crowd };

struct Sound
{
    juce::String path;      // chemin complet du fichier
    juce::String name;      // nom du fichier sans extension
    int key = -1;           // 0..11, -1 = pas de tonalité
    int mode = -1;          // 0 majeur, 1 mineur, -1 inconnu
    juce::String keySource; // "nom", "dossier", "son" ou ""
    double seconds = 0;
};

struct Category
{
    juce::String name;      // nom du sous-dossier ("" = racine)
    std::vector<Sound> sounds;
};

struct Library
{
    juce::String id;        // identifiant stable (calculé à partir du dossier)
    juce::String name;      // nom du dossier
    juce::String folder;    // chemin du dossier
    LibType type = LibType::Pad;
    std::vector<Category> categories;
    juce::int64 addedAt = 0;
    bool partial = false;   // choisie fichier par fichier : s'ajoute à la librairie du même dossier au lieu de la remplacer

    int numSounds() const;
    const Sound* sound (int index) const;          // index à plat sur toutes les catégories

    // Pad : quel son jouer pour une tonalité, et de combien le transposer (demi-tons)
    struct Slot { int sound = -1; int semitones = 0; bool exact = false; };
    Slot slotFor (int key, int mode) const;
    int exactSlots (int mode) const;                // nombre de tonalités présentes telles quelles (0..12)
    int countMode (int mode) const;                 // nombre de sons majeurs (0) ou mineurs (1)
    juce::String summary() const;                   // "24 WAV · 24/24 tonalités"
};

juce::String makeLibraryId (const juce::String& folder);
bool isAudioFile (const juce::File& f);
// écrit un fichier commun (application + plugins) de façon sûre ; sous Windows, réessaie si un autre programme le lit à ce moment
bool writeSharedFile (const juce::File& target, const juce::String& text);

// ---------------------------------------------------------------------------------------------
// Fichier commun des bibliothèques
// ---------------------------------------------------------------------------------------------
class LibraryStore
{
public:
    LibraryStore();

    static juce::File storageFolder();              // dossier commun (JB Praise/Spacepad)
    juce::File file() const { return storeFile; }

    bool load();                                    // relit le fichier
    bool save();                                    // écrit le fichier (sans risque de le casser)
    bool reloadIfChanged();                         // relit si une autre fenêtre/plugin l'a modifié

    // ajoute (ou remplace, si le même dossier existe déjà) puis enregistre
    bool addOrReplace (const std::vector<Library>& libs);
    void remove (const juce::String& id);
    void setType (const juce::String& id, LibType t);

    std::vector<Library> snapshot() const;
    int count() const { const juce::ScopedLock sl (lock); return (int) libs.size(); }
    std::shared_ptr<const Library> find (const juce::String& id) const;
    int changeCount() const { return changes.load(); }

    static juce::var toVar (const std::vector<Library>& libs);
    static std::vector<Library> fromVar (const juce::var& v);

private:
    mutable juce::CriticalSection lock;
    std::vector<std::shared_ptr<const Library>> libs;
    juce::File storeFile;
    bool dirty = false;
    juce::Time lastSeen;
    std::atomic<int> changes { 0 };
};

// ---------------------------------------------------------------------------------------------
// Copie des sons dans Spacepad, export et import vers / depuis un autre ordinateur
// (progress : 0..1, renvoie faux pour annuler)
// ---------------------------------------------------------------------------------------------
using ProgressFn = std::function<bool (double)>;
juce::File soundsFolder();                                                     // JB Praise/Spacepad/Sons
bool copySoundsInto (std::vector<Library>& libs, const juce::File& destRoot, ProgressFn progress);
bool exportLibraries (const std::vector<Library>& libs, const juce::File& dest, ProgressFn progress);   // écrit dest/Spacepad - librairies
std::vector<Library> importLibraries (const juce::File& exportFolder, ProgressFn progress);            // copie dans Spacepad/Sons

// ---------------------------------------------------------------------------------------------
// Analyse d'un dossier (lancée dans un fil séparé par l'interface)
// ---------------------------------------------------------------------------------------------
class FolderAnalyzer
{
public:
    explicit FolderAnalyzer (std::function<bool()> shouldStop = {});
    // progression : 0..1 et texte à afficher
    std::function<void (double, const juce::String&)> onProgress;

    std::vector<Library> analyze (const juce::File& root);
    // analyse depuis un onglet : tout est rangé en Pad (onglet Pad Library) ou en une seule Crowd Library (onglet Crowd)
    std::vector<Library> analyzeAs (const juce::File& root, LibType type);

    // ce que l'utilisateur a choisi ou déposé : dossiers et fichiers, mélangés, plusieurs à la fois
    std::vector<Library> analyzeItems (const juce::Array<juce::File>& items, LibType type);
    juce::StringArray skipped;                   // fichiers son refusés, avec la raison (« son.wav : … »)

    static bool nameHintsCrowd (const juce::String& folderName);
    static bool nameHintsPad (const juce::String& folderName);

private:
    std::vector<Library> analyzeDir (const juce::File& dir, int depth);
    Library buildLibrary (const juce::File& dir, const juce::Array<juce::File>* only = nullptr);
    void detectKeys (Library& lib);
    LibType decideType (const Library& lib, const juce::File& dir);

    juce::AudioFormatManager fm;
    std::function<bool()> stop;
    int totalFiles = 0, doneFiles = 0;
    bool forced = false;
    LibType forcedType = LibType::Pad;
};
}
