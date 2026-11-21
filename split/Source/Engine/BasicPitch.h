// Layers Split — détection polyphonique des notes avec « Basic Pitch » (Spotify, licence Apache 2.0).
// Petit réseau ONNX : signal mono 22 050 Hz -> activations par note (88 touches) et contour fin (1/3 de ton).
#pragma once

#include "MdxModel.h"

namespace lsplit
{

class BasicPitchFrames
{
public:
    static constexpr double rate = 22050.0 / 256.0;  // trames par seconde

    // Lance std::exception si le modèle est introuvable ou en erreur.
    void compute (const StereoBuffer& in, double sr, const std::string& modelPathUtf8);

    size_t frames() const { return numFrames; }
    const float* note (size_t frame) const { return notes.data() + frame * 88; }       // 88 notes (MIDI 21..108)
    const float* contour (size_t frame) const { return contours.data() + frame * 264; } // 3 cases par demi-ton

private:
    size_t numFrames = 0;
    std::vector<float> notes, contours;
};

} // namespace lsplit
