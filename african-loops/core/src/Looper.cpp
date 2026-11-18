#include "AfricanLoops/AfricanLoops.h"

#include <algorithm>
#include <cmath>

namespace africanloops {

Looper::Looper(int outputRate) : rate(outputRate) {}

void Looper::load(const LoopInfo& info, const std::vector<Stem>& stems) {
    std::lock_guard<std::mutex> lk(mu);
    tracks = {};
    srcFrames = 0;
    srcRate = rate;
    for (size_t i = 0; i < stems.size() && i < (size_t)kNumTracks; ++i) {
        tracks[i].name = stems[i].kind;
        tracks[i].audio = stems[i].audio;
        srcRate = stems[i].audio->rate;
        srcFrames = std::max(srcFrames, stems[i].audio->frames());
    }
    origBPM = info.tempo.bpm;
    slices.assign(1, 0.0);
    for (double t : info.slices) {
        double s = t * srcRate;
        if (s > slices.back() + 0.01 * srcRate && s < srcFrames) slices.push_back(s);
    }
    // sans coups (nappes, voix) : découpage régulier en doubles-croches
    if (slices.size() < 2 && origBPM > 0) {
        double step = 60 / origBPM / 4 * srcRate;
        for (double s = step; s < srcFrames; s += step) slices.push_back(s);
    }
    pos = 0;
}

double Looper::playBPM() const { return (preset || origBPM <= 0) ? origBPM : global; }
double Looper::speed() const {
    double b = playBPM();
    return (b > 0 && origBPM > 0) ? b / origBPM : 1.0;
}
double Looper::loopLen() const {
    return srcFrames == 0 ? 0.0 : (double)srcFrames / srcRate * rate / speed();
}

void Looper::setGlobalBPM(double bpm) {
    if (bpm < 20 || bpm > 400) return;
    std::lock_guard<std::mutex> lk(mu);
    double old = loopLen();
    global = bpm;
    if (old > 0) pos = pos / old * loopLen();  // même position musicale
}

void Looper::setPresetBPM(bool on) {
    std::lock_guard<std::mutex> lk(mu);
    double old = loopLen();
    preset = on;
    if (old > 0) pos = pos / old * loopLen();
}

double Looper::globalBPM() const { std::lock_guard<std::mutex> lk(mu); return global; }
double Looper::playingBPM() const { std::lock_guard<std::mutex> lk(mu); return playBPM(); }
double Looper::loopLength() const { std::lock_guard<std::mutex> lk(mu); return loopLen(); }
void Looper::play() { std::lock_guard<std::mutex> lk(mu); playing = true; }
void Looper::stop() { std::lock_guard<std::mutex> lk(mu); playing = false; pos = 0; }
bool Looper::isPlaying() const { std::lock_guard<std::mutex> lk(mu); return playing; }
double Looper::position() const {
    std::lock_guard<std::mutex> lk(mu);
    double n = loopLen();
    return n > 0 ? pos / n : 0;
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
void Looper::peaks(float& l, float& r) const { std::lock_guard<std::mutex> lk(mu); l = peakL; r = peakR; }

// Position de sortie → position dans la loop d'origine. false = silence entre la
// fin d'un morceau et le coup suivant (quand on ralentit).
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
    if (sp < 1 && end - src < fade) gain = (end - src) / fade;  // ralenti : le morceau s'éteint
    if (sp > 1) {  // accéléré : le coup suivant arrive plus tôt, le morceau est coupé
        double r = (end - start) / sp - elapsed;
        if (r <= 0) return false;
        if (r < fade) gain = std::min(gain, r / fade);
    }
    return true;
}

static float clampPan(float p) { return std::clamp(p, -1.f, 1.f); }

void Looper::render(float* outL, float* outR, int n) {
    std::fill(outL, outL + n, 0.f);
    std::fill(outR, outR + n, 0.f);
    // Le son ne doit jamais attendre l'interface : si elle modifie un réglage
    // à cet instant précis, on rend ce petit bloc en silence.
    std::unique_lock<std::mutex> lk(mu, std::try_to_lock);
    if (!lk.owns_lock()) return;
    const double len = loopLen();
    if (!playing || len <= 0) {
        peakL *= 0.9f; peakR *= 0.9f;
        return;
    }
    bool anySolo = false;
    for (const auto& t : tracks) anySolo |= (t.audio && t.solo);
    struct Live { Track* t; float gl, gr, peak; };
    Live live[kNumTracks];
    int nl = 0;
    constexpr float kHalfPi = 1.5707963f;
    for (auto& t : tracks) {
        if (!t.audio) continue;
        float g = (!t.mute && (!anySolo || t.solo)) ? t.gain : 0.f;
        float a = (clampPan(t.pan) + 1) * kHalfPi / 2;  // panoramique à puissance constante
        live[nl++] = {&t, g * std::cos(a) * 1.4142136f, g * std::sin(a) * 1.4142136f, 0.f};
    }
    float mg = masterMute ? 0.f : master, ma = (clampPan(masterPan) + 1) * kHalfPi / 2;
    float mgl = mg * std::cos(ma) * 1.4142136f, mgr = mg * std::sin(ma) * 1.4142136f;
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
        pl = std::max(pl, std::abs(outL[i]));
        pr = std::max(pr, std::abs(outR[i]));
        if (++pos >= len) pos -= len;
    }
    for (int k = 0; k < nl; ++k) live[k].t->peak = std::max(live[k].t->peak * 0.9f, live[k].peak);
    peakL = pl; peakR = pr;
}

}  // namespace africanloops
