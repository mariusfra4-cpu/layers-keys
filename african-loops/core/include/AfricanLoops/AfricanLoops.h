// African Loops Pro — moteur audio (C++17, sans dépendance).
//
// Ce moteur ne dépend ni de JUCE ni de HISE : il se branche dans un plugin
// JUCE (AudioProcessor) ou dans un projet HISE (nœud C++), et se teste seul.
//
//  - Audio        : loop stéréo en flottants, lecture/écriture WAV (tempo « acid »)
//  - analyse      : BPM automatique, reconnaissance des percussions
//  - separate     : une piste par percussion (la somme redonne la loop exacte)
//  - Library      : analyse d'un dossier, classement par style, index sauvegardé
//  - Looper       : lecture en boucle 8 pistes, Global BPM / Preset BPM
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace africanloops {

// ---------------------------------------------------------------- audio

struct Audio {
    int rate = 44100;
    std::vector<float> L, R;
    double metaBPM = 0;  // tempo écrit dans le fichier (bloc « acid »), 0 = absent
    int metaBeats = 0;

    int frames() const { return (int)L.size(); }
    double seconds() const { return rate > 0 ? (double)L.size() / rate : 0; }
    std::vector<float> mono() const;
};

constexpr double kMaxSeconds = 300;

// Lit un WAV (PCM 8/16/24/32 bits, flottant 32/64 bits). Renvoie false + message d'erreur.
bool parseWAV(const std::vector<uint8_t>& bytes, Audio& out, std::string& error);
bool loadWAV(const std::string& path, Audio& out, std::string& error);
// Fabrique un WAV stéréo 24 bits ; si bpm > 0 le tempo est écrit dans le fichier.
std::vector<uint8_t> encodeWAV(const Audio& a, double bpm, int beats);
bool isAudioFile(const std::string& path);  // .wav / .wave / .mp3

// ---------------------------------------------------------------- tempo

namespace tempo_source {
constexpr const char* File = "fichier";     // écrit dans le WAV par le fabricant
constexpr const char* Name = "nom";         // écrit dans le nom du fichier
constexpr const char* Length = "durée";     // la loop dure un nombre entier de temps
constexpr const char* Analysis = "analyse"; // déduit des coups seuls
constexpr const char* Unknown = "inconnu";
}  // namespace tempo_source

struct Tempo {
    double bpm = 0;
    int beats = 0;  // temps dans la loop (0 = inconnu)
    int bars = 0;   // mesures en 4/4 (0 = pas un nombre entier)
    std::string source = tempo_source::Unknown;
    double confidence = 0;  // 0..1
    std::vector<double> alternatives;  // moitié / double
};

// BPM écrit dans un nom de fichier. strong = le mot « bpm » est à côté du nombre.
double tempoFromFilename(const std::string& name, bool& strong);

// ---------------------------------------------------------------- percussions

namespace perc {
constexpr const char* Kick = "KICK";
constexpr const char* Snare = "SNARE/CLAP";
constexpr const char* Hat = "HI-HAT/SHAKER";
constexpr const char* Hand = "DJEMBE/CONGA";
constexpr const char* Talking = "TALKING DRUM";
}  // namespace perc
const std::vector<std::string>& percussionOrder();

struct Hit {
    double time = 0;  // secondes
    std::vector<std::string> kinds;
    double force = 0;
};

struct Percussion {
    std::string kind;
    int hits = 0;
    double share = 0;  // part des coups de la loop
};

struct Stem {
    std::string kind;
    std::shared_ptr<Audio> audio;
};

struct LoopInfo {
    Tempo tempo;
    double seconds = 0;
    int rate = 0;
    std::vector<Percussion> percussions;
    std::vector<Hit> hits;
    std::vector<double> slices;  // débuts des coups (s) : servent au changement de tempo
};

// Analyse complète (tempo + percussions).
LoopInfo analyse(const Audio& a, const std::string& filename);
// Analyse + une piste par percussion trouvée (ou la loop entière s'il n'y en a pas).
LoopInfo separate(const Audio& a, const std::string& filename, std::vector<Stem>& stems);

// ---------------------------------------------------------------- bibliothèque

extern const char* const kUncategorized;  // « Non classés »
extern const char* const kIndexFile;      // .africanloops-index.json

// Style d'une loop d'après son chemin relatif : le dossier décide (« I Love/x.wav » → I Love),
// sinon les mots du nom du fichier (worship, love, amapiano…).
std::string categoryOf(const std::string& relativePath);

struct Entry {
    std::string path;  // relatif au dossier de la bibliothèque, avec des « / »
    std::string category;
    int64_t size = 0;
    int64_t mtime = 0;  // secondes
    LoopInfo info;
    std::string error;
};

struct CategoryCount {
    std::string name;
    int count = 0;
};

// Fonction qui charge un fichier audio (WAV par défaut ; le plugin passe le
// lecteur de JUCE, qui sait aussi lire le MP3).
using AudioLoader = std::function<bool(const std::string& path, Audio& out, std::string& error)>;
using Progress = std::function<void(int done, int total, const std::string& current)>;

class Library {
public:
    std::string root;
    std::vector<Entry> entries;

    // Analyse toutes les loops du dossier ; ce qui n'a pas changé n'est pas refait.
    static bool scan(const std::string& root, Library& out, std::string& error,
                     Progress progress = nullptr, AudioLoader loader = nullptr);
    static bool loadIndex(const std::string& root, Library& out);
    bool save() const;

    std::vector<CategoryCount> categories() const;
    bool setCategory(const std::string& path, const std::string& category);
    std::vector<const Entry*> search(const std::string& category, const std::string& text) const;
};

// ---------------------------------------------------------------- lecteur

constexpr int kNumTracks = 8;

struct Track {
    std::string name;
    std::shared_ptr<Audio> audio;
    float gain = 1;  // 0..2
    float pan = 0;   // -1..1
    bool mute = false;
    bool solo = false;
    float peak = 0;  // vumètre
};

// Joue les pistes en boucle, calées sur le même tempo. Pour changer de tempo
// sans changer la hauteur du son, la loop est découpée à chaque coup et chaque
// morceau est rejoué à vitesse normale, placé plus tôt ou plus tard.
class Looper {
public:
    explicit Looper(int outputRate);

    void load(const LoopInfo& info, const std::vector<Stem>& stems);
    void setGlobalBPM(double bpm);
    void setPresetBPM(bool on);  // vrai = tempo d'origine de la loop
    double globalBPM() const;
    double playingBPM() const;
    double loopLength() const;  // en échantillons de sortie

    void play();
    void stop();
    bool isPlaying() const;
    double position() const;  // 0..1 dans la boucle

    // Modifie une piste en toute sécurité pendant la lecture.
    void withTrack(int i, const std::function<void(Track&)>& f);
    Track track(int i) const;

    void setMaster(float gain, float pan, bool mute);
    void peaks(float& l, float& r) const;

    // Appelé par le moteur audio : remplit les deux tampons.
    void render(float* outL, float* outR, int n);

private:
    float master = 1, masterPan = 0;
    bool masterMute = false;
    float peakL = 0, peakR = 0;

    bool sourcePos(double outPos, double& src, double& gain) const;
    double speed() const;
    double playBPM() const;
    double loopLen() const;

    mutable std::mutex mu;
    int rate, srcRate = 44100, srcFrames = 0;
    double origBPM = 0, global = 120;
    bool preset = false, playing = false;
    std::vector<double> slices;
    double pos = 0;
    std::array<Track, kNumTracks> tracks;
};

}  // namespace africanloops
