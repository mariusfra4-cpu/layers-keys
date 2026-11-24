#include "MdxModel.h"
#include "Dsp.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <chrono>
#include <cmath>
#include <thread>

namespace lsplit
{

bool MdxModel::load (const std::string& path, const MdxParams& params, Device device, std::string& error)
{
    session.reset();
    p = params;
    dev = device;

    try
    {
        Ort::SessionOptions opts;
        OrtRuntime::configure (opts);

        if (device == Device::gpu)
        {
            std::string e;
            if (! OrtRuntime::appendDirectML (opts, e))
            {
                error = "GPU indisponible (" + e + ")";
                return false;
            }
        }

        session = std::make_unique<Ort::Session> (OrtRuntime::env(), toOrtPath (path).c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        inputName = session->GetInputNameAllocated (0, alloc).get();
        outputName = session->GetOutputNameAllocated (0, alloc).get();

        // Vérifie la forme attendue [N, 4, dimF, dimT]
        auto shape = session->GetInputTypeInfo (0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 4 || (shape[1] > 0 && shape[1] != 4)
            || (shape[2] > 0 && shape[2] != p.dimF) || (shape[3] > 0 && shape[3] != p.dimT))
        {
            error = "Modèle incompatible : " + path;
            session.reset();
            return false;
        }
    }
    catch (const std::exception& e)
    {
        error = e.what();
        session.reset();
        return false;
    }
    return true;
}

size_t MdxModel::numChunks (size_t n) const
{
    const size_t chunk = (size_t) p.hop * (size_t) (p.dimT - 1);
    const size_t gen = chunk - (size_t) p.nfft; // chunk - 2*trim
    const size_t pad = gen - n % gen;
    return (n + pad) / gen;
}

void MdxModel::runNet (const std::vector<float>& input, std::vector<float>& output)
{
    const std::array<int64_t, 4> shape { 1, 4, p.dimF, p.dimT };
    auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value in = Ort::Value::CreateTensor<float> (mem, const_cast<float*> (input.data()), input.size(), shape.data(), shape.size());

    const char* inNames[] = { inputName.c_str() };
    const char* outNames[] = { outputName.c_str() };
    auto res = session->Run (Ort::RunOptions { nullptr }, inNames, &in, 1, outNames, 1);
    if (res.empty())
        throw std::runtime_error ("Le modèle n'a rien renvoyé.");

    const float* data = res[0].GetTensorData<float>();
    const size_t count = res[0].GetTensorTypeAndShapeInfo().GetElementCount();
    if (count != input.size())
        throw std::runtime_error ("Sortie du modèle de taille inattendue.");
    output.assign (data, data + count);
}

void MdxModel::runChunk (const float* l, const float* r, float* outL, float* outR, bool denoise)
{
    const int chunk = p.hop * (p.dimT - 1);
    Stft stft (p.nfft, p.hop);
    const int nb = stft.bins();
    const size_t plane = (size_t) p.dimF * (size_t) p.dimT;

    // entrée [4][dimF][dimT] : canal0 réel, canal0 imaginaire, canal1 réel, canal1 imaginaire
    std::vector<float> input (4 * plane);
    const float* chans[2] = { l, r };
    std::vector<std::complex<float>> spec;
    for (int c = 0; c < 2; ++c)
    {
        int frames = 0;
        stft.forward (chans[c], chunk, spec, frames);
        float* re = input.data() + (size_t) (2 * c) * plane;
        float* im = re + plane;
        for (int f = 0; f < p.dimF; ++f)
            for (int t = 0; t < p.dimT; ++t)
            {
                const auto v = spec[(size_t) t * (size_t) nb + (size_t) f];
                re[(size_t) f * (size_t) p.dimT + (size_t) t] = v.real();
                im[(size_t) f * (size_t) p.dimT + (size_t) t] = v.imag();
            }
    }

    std::vector<float> pred;
    runNet (input, pred);

    if (denoise)
    {
        // moyenne de f(x) et -f(-x) : retire une partie du bruit propre au réseau
        std::vector<float> neg (input.size());
        for (size_t i = 0; i < input.size(); ++i) neg[i] = -input[i];
        std::vector<float> predNeg;
        runNet (neg, predNeg);
        for (size_t i = 0; i < pred.size(); ++i)
            pred[i] = 0.5f * (pred[i] - predNeg[i]);
    }

    float* outs[2] = { outL, outR };
    for (int c = 0; c < 2; ++c)
    {
        std::vector<std::complex<float>> s ((size_t) p.dimT * (size_t) nb, std::complex<float> (0.0f, 0.0f));
        const float* re = pred.data() + (size_t) (2 * c) * plane;
        const float* im = re + plane;
        for (int f = 0; f < p.dimF; ++f)
            for (int t = 0; t < p.dimT; ++t)
                s[(size_t) t * (size_t) nb + (size_t) f] = { re[(size_t) f * (size_t) p.dimT + (size_t) t],
                                                             im[(size_t) f * (size_t) p.dimT + (size_t) t] };
        stft.inverse (s, p.dimT, outs[c]);
    }
}

bool MdxModel::process (const StereoBuffer& in, StereoBuffer& out, bool denoise, const Progress& progress,
                        const std::vector<char>* active)
{
    if (session == nullptr)
        throw std::runtime_error ("Modèle non chargé.");

    const size_t n = in.size();
    const size_t chunk = (size_t) p.hop * (size_t) (p.dimT - 1);
    const size_t trim = (size_t) p.nfft / 2;
    const size_t gen = chunk - 2 * trim;
    const size_t pad = gen - n % gen;
    // le signal est vu comme [trim zéros][signal][pad zéros][trim zéros] ; chaque morceau est copié
    // à la volée (pas de copie complète du morceau en mémoire)
    out.resize (n);
    const size_t count = (n + pad) / gen;
    std::vector<float> il (chunk), ir (chunk), cl (chunk), cr (chunk);

    for (size_t k = 0; k < count; ++k)
    {
        if (progress && ! progress ((double) k / (double) count))
            return false;

        const size_t start = k * gen;
        if (active != nullptr && k < active->size() && ! (*active)[k])
            continue; // rien à séparer ici (sortie = silence)

        for (size_t i = 0; i < chunk; ++i)
        {
            const long src = (long) (start + i) - (long) trim;
            const bool inside = src >= 0 && src < (long) n;
            il[i] = inside ? in.l[(size_t) src] : 0.0f;
            ir[i] = inside ? in.r[(size_t) src] : 0.0f;
        }
        runChunk (il.data(), ir.data(), cl.data(), cr.data(), denoise);

        // on ne garde que le centre (les bords de chaque morceau sont moins fiables)
        for (size_t i = 0; i < gen; ++i)
        {
            const size_t dst = start + i;
            if (dst >= n) break;
            out.l[dst] = cl[trim + i] * p.compensate;
            out.r[dst] = cr[trim + i] * p.compensate;
        }
    }
    if (progress)
        progress (1.0);
    return true;
}

std::vector<char> MdxModel::chunkActivity (const StereoBuffer& ref, double thresholdDb) const
{
    // un morceau est « actif » si le signal de référence y dépasse le seuil (relatif au niveau moyen)
    const size_t n = ref.size();
    const size_t chunk = (size_t) p.hop * (size_t) (p.dimT - 1);
    const size_t trim = (size_t) p.nfft / 2;
    const size_t gen = chunk - 2 * trim;
    const size_t count = numChunks (n);
    double total = 0.0;
    for (size_t i = 0; i < n; ++i) total += (double) ref.l[i] * ref.l[i] + (double) ref.r[i] * ref.r[i];
    const double meanPow = total / (double) std::max<size_t> (1, n);
    const double thr = std::max (1.0e-9, meanPow * std::pow (10.0, thresholdDb / 10.0));

    std::vector<char> act (count, 0);
    for (size_t k = 0; k < count; ++k)
    {
        // zone utile du morceau + une marge de chaque côté
        const size_t a = k * gen > trim ? k * gen - trim : 0;
        const size_t b = std::min (n, k * gen + gen + trim);
        double e = 0.0;
        for (size_t i = a; i < b; ++i) e += (double) ref.l[i] * ref.l[i] + (double) ref.r[i] * ref.r[i];
        act[k] = b > a && e / (double) (b - a) > thr ? 1 : 0;
    }
    return act;
}

double MdxModel::timeOneChunk (const StereoBuffer& in)
{
    const size_t chunk = (size_t) p.hop * (size_t) (p.dimT - 1);
    std::vector<float> il (chunk, 0.0f), ir (chunk, 0.0f), cl (chunk), cr (chunk);
    for (size_t i = 0; i < chunk && i < in.size(); ++i) { il[i] = in.l[i]; ir[i] = in.r[i]; }
    runChunk (il.data(), ir.data(), cl.data(), cr.data(), false); // préchauffage (compilation GPU, etc.)
    const auto t0 = std::chrono::steady_clock::now();
    runChunk (il.data(), ir.data(), cl.data(), cr.data(), false);
    return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
}

} // namespace lsplit
