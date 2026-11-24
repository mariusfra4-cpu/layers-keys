#include "Internal.h"

#include <algorithm>
#include <cmath>
#include <regex>

namespace africanloops {

namespace {
constexpr double kMinBPM = 60, kMaxBPM = 200;

std::string baseName(const std::string& path) {
    auto s = path.find_last_of("/\\");
    std::string b = s == std::string::npos ? path : path.substr(s + 1);
    auto d = b.find_last_of('.');
    return d == std::string::npos ? b : b.substr(0, d);
}

double parseBPM(std::string s) {
    std::replace(s.begin(), s.end(), ',', '.');
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    return (v >= 40 && v <= 300) ? v : 0;
}
}  // namespace

double tempoFromFilename(const std::string& name, bool& strong) {
    static const std::regex strongRe(R"((?:^|[^0-9])(\d{2,3}(?:[.,]\d{1,2})?)\s*[-_ ]?\s*bpm)", std::regex::icase);
    static const std::regex firstRe(R"(bpm\s*[-_ ]?\s*(\d{2,3}(?:[.,]\d{1,2})?)(?:[^0-9]|$))", std::regex::icase);
    static const std::regex numRe(R"((?:^|[^0-9a-zA-Z])(\d{2,3})(?=[^0-9a-zA-Z]|$))");
    const std::string base = baseName(name);
    std::smatch m;
    strong = false;
    if (std::regex_search(base, m, strongRe))
        if (double v = parseBPM(m[1].str()); v > 0) return strong = true, v;
    if (std::regex_search(base, m, firstRe))
        if (double v = parseBPM(m[1].str()); v > 0) return strong = true, v;
    // un nombre seul, par ex. « Worship_96_Am.wav » : à confirmer par la durée
    for (auto it = std::sregex_iterator(base.begin(), base.end(), numRe); it != std::sregex_iterator(); ++it)
        if (double v = parseBPM((*it)[1].str()); v >= kMinBPM && v <= kMaxBPM) return v;
    return 0;
}

namespace detail {

namespace {

// Mesure à quel point les coups se répètent à un tempo donné.
struct TempoScorer {
    std::vector<double> ac;  // autocorrélation circulaire (une loop se répète)
    double fr;

    TempoScorer(const std::vector<double>& flux, int rate) : fr((double)rate / kHop) {
        const int T = (int)flux.size();
        if (T < 8) return;
        double mean = 0;
        for (double v : flux) mean += v;
        mean /= T;
        std::vector<double> x(T);
        for (int i = 0; i < T; ++i) x[i] = flux[i] - mean;
        int maxLag = std::min(T - 1, (int)(4 * 60 * fr / kMinBPM) + 2);
        ac.assign(maxLag + 1, 0.0);
        for (int l = 0; l <= maxLag; ++l) {
            double s = 0;
            for (int i = 0; i < T; ++i) s += x[i] * x[(i + l) % T];
            ac[l] = s;
        }
        if (ac[0] > 0)
            for (auto& v : ac) v /= ac[0];
    }

    double at(double lag) const {
        if (lag < 0 || (size_t)lag + 1 >= ac.size()) return 0;
        size_t i = (size_t)lag;
        double f = lag - (double)i;
        return ac[i] * (1 - f) + ac[i + 1] * f;
    }

