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
    out->L.assign((size_t)len, 0.f);
    out->R.assign((size_t)len, 0.f);
    for (int i = 0; i < a->frames(); ++i) {
        out->L[(size_t)(i % len)] += a->L[(size_t)i];
        out->R[(size_t)(i % len)] += a->R[(size_t)i];
    }
    return out;
}

// Réorganisation des temps d'une mesure de 4 temps de la loop.
struct MeterMap {
    double outBar;              // longueur d'une mesure jouée, en temps (noires)
    double seg;                 // taille d'un morceau, en temps
    std::vector<int> seq;       // morceaux de la mesure d'origine joués, dans l'ordre
};
const MeterMap& meterMap(Meter m) {
    static const MeterMap m44{4, 1, {0, 1, 2, 3}};
    static const MeterMap m34{3, 1, {0, 1, 2}};               // temps 1, 2, 3
    static const MeterMap m68{3, 0.5, {0, 1, 2, 4, 5, 6}};    // croches 1-2-3 et 5-6-7
    return m == Meter::M34 ? m34 : m == Meter::M68 ? m68 : m44;
}
constexpr double kSrcBar = 4;  // les loops sont écrites en 4/4
}  // namespace

Looper::Looper(int outputRate) : rate(outputRate > 0 ? outputRate : 44100) {}

Looper::Loop Looper::prepare(const LoopInfo& info, const std::vector<Stem>& stems, int outRate) {
    Loop l;
    int longest = 0;
    l.srcRate = outRate;
    for (const auto& s : stems)
        if (s.audio) { l.srcRate = s.audio->rate; longest = std::max(longest, s.audio->frames()); }
    l.origBPM = info.tempo.bpm;
    int musical = 0;  // longueur musicale exacte : nombre entier de temps
    if (info.tempo.beats > 0 && l.origBPM > 0) musical = (int)std::lround(info.tempo.beats * 60.0 / l.origBPM * l.srcRate);
    for (size_t i = 0; i < stems.size() && i < (size_t)kNumTracks; ++i) {
        l.tracks[i].name = stems[i].kind;
        l.tracks[i].audio = musical > 0 ? fitToLoop(stems[i].audio, musical) : stems[i].audio;
    }
    l.srcFrames = musical > 0 ? musical : longest;
    for (double t : info.slices) {
        double s = t * l.srcRate;
        if (s > l.slices.back() + 0.01 * l.srcRate && s < l.srcFrames) l.slices.push_back(s);
    }
    if (l.slices.size() < 2 && l.origBPM > 0) {  // sans coups (nappes, voix) : découpage en doubles-croches
        double step = 60 / l.origBPM / 4 * l.srcRate;
        for (double s = step; s < l.srcFrames; s += step) l.slices.push_back(s);
    }
    return l;
}

void Looper::commit(Loop&& l, double anchorBeat) {
    // les réglages du mixer (volume, pan, mute, solo) restent sur chaque piste
    for (int i = 0; i < kNumTracks; ++i) {
        auto& n = l.tracks[(size_t)i];
        const auto& o = cur.tracks[(size_t)i];
        n.gain = o.gain; n.pan = o.pan; n.mute = o.mute; n.solo = o.solo;
    }
    // en lecture, avec fondu enchaîné : l'ancienne loop s'efface pendant que la nouvelle monte
    if (playing && crossfadeSec > 0 && cur.srcFrames > 0) {
        prev = std::move(cur);
        prevAnchor = anchor;
        xfadeLen = xfadeLeft = std::max(1LL, (long long)std::llround(crossfadeSec * rate));
    } else {
        xfadeLeft = 0;
        prev = Loop{};
    }
    cur = std::move(l);
    anchor = anchorBeat;
}

void Looper::load(const LoopInfo& info, const std::vector<Stem>& stems) {
    Loop l = prepare(info, stems, rate);  // préparation hors verrou : le son continue pendant ce temps
    std::lock_guard<std::mutex> lk(mu);
    if (playing && cur.srcFrames > 0) {
        // en pleine lecture : la nouvelle loop entre à la prochaine barre de mesure
        // (tout de suite si on est juste dessus)
        const double bb = beatsPerBar();
        const double into = clock - std::floor(clock / bb) * bb;
        double at = into < 0.05 ? std::floor(clock / bb) * bb : std::ceil(clock / bb) * bb;
        next = std::move(l);
        hasNext = true;
        nextAt = at;
    } else {
        commit(std::move(l), 0);
        hasNext = false;
    }
}

bool Looper::hasPendingLoop() const { std::lock_guard<std::mutex> lk(mu); return hasNext; }

