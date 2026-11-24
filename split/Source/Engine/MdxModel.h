// Layers Split — exécution d'un modèle MDX-Net (ONNX) sur un signal stéréo 44,1 kHz.
#pragma once

#include "OrtRuntime.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lsplit
{

struct MdxParams
{
    int nfft = 6144;
    int dimF = 2048;
    int dimT = 256;     // nombre de trames par morceau (2^dim_t_set)
    float compensate = 1.0f;
    int hop = 1024;
};

enum class Device { cpu, gpu };

struct StereoBuffer
{
    std::vector<float> l, r;
    size_t size() const { return l.size(); }
    void resize (size_t n) { l.assign (n, 0.0f); r.assign (n, 0.0f); }
};

class MdxModel
{
public:
    // progress : fraction [0,1] du travail de ce modèle. Retourne faux pour annuler.
    using Progress = std::function<bool (double)>;

    bool load (const std::string& pathUtf8, const MdxParams& params, Device device, std::string& error);
    bool isLoaded() const { return session != nullptr; }
    Device device() const { return dev; }

    // Sépare la source principale du modèle. Faux si annulé ; lance std::exception en cas d'erreur.
    // active : un drapeau par morceau (numChunks) ; les morceaux inactifs ne sont pas calculés (silence).
    bool process (const StereoBuffer& in, StereoBuffer& out, bool denoise, const Progress& progress,
                  const std::vector<char>* active = nullptr);

    // Morceaux où « ref » contient du signal (au-dessus de thresholdDb sous son niveau moyen).
    std::vector<char> chunkActivity (const StereoBuffer& ref, double thresholdDb) const;

    // Durée d'un morceau sur l'appareil choisi (pour comparer carte graphique et processeur).
    double timeOneChunk (const StereoBuffer& in);

    // Nombre d'exécutions du réseau nécessaires (pour l'estimation de la progression).
    size_t numChunks (size_t numSamples) const;

private:
    void runChunk (const float* l, const float* r, float* outL, float* outR, bool denoise);
    void runNet (const std::vector<float>& input, std::vector<float>& output);

    MdxParams p;
    Device dev = Device::cpu;
    std::unique_ptr<Ort::Session> session;
    std::string inputName, outputName;
};

} // namespace lsplit
