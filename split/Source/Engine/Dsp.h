// Layers Split — outils de traitement du signal : STFT/iSTFT (identiques à torch.stft
// avec center=True, fenêtre de Hann périodique) et rééchantillonnage sinc fenêtré.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace lsplit
{

class Stft
{
public:
    Stft (int nfft, int hop);

    int nfft() const { return n; }
    int hop() const { return h; }
    int bins() const { return n / 2 + 1; }

    // x : length = hop * (frames - 1). Sortie : frames x bins (ligne = trame).
    void forward (const float* x, int length, std::vector<std::complex<float>>& out, int& frames) const;

    // spec : frames x bins. Sortie : length = hop * (frames - 1) échantillons.
    void inverse (const std::vector<std::complex<float>>& spec, int frames, float* out) const;

private:
    int n, h;
    std::vector<float> window;
};

// Rééchantillonnage de haute qualité (sinc fenêtré Kaiser, 32 passages par zéro de chaque côté).
std::vector<float> resample (const float* in, size_t numIn, double inRate, double outRate);

} // namespace lsplit
