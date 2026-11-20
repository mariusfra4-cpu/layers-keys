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
#include <climits>
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
    double metaBPM = 0;  // tempo écrit dans le fichier (bloc « acid » du WAV, étiquette BPM du MP3), 0 = absent
    int metaBeats = 0;
    int loopStart = 0, loopEnd = 0;  // points de boucle écrits dans le WAV (bloc « smpl »), 0 = absents

    int frames() const { return (int)L.size(); }
    double seconds() const { return rate > 0 ? (double)L.size() / rate : 0; }
    std::vector<float> mono() const;
};

constexpr double kMaxSeconds = 300;

// Lit un WAV (PCM 8/16/24/32 bits, flottant 32/64 bits). Renvoie false + message d'erreur.
bool parseWAV(const std::vector<uint8_t>& bytes, Audio& out, std::string& error);
bool loadWAV(const std::string& path, Audio& out, std::string& error);
// « WAV » qui contient en fait du MP3 (fréquent dans les packs téléchargés) :
// position et taille du MP3 à l'intérieur.
bool wavMp3Payload(const std::vector<uint8_t>& wavFile, size_t& offset, size_t& length);
// Fabrique un WAV stéréo 24 bits ; si bpm > 0 le tempo est écrit dans le fichier.
std::vector<uint8_t> encodeWAV(const Audio& a, double bpm, int beats);
bool isAudioFile(const std::string& path);  // .wav / .wave / .mp3

// Prépare le son pour une boucle parfaite : garde seulement la zone de boucle
// écrite dans le fichier, et pour un MP3 retire le petit silence que l'encodeur
// ajoute toujours au début et à la fin (sinon on entend un trou à chaque tour).
void prepareLoop(Audio& a, bool fromMp3);
// Tempo écrit dans l'étiquette d'un MP3 (ID3, champ « TBPM »). 0 = absent.
double readMp3Bpm(const std::vector<uint8_t>& fileStart);

// Silence ajouté par l'encodeur MP3, écrit dans l'en-tête « Xing / Info » par
// LAME, FFmpeg et la plupart des encodeurs. found = false si l'en-tête manque.
struct Mp3Gapless {
    bool found = false;
    int delay = 0, padding = 0;  // en échantillons
    long long frames = 0;        // nombre de blocs MP3 (0 = inconnu)
};
Mp3Gapless readMp3Gapless(const std::vector<uint8_t>& fileStart);
// Position du premier vrai bloc de son d'un MP3, après toutes les étiquettes
// (pochette, titre… même plusieurs à la suite) et les octets parasites.
// Renvoie SIZE_MAX si aucun son n'est trouvé.
size_t mp3AudioStart(const std::vector<uint8_t>& file);
// Coupe un MP3 décodé (décodeur de JUCE, qui ajoute 529 échantillons) exactement
// à sa longueur d'origine, grâce aux nombres écrits par l'encodeur.
void trimMp3(Audio& a, const Mp3Gapless& g);

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
// Séparation seule, avec une analyse déjà faite (celle de la bibliothèque) : très rapide.
std::vector<Stem> separateWith(const Audio& a, const LoopInfo& info);

// ---------------------------------------------------------------- bibliothèque

extern const char* const kUncategorized;  // « Non classés »
extern const char* const kIndexFile;      // .africanloops-index.json

// Style d'une loop d'après son chemin relatif : le nom du dossier où elle est rangée
// décide, sinon les mots du nom du fichier (worship, amapiano, makossa…).
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
    // threads = nombre de fils de calcul (0 = la moitié des cœurs).
    static bool scan(const std::string& root, Library& out, std::string& error,
                     Progress progress = nullptr, AudioLoader loader = nullptr, int threads = 0);
    static bool loadIndex(const std::string& root, Library& out);
    bool save() const;

    // La loop de ce chemin complet, si elle fait partie de la bibliothèque.
    const Entry* find(const std::string& absolutePath) const;
    // Vrai si le fichier n'a pas changé depuis l'analyse : on peut tout réutiliser.
    bool upToDate(const Entry& e) const;

    std::vector<CategoryCount> categories() const;
    bool setCategory(const std::string& path, const std::string& category);
    // Renomme un style (toutes ses loops suivent). Renvoie le nombre de loops.
    int renameCategory(const std::string& from, const std::string& to);
    // Supprime un style : ses loops vont dans « Non classés » (aucun fichier effacé).
    int removeCategory(const std::string& name);
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

// Mesure : la loop (écrite en 4/4) est transformée pour la jouer dans une autre mesure.
//   M44 : telle quelle.
//   M34 : chaque mesure de 4 temps est jouée sur 3 temps (temps 1, 2, 3) : valse.
//   M68 : de chaque mesure, on garde les croches 1-2-3 et 5-6-7 : balancement ternaire.
enum class Meter { M44 = 0, M34 = 1, M68 = 2 };