double Looper::playBPM(const Loop& l) const {
    double base = (preset && l.origBPM > 0) ? l.origBPM : global;
    return base * feel;
}
double Looper::speed(const Loop& l) const { return l.origBPM > 0 ? playBPM(l) / l.origBPM : 1.0; }
double Looper::beatLen() const { return 60.0 / playBPM() * rate; }
double Looper::srcBeatFrames(const Loop& l) { return l.origBPM > 0 ? 60.0 / l.origBPM * l.srcRate : 0; }
double Looper::beatsPerBar() const { return meterMap(meter).outBar; }

bool Looper::transformed(const Loop& l) const {
    if (meter == Meter::M44 || l.origBPM <= 0) return false;
    const double sb = l.srcFrames / srcBeatFrames(l);
    return sb >= kSrcBar - 1e-6 && std::abs(sb / kSrcBar - std::round(sb / kSrcBar)) < 1e-3;
}

double Looper::loopBeats(const Loop& l) const {
    if (l.srcFrames == 0) return 0;
    if (l.origBPM <= 0) {
        // tempo inconnu (un seul coup, son très court) : rejoué à chaque temps, en arrondissant au temps supérieur
        const double natural = (double)l.srcFrames / l.srcRate * playBPM(l) / 60;
        return std::max(1.0, std::ceil(natural - 1e-6));
    }
    const double sb = l.srcFrames / srcBeatFrames(l);
    if (transformed(l)) return std::round(sb / kSrcBar) * meterMap(meter).outBar;
    return sb;
}

double Looper::loopLen() const { return loopBeats() * beatLen(); }

// Temps joué depuis le début de la loop → position musicale dans la loop d'origine.
bool Looper::gridAt(const Loop& l, double tau, double& grid, double& gain) const {
    const double L = loopBeats(l);
    if (L <= 0) return false;
    double t = std::fmod(tau, L);
    if (t < 0) t += L;
    gain = 1;
    if (l.origBPM <= 0) {
        grid = t * (60.0 / playBPM(l) * rate) / rate * l.srcRate;
        return grid < l.srcFrames;  // au-delà du son : silence jusqu'au temps suivant
    }
    double srcBeat = t;
    if (transformed(l)) {
        const auto& m = meterMap(meter);
        const double bar = std::floor(t / m.outBar);
        const double w = t - bar * m.outBar;
        const int last = (int)m.seq.size() - 1;
        const int k = std::min(last, (int)(w / m.seg));
        const double frac = w - k * m.seg;
        srcBeat = bar * kSrcBar + m.seq[(size_t)k] * m.seg + frac;
        // petits fondus aux endroits où l'on saute un morceau : aucun clic
        const double fadeBeats = 0.004 * playBPM(l) / 60;
        const bool leftJump = k == 0 ? (m.seq[(size_t)last] + 1) * m.seg < kSrcBar - 1e-9 : m.seq[(size_t)k - 1] + 1 != m.seq[(size_t)k];
        const bool rightJump = k == last ? (m.seq[(size_t)last] + 1) * m.seg < kSrcBar - 1e-9 : m.seq[(size_t)k] + 1 != m.seq[(size_t)k + 1];
        if (leftJump && frac < fadeBeats) gain = frac / fadeBeats;
        if (rightJump && m.seg - frac < fadeBeats) gain = std::min(gain, (m.seg - frac) / fadeBeats);
    }
    grid = srcBeat * srcBeatFrames(l);
    return true;
}

// Position musicale → échantillon de la loop. false = silence (entre la fin d'un
// morceau et le coup suivant quand on ralentit).
bool Looper::sourcePos(const Loop& l, double grid, double& src, double& gain) const {
    const double sp = speed(l);
    const auto& sl = l.slices;
    auto it = std::upper_bound(sl.begin(), sl.end(), grid + 1e-9);
    size_t i = it == sl.begin() ? 0 : (size_t)(it - sl.begin()) - 1;
    const double start = sl[i];
    const double end = i + 1 < sl.size() ? sl[i + 1] : (double)l.srcFrames;
    const double elapsed = (grid - start) / sp;  // temps écoulé dans le morceau, vitesse normale
    src = start + elapsed;
    if (src >= end || src >= l.srcFrames) return false;
    gain = 1;
    if (std::abs(sp - 1) < 1e-9) return true;  // tempo d'origine : lecture exacte
    const double fade = 0.004 * l.srcRate;      // 4 ms contre les clics
    if (sp < 1 && end - src < fade) gain = (end - src) / fade;
    if (sp > 1) {  // accéléré : le coup suivant arrive plus tôt, le morceau est coupé
        double r = (end - start) / sp - elapsed;
        if (r <= 0) return false;
        if (r < fade) gain = std::min(gain, r / fade);
    }
    return true;
}

