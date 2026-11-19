#include "AfricanLoops/AfricanLoops.h"

#include <algorithm>
#include <cmath>

namespace africanloops {

namespace {
constexpr float kHalfPi = 1.5707963f;
constexpr float kSqrt2 = 1.4142136f;
float clampPan(float p) { return std::clamp(p, -1.f, 1.f); }

// Met une piste à la longueur exacte de la boucle : ce qui dépasse à la fin
// (réverbération, résonance) est replacé au début, pour une jonction sans trou.
std::shared_ptr<Audio> fitToLoop(const std::shared_ptr<Audio>& a, int len) {
    if (!a || len <= 0 || a->frames() == len) return a;
    auto out = std::make_shared<Audio>();
    out->rate = a->rate;
    out->L.assign(len, 0.f);
    out->R.assign(len, 0.f);
    for (int i = 0; i < a->frames(); ++i) {
        out->L[i % len] += a->L[i];
        out->R[i % len] += a->R[i];
    }
    return out;
}
}  // namespace

Looper::Looper(int outputRate) : rate(outputRate > 0 ? outputRate : 44100) {}

void Looper::load(const LoopInfo& info, const std::vector<Stem>& stems) {
    // préparation hors verrou : le son continue pendant ce temps
    int newRate = rate, longest = 0;
    for (const auto& s : stems)
        if (s.audio) { newRate = s.audio->rate; longest = std::max(longest, s.audio->frames()); }
    const double bpm = info.tempo.bpm;
    int musical = 0;  // longueur musicale exacte : nombre entier de temps
    if (info.tempo.beats > 0 && bpm > 0) musical = (int)std::lround(info.tempo.beats * 60.0 / bpm * newRate);
    std::array<Track, kNumTracks> fresh{};
    for (size_t i = 0; i < stems.size() && i < (size_t)kNumTracks; ++i) {
        fresh[i].name = stems[i].kind;
        fresh[i].audio = musical > 0 ? fitToLoop(stems[i].audio, musical) : stems[i].audio;
    }
    const int frames = musical > 0 ? musical : longest;
    std::vector<double> sl(1, 0.0);
    for (double t : info.slices) {
        double s = t * newRate;
        if (s > sl.back() + 0.01 * newRate && s < frames) sl.push_back(s);
    }
    if (sl.size() < 2 && bpm > 0) {  // sans coups (nappes, voix) : découpage en doubles-croches
        double step = 60 / bpm / 4 * newRate;
        for (double s = step; s < frames; s += step) sl.push_back(s);
    }

    std::lock_guard<std::mutex> lk(mu);
    // en pleine lecture (live) : la nouvelle loop entre au même endroit de la
    // mesure, sans repartir du début ni casser le groove
    const bool keep = playing && srcFrames > 0 && loopLen() > 0;
    const double beats = keep ? pos / beatLen() : 0;
    tracks = std::move(fresh);
    srcRate = newRate;
    srcFrames = frames;
    origBPM = bpm;
    slices = std::move(sl);
    const double len = loopLen();
    pos = (keep && len > 0) ? std::fmod(beats * beatLen(), len) : 0;
    lastBeat = keep ? (long long)(pos / beatLen() + 1e-9) : -1;  // pas de clic de métronome en double
    if (!keep) clickPos = -1;
    publish();
}

double Looper::playBPM() const {
    double base = (preset && origBPM > 0) ? origBPM : global;
    return base * feel;
}
double Looper::speed() const { return origBPM > 0 ? playBPM() / origBPM : 1.0; }
double Looper::beatLen() const { return 60.0 / playBPM() * rate; }

double Looper::loopLen() const {
    if (srcFrames == 0) return 0;
    const double natural = (double)srcFrames / srcRate * rate;
    if (origBPM > 0) return natural / speed();
    // tempo inconnu (un seul coup, son très court) : on le rejoue sur la grille
    // du tempo global, en arrondissant au temps supérieur
    const double bl = beatLen();
    return std::max(1.0, std::ceil(natural / bl - 1e-6)) * bl;
}

void Looper::setGlobalBPM(double bpm) {
    if (!(bpm >= 20 && bpm <= 400)) return;
    std::lock_guard<std::mutex> lk(mu);
    double old = loopLen();
    global = bpm;
    if (old > 0) pos = std::fmod(pos / old * loopLen(), std::max(1.0, loopLen()));  // même position musicale
}

void Looper::setPresetBPM(bool on) {
    std::lock_guard<std::mutex> lk(mu);
    double old = loopLen();
    preset = on;
    if (old > 0) pos = std::fmod(pos / old * loopLen(), std::max(1.0, loopLen()));
}

void Looper::setFeel(double factor) {
    if (!(factor >= 0.25 && factor <= 4)) return;
    std::lock_guard<std::mutex> lk(mu);
    double old = loopLen();
    feel = factor;
    if (old > 0) pos = std::fmod(pos / old * loopLen(), std::max(1.0, loopLen()));
}

void Looper::setMetronome(bool on, int bpb) {
    std::lock_guard<std::mutex> lk(mu);
    metronome = on;
    beatsPerBar = std::clamp(bpb, 1, 16);
}

double Looper::globalBPM() const { std::lock_guard<std::mutex> lk(mu); return global; }
double Looper::playingBPM() const { std::lock_guard<std::mutex> lk(mu); return playBPM(); }
double Looper::loopLength() const { std::lock_guard<std::mutex> lk(mu); return loopLen(); }
bool Looper::hasLoop() const { std::lock_guard<std::mutex> lk(mu); return srcFrames > 0; }
void Looper::play() { std::lock_guard<std::mutex> lk(mu); playing = true; }
void Looper::stop() {
    std::lock_guard<std::mutex> lk(mu);
    playing = false;
    pos = 0;
    lastBeat = -1;
    clickPos = -1;
}
bool Looper::isPlaying() const { std::lock_guard<std::mutex> lk(mu); return playing; }
double Looper::position() const { return posShown.load(std::memory_order_relaxed); }
float Looper::trackPeak(int i) const {
    return (i >= 0 && i < kNumTracks) ? trackPeakShown[i].load(std::memory_order_relaxed) : 0.f;
}
void Looper::publish() {
    double n = loopLen();
    posShown.store(n > 0 ? (float)(pos / n) : 0.f, std::memory_order_relaxed);
    peakShownL.store(peakL, std::memory_order_relaxed);
    peakShownR.store(peakR, std::memory_order_relaxed);
    for (int i = 0; i < kNumTracks; ++i) trackPeakShown[i].store(tracks[i].peak, std::memory_order_relaxed);
}
void Looper::withTrack(int i, const std::function<void(Track&)>& f) {
    if (i < 0 || i >= kNumTracks) return;
    std::lock_guard<std::mutex> lk(mu);
    f(tracks[i]);
}
Track Looper::track(int i) const {
    std::lock_guard<std::mutex> lk(mu);
    return (i >= 0 && i < kNumTracks) ? tracks[i] : Track{};
}
void Looper::setMaster(float gain, float pan, bool mute) {
    std::lock_guard<std::mutex> lk(mu);
    master = gain; masterPan = pan; masterMute = mute;
}
void Looper::peaks(float& l, float& r) const {
    l = peakShownL.load(std::memory_order_relaxed);
    r = peakShownR.load(std::memory_order_relaxed);
}

// Position de sortie → position dans la loop d'origine. false = silence (entre
// la fin d'un morceau et le coup suivant quand on ralentit, ou après un son court).
bool Looper::sourcePos(double outPos, double& src, double& gain) const {
    const double sp = speed();
    const double grid = outPos / rate * srcRate * sp;  // position « musicale »
    auto it = std::upper_bound(slices.begin(), slices.end(), grid + 1e-9);
    size_t i = it == slices.begin() ? 0 : (size_t)(it - slices.begin()) - 1;
    const double start = slices[i];
    const double end = i + 1 < slices.size() ? slices[i + 1] : (double)srcFrames;
    const double elapsed = (grid - start) / sp;  // temps écoulé dans le morceau, vitesse normale
    src = start + elapsed;
    if (src >= end) return false;
    gain = 1;
    if (std::abs(sp - 1) < 1e-9) return true;  // tempo d'origine : lecture exacte
    const double fade = 0.004 * srcRate;       // 4 ms contre les clics
    if (sp < 1 && end - src < fade) gain = (end - src) / fade;
    if (sp > 1) {  // accéléré : le coup suivant arrive plus tôt, le morceau est coupé
        double r = (end - start) / sp - elapsed;
        if (r <= 0) return false;
        if (r < fade) gain = std::min(gain, r / fade);
    }
    return true;
}

void Looper::render(float* outL, float* outR, int n) {
    // Le son n'attend jamais l'interface : si elle modifie un réglage à cet
    // instant précis, ce petit bloc est rendu en silence.
    std::unique_lock<std::mutex> lk(mu, std::try_to_lock);
    if (!lk.owns_lock()) {
        std::fill(outL, outL + n, 0.f);
        std::fill(outR, outR + n, 0.f);
        return;
    }
    renderLocked(outL, outR, n);
}

void Looper::process(const MixSettings& s, float* outL, float* outR, int n) {
    std::unique_lock<std::mutex> lk(mu, std::try_to_lock);
    if (!lk.owns_lock()) {
        std::fill(outL, outL + n, 0.f);
        std::fill(outR, outR + n, 0.f);
        return;
    }
    for (int i = 0; i < kNumTracks; ++i) {
        auto& t = tracks[i];
        const auto& m = s.tracks[i];
        t.gain = m.gain; t.pan = m.pan; t.mute = m.mute; t.solo = m.solo;
    }
    master = s.master; masterPan = s.masterPan; masterMute = s.masterMute;
    metronome = s.metronome;
    beatsPerBar = std::clamp(s.beatsPerBar, 1, 16);
    const double old = loopLen();
    if (s.globalBPM >= 20 && s.globalBPM <= 400) global = s.globalBPM;
    preset = s.presetBPM;
    if (s.feel >= 0.25 && s.feel <= 4) feel = s.feel;
    const double len = loopLen();
    if (old > 0 && len > 0 && std::abs(old - len) > 1e-6) pos = std::fmod(pos / old * len, len);  // même position musicale
    if (s.playing && !playing) { pos = 0; lastBeat = -1; }
    if (!s.playing && playing) { pos = 0; lastBeat = -1; clickPos = -1; }
    playing = s.playing;
    if (s.hostBeats >= 0 && len > 0) {  // calé sur le logiciel de musique
        double want = std::fmod(s.hostBeats * beatLen(), len);
        if (std::abs(want - pos) > 64 && std::abs(want - pos) < len - 64) pos = want;
    }
    renderLocked(outL, outR, n);
}

void Looper::renderLocked(float* outL, float* outR, int n) {
    std::fill(outL, outL + n, 0.f);
    std::fill(outR, outR + n, 0.f);
    const double len = loopLen();
    if (!playing || len <= 0) {
        peakL *= 0.9f; peakR *= 0.9f;
        for (auto& t : tracks) t.peak *= 0.9f;
        publish();
        return;
    }
    bool anySolo = false;
    for (const auto& t : tracks) anySolo |= (t.audio && t.solo);
    struct Live { Track* t; float gl, gr, peak; };
    Live live[kNumTracks];
    int nl = 0;
    for (auto& t : tracks) {
        if (!t.audio) continue;
        float g = (!t.mute && (!anySolo || t.solo)) ? t.gain : 0.f;
        float a = (clampPan(t.pan) + 1) * kHalfPi / 2;  // panoramique à puissance constante
        live[nl++] = {&t, g * std::cos(a) * kSqrt2, g * std::sin(a) * kSqrt2, 0.f};
    }
    const float mg = masterMute ? 0.f : master, ma = (clampPan(masterPan) + 1) * kHalfPi / 2;
    const float mgl = mg * std::cos(ma) * kSqrt2, mgr = mg * std::sin(ma) * kSqrt2;
    const double bl = beatLen();
    const double clickLen = 0.03 * rate;
    float pl = peakL * 0.9f, pr = peakR * 0.9f;
    for (int i = 0; i < n; ++i) {
        double src, g;
        if (sourcePos(pos, src, g)) {
            size_t j = (size_t)src;
            float f = (float)(src - (double)j);
            for (int k = 0; k < nl; ++k) {
                const Audio& a = *live[k].t->audio;
                if (j >= a.L.size()) continue;
                float sl = a.L[j], sr = a.R[j];
                if (f != 0 && j + 1 < a.L.size()) {
                    sl += (a.L[j + 1] - a.L[j]) * f;
                    sr += (a.R[j + 1] - a.R[j]) * f;
                }
                float vl = sl * (float)g * live[k].gl, vr = sr * (float)g * live[k].gr;
                live[k].peak = std::max(live[k].peak, std::max(std::abs(vl), std::abs(vr)));
                outL[i] += vl * mgl;
                outR[i] += vr * mgr;
            }
        }
        if (metronome && bl > 1) {
            long long beat = (long long)(pos / bl + 1e-9);
            if (beat != lastBeat) {
                lastBeat = beat;
                clickPos = 0;
                clickAccent = beat % beatsPerBar == 0;
            }
            if (clickPos >= 0) {
                double t = clickPos / rate;
                float c = (float)((clickAccent ? 0.35 : 0.22) * std::sin(2 * 3.14159265358979 * (clickAccent ? 1500 : 1000) * t) *
                                  std::exp(-t / 0.008));
                outL[i] += c;
                outR[i] += c;
                if (++clickPos > clickLen) clickPos = -1;
            }
        }
        pl = std::max(pl, std::abs(outL[i]));
        pr = std::max(pr, std::abs(outR[i]));
        pos += 1;
        if (pos >= len) {
            pos -= len;
            lastBeat = -1;
        }
    }
    for (int k = 0; k < nl; ++k) live[k].t->peak = std::max(live[k].t->peak * 0.9f, live[k].peak);
    peakL = pl; peakR = pr;
    publish();
}

std::shared_ptr<Audio> Looper::bounce(int trackOnly, int passes, double& bpm) const {
    // copie rapide de l'état, puis rendu sans bloquer la lecture
    Looper tmp(rate);
    {
        std::lock_guard<std::mutex> lk(mu);
        tmp.srcRate = srcRate;
        tmp.srcFrames = srcFrames;
        tmp.origBPM = origBPM;
        tmp.global = global;
        tmp.feel = feel;
        tmp.preset = preset;
        tmp.slices = slices;
        tmp.tracks = tracks;
        tmp.master = master;
        tmp.masterPan = masterPan;
    }
    if (trackOnly >= 0 && trackOnly < kNumTracks) {
        for (int i = 0; i < kNumTracks; ++i) {
            tmp.tracks[i].solo = false;
            tmp.tracks[i].mute = i != trackOnly;
        }
    }
    bpm = tmp.playBPM();
    auto out = std::make_shared<Audio>();
    out->rate = rate;
    const double len = tmp.loopLen();
    if (len <= 0) return out;
    const int n = (int)std::lround(len * std::max(1, passes));
    out->L.resize(n);
    out->R.resize(n);
    tmp.playing = true;
    tmp.pos = 0;
    for (int i = 0; i < n; i += 4096) tmp.renderLocked(out->L.data() + i, out->R.data() + i, std::min(4096, n - i));
    return out;
}

}  // namespace africanloops