// Réglages envoyés au lecteur à chaque bloc audio par le plugin.
struct MixSettings {
    struct Strip { float gain = 1, pan = 0; bool mute = false, solo = false; };
    std::array<Strip, kNumTracks> tracks{};
    float master = 1, masterPan = 0;
    bool masterMute = false;
    double globalBPM = 120;
    bool presetBPM = false;
    double feel = 1;          // 1, 2 (Double), 0.5 (Half)
    bool metronome = false;
    Meter meter = Meter::M44;
    bool playing = false;
    // Horloge externe (Ableton Link ou logiciel hôte) : position en temps au
    // début du bloc. Négative = décompte avant le départ (silence).
    bool hostClock = false;
    double hostBeats = 0;
};

// Joue les pistes en boucle sur une horloge musicale (en temps et en mesures) :
//  - chaque loop est posée sur une barre de mesure ; une nouvelle loop choisie
//    pendant la lecture entre à la mesure suivante, au premier temps ;
//  - le tempo peut changer sans changer la hauteur du son : la loop est découpée
//    à chaque coup et chaque morceau est rejoué à vitesse normale ;
//  - la mesure (3/4, 6/8) réorganise les temps de la loop.
class Looper {
public:
    explicit Looper(int outputRate);

    // À l'arrêt : la loop est prête tout de suite. En lecture : elle entre à la
    // mesure suivante.
    void load(const LoopInfo& info, const std::vector<Stem>& stems);
    bool hasPendingLoop() const;
    void setGlobalBPM(double bpm);
    void setPresetBPM(bool on);  // vrai = tempo d'origine de la loop
    double globalBPM() const;
    double playingBPM() const;
    double loopLength() const;  // en échantillons de sortie
    double beatsPerBar() const;  // 4 en 4/4, 3 en 3/4 et 6/8 (en noires)

    void play();
    void stop();
    bool isPlaying() const;
    double position() const;  // 0..1 dans la boucle (lecture sans verrou)
    float trackPeak(int i) const;  // vumètre d'une piste (sans verrou)

    // Modifie une piste en toute sécurité pendant la lecture.
    void withTrack(int i, const std::function<void(Track&)>& f);
    Track track(int i) const;

    void setMaster(float gain, float pan, bool mute);
    void peaks(float& l, float& r) const;  // sans verrou

    // Vitesse ressentie : 1 = normal, 2 = « Double », 0.5 = « Half ».
    void setFeel(double factor);
    void setMeter(Meter m);
    // Métronome : un clic par temps, plus fort au début de chaque mesure.
    void setMetronome(bool on);
    bool hasLoop() const;

    // Fabrique la boucle telle qu'on l'entend (tempo, mesure, volumes, mutes…)
    // pour la glisser dans le logiciel de musique. track = -1 : toutes les
    // pistes ; sinon cette piste seule. Le tempo joué est renvoyé dans bpm.
    std::shared_ptr<Audio> bounce(int track, int passes, double& bpm) const;

    // Appelé par le moteur audio : remplit les deux tampons.
    void render(float* outL, float* outR, int n);
    // Applique les réglages puis rend le bloc, sans jamais attendre : si
    // l'interface charge une loop à cet instant, le bloc est rendu en silence.
    void process(const MixSettings& s, float* outL, float* outR, int n);

private:
    struct Loop {  // une loop prête à jouer
        std::array<Track, kNumTracks> tracks{};
        int srcRate = 44100, srcFrames = 0;
        double origBPM = 0;
        std::vector<double> slices{0.0};
    };
    static Loop prepare(const LoopInfo& info, const std::vector<Stem>& stems, int rate);
    void commit(Loop&& l, double anchorBeat);

    // tout ce qui suit : appelé verrou pris
    double playBPM() const;
    double speed() const;
    double beatLen() const;       // échantillons de sortie par temps
    double srcBeatFrames() const; // échantillons de la loop par temps (0 si tempo inconnu)
    bool transformed() const;     // la mesure réorganise-t-elle les temps ?
    double loopBeats() const;     // longueur de la boucle jouée, en temps
    double loopLen() const;
    bool gridAt(double tau, double& grid, double& gain) const;  // temps joué → position dans la loop
    bool sourcePos(double grid, double& src, double& gain) const;
    void renderLocked(float* outL, float* outR, int n);
    void publish();

    mutable std::mutex mu;
    int rate;
    Loop cur, next;
    bool hasNext = false;
    double nextAt = 0;  // barre de mesure où la loop suivante entre
    bool hostClock = false;
    double global = 120, feel = 1;
    bool preset = false, playing = false, metronome = false;
    Meter meter = Meter::M44;
    double clock = 0;   // horloge musicale, en temps
    double anchor = 0;  // temps où la loop en cours a commencé (barre de mesure)
    double clickPos = -1;
    bool clickAccent = false;
    long long lastBeat = LLONG_MIN;
    float master = 1, masterPan = 0;
    bool masterMute = false;
    float peakL = 0, peakR = 0;
    // copies lisibles par l'interface sans bloquer le son
    std::atomic<float> posShown{0}, peakShownL{0}, peakShownR{0};
    std::array<std::atomic<float>, kNumTracks> trackPeakShown{};
};

}  // namespace africanloops