void Looper::setGlobalBPM(double bpm) {
    if (!(bpm >= 20 && bpm <= 400)) return;
    std::lock_guard<std::mutex> lk(mu);
    global = bpm;
}
void Looper::setPresetBPM(bool on) { std::lock_guard<std::mutex> lk(mu); preset = on; }
void Looper::setFeel(double factor) {
    if (!(factor >= 0.25 && factor <= 4)) return;
    std::lock_guard<std::mutex> lk(mu);
    feel = factor;
}
void Looper::setMeter(Meter m) { std::lock_guard<std::mutex> lk(mu); meter = m; }
void Looper::setMetronome(bool on) { std::lock_guard<std::mutex> lk(mu); metronome = on; }

double Looper::globalBPM() const { std::lock_guard<std::mutex> lk(mu); return global; }
double Looper::playingBPM() const { std::lock_guard<std::mutex> lk(mu); return playBPM(); }
double Looper::loopLength() const { std::lock_guard<std::mutex> lk(mu); return loopLen(); }
bool Looper::hasLoop() const { std::lock_guard<std::mutex> lk(mu); return cur.srcFrames > 0; }

void Looper::play() {
    std::lock_guard<std::mutex> lk(mu);
    if (playing) return;
    playing = true;
    clock = 0;
    if (hasNext) { commit(std::move(next), 0); hasNext = false; }
    xfadeLeft = 0;
    anchor = 0;
    lastBeat = LLONG_MIN;
}
void Looper::stop() {
    std::lock_guard<std::mutex> lk(mu);
    playing = false;
    clock = 0;
    anchor = 0;
    if (hasNext) { commit(std::move(next), 0); hasNext = false; }
    xfadeLeft = 0;
    prev = Loop{};
    lastBeat = LLONG_MIN;
    clickPos = -1;
    publish();
}
bool Looper::isPlaying() const { std::lock_guard<std::mutex> lk(mu); return playing; }

double Looper::position() const { return posShown.load(std::memory_order_relaxed); }
float Looper::trackPeak(int i) const {
    return (i >= 0 && i < kNumTracks) ? trackPeakShown[(size_t)i].load(std::memory_order_relaxed) : 0.f;
}
void Looper::publish() {
    const double L = loopBeats();
    double t = L > 0 ? std::fmod(clock - anchor, L) : 0;
    if (t < 0) t += L;
    posShown.store(L > 0 && playing ? (float)(t / L) : 0.f, std::memory_order_relaxed);
    peakShownL.store(peakL, std::memory_order_relaxed);
    peakShownR.store(peakR, std::memory_order_relaxed);
    for (size_t i = 0; i < (size_t)kNumTracks; ++i) trackPeakShown[i].store(cur.tracks[i].peak, std::memory_order_relaxed);
}