    // les coups reviennent-ils tous les 1, 2, 3, 4 temps (et à la croche) ?
    double score(double bpm) const {
        if (ac.empty()) return 0;
        const double p = 60 * fr / bpm;
        double s = 0, n = 0;
        for (int k = 1; k <= 4; ++k) {
            if (double lag = p * k; (size_t)lag + 1 < ac.size()) { s += at(lag); n += 1; }
            if (k == 1) { s += 0.5 * at(p / 2); n += 0.5; }
        }
        return n > 0 ? s / n : 0;
    }
};

// préférence douce pour les tempos courants (autour de 105 BPM)
double prior(double bpm) {
    double d = std::log2(bpm / 105) / 0.8;
    return std::exp(-0.5 * d * d);
}

// La caisse claire tombe presque toujours sur les temps 2 et 4 (tous les 2 temps).
// Si, au tempo trouvé, elle revient tous les 4 temps, on a compté deux fois trop vite.
bool backbeatTooFast(double bpm, const std::vector<Hit>& hits) {
    if (bpm / 2 < 50) return false;
    std::vector<double> times;
    for (const auto& h : hits)
        for (const auto& k : h.kinds)
            if (k == perc::Snare) times.push_back(h.time);
    if (times.size() < 3) return false;
    int four = 0;
    for (size_t i = 1; i < times.size(); ++i) {
        double g = (times[i] - times[i - 1]) * bpm / 60;  // écart en temps
        if (g < 0.1 * bpm / 60) continue;
        if (g < 3.5) return false;  // la caisse claire revient avant 4 temps : le tempo est bon
        if (std::abs(g / 4 - std::round(g / 4)) < 0.1) four++;  // 4, 8, 12… temps
    }
    return four >= 2;
}

}  // namespace

// La partie du fichier après l'échantillon « from » est-elle (presque) muette ?
// (silence d'encodeur, fin de réverbération coupée) — sinon c'est de la musique.
static bool silentAfter(const Audio& a, long long from) {
    const long long n = a.frames();
    if (from >= n) return true;
    if (from < 0) from = 0;
    double all = 0, end = 0;
    for (long long i = 0; i < n; ++i) {
        const double v = 0.5 * ((double)a.L[(size_t)i] + a.R[(size_t)i]);
        all += v * v;
        if (i >= from) end += v * v;
    }
    const double rmsAll = std::sqrt(all / (double)n), rmsEnd = std::sqrt(end / (double)(n - from));
    return rmsEnd < 0.1 * rmsAll;
}

Tempo detectTempo(const Audio& a, const Analysis& an, const std::vector<Hit>& hits, const std::string& filename) {
    const double dur = a.seconds();
    auto finish = [&](Tempo t) {
        if (t.source == tempo_source::Length || t.source == tempo_source::Analysis) {
            if (backbeatTooFast(t.bpm, hits)) {
                t.bpm /= 2;
                t.beats = (t.beats % 2 == 0) ? t.beats / 2 : 0;
            }
        }
        // Longueur musicale de la boucle (nombre entier de temps) : elle est
        // toujours fixée quand le tempo est connu, pour que la boucle tourne sans
        // trou ni décalage, même si le fichier dure un peu plus ou un peu moins.
        if (dur > 0 && t.bpm > 0 && t.beats == 0) {
            const double b = dur * t.bpm / 60, rb = std::round(b);
            if (rb >= 1 && std::abs(b - rb) < 0.08) {
                t.beats = (int)rb;  // fichier coupé pile (à moins d'un douzième de temps près)
            } else if (b >= 3.5) {
                const double bars = std::round(b / 4) * 4, whole = std::floor(b / 4) * 4;
                if (bars >= 4 && std::abs(b - bars) <= 1.0) t.beats = (int)bars;   // presque des mesures entières
                else if (whole >= 4 && b - whole <= 2.5) t.beats = (int)whole;      // + une fin qui dépasse
                else t.beats = (int)std::max(1.0, rb);                              // sinon : temps entiers
            } else {
                t.beats = (int)std::max(1.0, rb);  // loop très courte
            }
        }
        // Précision : si la loop est une vraie boucle (de la musique jusqu'au bout
        // du fichier), c'est la longueur du fichier qui fait foi. On ajuste le tempo
        // (au plus 1,5 %) pour que la boucle tombe pile, sans rien couper ni ajouter :
        // pas de « dandinement » à chaque tour.
        if (t.source != tempo_source::File && !a.lengthUncertain && dur > 0 && t.bpm > 0 && t.beats > 0) {
            const double musical = t.beats * 60.0 / t.bpm;  // secondes
            const double exact = 60.0 * t.beats / dur;
            const double gap = dur - musical;
            const bool silence = gap > 0 && silentAfter(a, (long long)std::llround(musical * a.rate));
            if (std::abs(gap) * a.rate > 2 && !silence && std::abs(exact - t.bpm) / t.bpm < 0.015)
                t.bpm = std::round(exact * 1000) / 1000;
        }
        if (t.beats > 0 && t.beats % 4 == 0) t.bars = t.beats / 4;
        for (double alt : {t.bpm / 2, t.bpm * 2})
            if (alt >= 45 && alt <= 230) t.alternatives.push_back(std::round(alt * 100) / 100);
        return t;
    };
    auto make = [](double bpm, int beats, const char* src, double conf) {
        Tempo t;
        t.bpm = bpm; t.beats = beats; t.source = src; t.confidence = conf;
        return t;
    };

    // 1. tempo écrit dans le fichier
    if (a.metaBPM > 0) return finish(make(a.metaBPM, a.metaBeats, tempo_source::File, 1));
    // 2. tempo écrit dans le nom (« Groove_96bpm.wav »)
    bool strong = false;
    const double nameBPM = tempoFromFilename(filename, strong);
    if (strong) return finish(make(nameBPM, 0, tempo_source::Name, 0.95));

    // il faut au moins 3 coups pour mesurer un tempo
    if (an.onsets.size() < 3) {
        if (nameBPM > 0) return finish(make(nameBPM, 0, tempo_source::Name, 0.6));
        return Tempo{};
    }
    const TempoScorer ts(an.flux, a.rate);
    // 3. meilleur tempo d'après les coups seuls
    double best = 0, bestS = -1;
    std::vector<double> all;
    for (double b = kMinBPM; b <= kMaxBPM + 1e-9; b += 0.05) {
        double s = ts.score(b) * prior(b);
        all.push_back(s);
        if (s > bestS) { best = b; bestS = s; }
    }
    double conf = 0;
    if (!all.empty() && bestS > 0) {
        std::vector<double> sorted = all;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        conf = clamp01((bestS - sorted[sorted.size() / 2]) / bestS);
    }

    // 4. la durée : une loop bien coupée dure un nombre entier de temps
    if (dur > 0.5) {
        double candBPM = 0, candS = -1;
        int candBeats = 0;
        for (int beats : {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128}) {
            double b = 60.0 * beats / dur;
            if (b < kMinBPM || b > kMaxBPM) continue;
            double s = ts.score(b) * prior(b);
            if (s > candS) { candBPM = b; candS = s; candBeats = beats; }
        }
        // nombre seul dans le nom, qui tombe pile sur la durée
        if (nameBPM > 0) {
            double beats = dur * nameBPM / 60, rb = std::round(beats);
            if (rb >= 1 && std::abs(beats - rb) < 0.01 * rb)
                return finish(make(nameBPM, (int)rb, tempo_source::Name, 0.9));
        }
        if (candBPM > 0 && (bestS <= 0 || candS >= 0.7 * bestS)) {
            // presque toutes les loops sont faites à un tempo entier : quelques
            // millisecondes en trop dans le fichier ne doivent pas donner 87,82 au lieu de 88
            double bpm = candBPM, r = std::round(bpm);
            bpm = std::abs(bpm - r) / bpm < 0.006 ? r : std::round(bpm * 100) / 100;
            double c = 0.6 + 0.4 * conf;
            if (bestS > 0 && std::abs(candBPM - best) / best < 0.02) c = std::max(c, 0.9);  // d'accord
            return finish(make(bpm, candBeats, tempo_source::Length, clamp01(c)));
        }
    }
    if (bestS <= 0) return Tempo{};
    // 5. coups seuls (loop mal coupée)
    double bpm = std::round(best * 10) / 10, r = std::round(bpm);
    if (std::abs(bpm - r) <= std::max(0.3, 0.006 * bpm)) bpm = r;
    return finish(make(bpm, 0, tempo_source::Analysis, conf * 0.8));
}

}  // namespace detail
}  // namespace africanloops
