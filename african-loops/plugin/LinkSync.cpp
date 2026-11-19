#include "LinkSync.h"

#include <ableton/Link.hpp>

struct LinkSync::Impl {
    ableton::Link link{120.0};
    Impl() { link.enableStartStopSync(true); }
};

LinkSync::LinkSync() : impl(std::make_unique<Impl>()) {}
LinkSync::~LinkSync() { impl->link.enable(false); }

void LinkSync::setEnabled(bool on) {
    if (impl->link.isEnabled() != on) impl->link.enable(on);
}
bool LinkSync::isEnabled() const { return impl->link.isEnabled(); }
int LinkSync::peers() const { return (int)impl->link.numPeers(); }

void LinkSync::setTempo(double bpm) {
    if (!(bpm >= 20 && bpm <= 999)) return;
    auto st = impl->link.captureAppSessionState();
    st.setTempo(bpm, impl->link.clock().micros());
    impl->link.commitAppSessionState(st);
}

LinkSync::State LinkSync::audioState(double quantum, std::int64_t latencyMicros) const {
    const auto t = impl->link.clock().micros() + std::chrono::microseconds(latencyMicros);
    const auto st = impl->link.captureAudioSessionState();
    State s;
    s.tempo = st.tempo();
    s.beat = st.beatAtTime(t, quantum);
    s.playing = st.isPlaying();
    return s;
}

void LinkSync::audioRequestPlay(bool play, double quantum, std::int64_t latencyMicros) {
    const auto t = impl->link.clock().micros() + std::chrono::microseconds(latencyMicros);
    auto st = impl->link.captureAudioSessionState();
    if (play) st.setIsPlayingAndRequestBeatAtTime(true, t, 0.0, quantum);  // temps 0 = début de la prochaine mesure
    else st.setIsPlaying(false, t);
    impl->link.commitAudioSessionState(st);
}