void Looper::withTrack(int i, const std::function<void(Track&)>& f) {
    if (i < 0 || i >= kNumTracks) return;
    std::lock_guard<std::mutex> lk(mu);
    f(cur.tracks[(size_t)i]);
}
Track Looper::track(int i) const {
    std::lock_guard<std::mutex> lk(mu);
    return (i >= 0 && i < kNumTracks) ? cur.tracks[(size_t)i] : Track{};
}
void Looper::setMaster(float gain, float pan, bool mute) {
    std::lock_guard<std::mutex> lk(mu);
    master = gain; masterPan = pan; masterMute = mute;
}
void Looper::peaks(float& l, float& r) const {
    l = peakShownL.load(std::memory_order_relaxed);
    r = peakShownR.load(std::memory_order_relaxed);
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

void Looper::apply(const MixSettings& s) {
    for (size_t i = 0; i < (size_t)kNumTracks; ++i) {
        for (Loop* l : {&cur, &prev}) {
            auto& t = l->tracks[i];
            const auto& m = s.tracks[i];
            t.gain = m.gain; t.pan = m.pan; t.mute = m.mute; t.solo = m.solo;
        }
    }
    master = s.master; masterPan = s.masterPan; masterMute = s.masterMute;
    metronome = s.metronome;
    meter = s.meter;
    crossfadeSec = std::clamp(s.crossfade, 0.0, 10.0);
    if (s.globalBPM >= 20 && s.globalBPM <= 400) global = s.globalBPM;
    preset = s.presetBPM;
    if (s.feel >= 0.25 && s.feel <= 4) feel = s.feel;
    if (s.playing && !playing) {
        playing = true;
        clock = 0;
        if (hasNext) { commit(std::move(next), 0); hasNext = false; }
        xfadeLeft = 0;
        anchor = 0;
        lastBeat = LLONG_MIN;
    } else if (!s.playing && playing) {
        playing = false;
        clock = 0;
        anchor = 0;
        if (hasNext) { commit(std::move(next), 0); hasNext = false; }
        xfadeLeft = 0;
        clickPos = -1;
    }
    // horloge externe (Link, logiciel hôte) : nos temps suivent les siens (×2 en Double, ÷2 en Half)
    if (s.hostClock) clock = s.hostBeats * feel;
    hostClock = s.hostClock;
}

void Looper::process(const MixSettings& s, float* outL, float* outR, int n) {
    std::unique_lock<std::mutex> lk(mu, std::try_to_lock);
    if (!lk.owns_lock()) {
        std::fill(outL, outL + n, 0.f);
        std::fill(outR, outR + n, 0.f);
        return;
    }
    apply(s);
    renderLocked(outL, outR, n);
}

void Looper::processTracks(const MixSettings& s, const std::array<Bus, kNumTracks>& tracks, Bus click, int n) {
    std::unique_lock<std::mutex> lk(mu, std::try_to_lock);
    if (!lk.owns_lock()) {
        for (const auto& b : tracks)
            if (b.L) { std::fill(b.L, b.L + n, 0.f); std::fill(b.R, b.R + n, 0.f); }
        if (click.L) { std::fill(click.L, click.L + n, 0.f); std::fill(click.R, click.R + n, 0.f); }
        return;
    }
    apply(s);
    renderLocked(nullptr, nullptr, n, &tracks, click);
}

void Looper::renderLocked(float* outL, float* outR, int n, const std::array<Bus, kNumTracks>* trk, Bus click) {
    // deux façons de rendre : tout mélangé (outL/outR, avec le master), ou piste par piste
    const bool split = trk != nullptr;
    if (!split) {
        std::fill(outL, outL + n, 0.f);
        std::fill(outR, outR + n, 0.f);
    } else {
        for (const auto& b : *trk)
            if (b.L) { std::fill(b.L, b.L + n, 0.f); std::fill(b.R, b.R + n, 0.f); }
        if (click.L) { std::fill(click.L, click.L + n, 0.f); std::fill(click.R, click.R + n, 0.f); }
    }
    if (!playing || cur.srcFrames == 0) {
        peakL *= 0.9f; peakR *= 0.9f;
        for (auto& t : cur.tracks) t.peak *= 0.9f;
        publish();
        return;
    }
    const float mg = split ? 1.f : (masterMute ? 0.f : master), ma = split ? kHalfPi / 2 : (clampPan(masterPan) + 1) * kHalfPi / 2;
    const float mgl = mg * std::cos(ma) * kSqrt2, mgr = mg * std::sin(ma) * kSqrt2;
    const double bl = beatLen(), step = 1.0 / bl;
    const int bpb = (int)std::lround(beatsPerBar());
    const double clickLen = 0.03 * rate;
    float pl = peakL * 0.9f, pr = peakR * 0.9f;

    struct Live { Track* t; int index; float gl, gr, peak; };
    Live live[kNumTracks], livePrev[kNumTracks];
    int nl = 0, np = 0;
    auto build = [&](Loop& l, Live* out, int& count) {
        bool anySolo = false;
        for (const auto& t : l.tracks) anySolo |= (t.audio && t.solo);
        count = 0;
        for (int i = 0; i < kNumTracks; ++i) {
            auto& t = l.tracks[(size_t)i];
            if (!t.audio) continue;
            float g = (!t.mute && (!anySolo || t.solo)) ? t.gain : 0.f;
            float a = (clampPan(t.pan) + 1) * kHalfPi / 2;  // panoramique à puissance constante
            out[count++] = {&t, i, g * std::cos(a) * kSqrt2, g * std::sin(a) * kSqrt2, 0.f};
        }
    };
    build(cur, live, nl);
    if (xfadeLeft > 0) build(prev, livePrev, np);

    // ajoute le son d'une loop à l'instant i, avec un gain
    auto mixLoop = [&](const Loop& l, double tau, Live* lv, int count, float extra, int i) {
        double grid, g1, src, g2;
        if (!gridAt(l, tau, grid, g1) || !sourcePos(l, grid, src, g2)) return;
        const float g = (float)(g1 * g2) * extra;
        const size_t j = (size_t)src;
        const float f = (float)(src - (double)j);
        for (int k = 0; k < count; ++k) {
            const Audio& a = *lv[k].t->audio;
            if (j >= a.L.size()) continue;
            float sl = a.L[j], sr = a.R[j];
            if (f != 0 && j + 1 < a.L.size()) {
                sl += (a.L[j + 1] - a.L[j]) * f;
                sr += (a.R[j + 1] - a.R[j]) * f;
            }
            const float vl = sl * g * lv[k].gl, vr = sr * g * lv[k].gr;
            lv[k].peak = std::max(lv[k].peak, std::max(std::abs(vl), std::abs(vr)));
            if (split) {
                const Bus& b = (*trk)[(size_t)lv[k].index];
                if (b.L) { b.L[i] += vl; b.R[i] += vr; }
            } else {
                outL[i] += vl * mgl;
                outR[i] += vr * mgr;
            }
        }
    };

    for (int i = 0; i < n; ++i) {
        // loop suivante : elle entre pile sur la barre de mesure
        if (hasNext && clock >= nextAt - 1e-9) {
            for (int k = 0; k < nl; ++k) live[k].t->peak = std::max(live[k].t->peak * 0.9f, live[k].peak);
            commit(std::move(next), nextAt);
            hasNext = false;
            build(cur, live, nl);
            if (xfadeLeft > 0) build(prev, livePrev, np);
        }
        const bool counting = hostClock && clock < 0;  // décompte de Link avant le départ
        if (!counting) {
            if (xfadeLeft > 0) {
                // fondu enchaîné à puissance constante
                const float x = (float)xfadeLeft / (float)xfadeLen;
                mixLoop(prev, clock - prevAnchor, livePrev, np, std::sin(x * kHalfPi), i);
                mixLoop(cur, clock - anchor, live, nl, std::cos(x * kHalfPi), i);
                if (--xfadeLeft == 0) prev = Loop{};
            } else {
                mixLoop(cur, clock - anchor, live, nl, 1.f, i);
            }
        }
        if (metronome) {
            const long long beat = (long long)std::floor(clock + 1e-9);
            if (beat != lastBeat) {
                if (lastBeat != LLONG_MIN || clock - (double)beat < step * 2) {  // pas de clic au milieu d'un temps
                    clickPos = 0;
                    clickAccent = ((beat % bpb) + bpb) % bpb == 0;
                }
                lastBeat = beat;
            }
            if (clickPos >= 0) {
                const double t = clickPos / rate;
                const float c = (float)((clickAccent ? 0.35 : 0.22) *
                                        std::sin(2 * 3.14159265358979 * (clickAccent ? 1500 : 1000) * t) * std::exp(-t / 0.008));
                if (split) {
                    if (click.L) { click.L[i] += c; click.R[i] += c; }
                } else {
                    outL[i] += c;
                    outR[i] += c;
                }
                if (++clickPos > clickLen) clickPos = -1;
            }
        }
        if (!split) {
            pl = std::max(pl, std::abs(outL[i]));
            pr = std::max(pr, std::abs(outR[i]));
        }
        clock += step;
    }
    for (int k = 0; k < nl; ++k) live[k].t->peak = std::max(live[k].t->peak * 0.9f, live[k].peak);
    if (!split) { peakL = pl; peakR = pr; }
    publish();
}

std::shared_ptr<Audio> Looper::bounce(int trackOnly, int passes, double& bpm) const {
    // copie rapide de l'état, puis rendu sans bloquer la lecture
    Looper tmp(rate);
    {
        std::lock_guard<std::mutex> lk(mu);
        tmp.cur = cur;
        tmp.global = global;
        tmp.feel = feel;
        tmp.preset = preset;
        tmp.meter = meter;
        tmp.master = master;
        tmp.masterPan = masterPan;
    }
    if (trackOnly >= 0 && trackOnly < kNumTracks) {
        for (int i = 0; i < kNumTracks; ++i) {
            tmp.cur.tracks[(size_t)i].solo = false;
            tmp.cur.tracks[(size_t)i].mute = i != trackOnly;
        }
    }
    bpm = tmp.playBPM();
    auto out = std::make_shared<Audio>();
    out->rate = rate;
    const double len = tmp.loopLen();
    if (len <= 0) return out;
    const int n = (int)std::lround(len * std::max(1, passes));
    out->L.resize((size_t)n);
    out->R.resize((size_t)n);
    tmp.playing = true;
    tmp.clock = 0;
    tmp.anchor = 0;
    for (int i = 0; i < n; i += 4096) tmp.renderLocked(out->L.data() + i, out->R.data() + i, std::min(4096, n - i));
    return out;
}

}  // namespace africanloops
