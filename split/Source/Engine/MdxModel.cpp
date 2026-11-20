#include "MdxModel.h"
#include "Dsp.h"

#include <algorithm>
#include <array>
#include <stdexcept>
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
        opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        opts.SetIntraOpNumThreads ((int) std::max (1u, std::thread::hardware_concurrency()));

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

bool MdxModel::process (const StereoBuffer& in, StereoBuffer& out, bool denoise, const Progress& progress)
{
    if (session == nullptr)
        throw std::runtime_error ("Modèle non chargé.");

    const size_t n = in.size();
    const size_t chunk = (size_t) p.hop * (size_t) (p.dimT - 1);
    const size_t trim = (size_t) p.nfft / 2;
    const size_t gen = chunk - 2 * trim;
    const size_t pad = gen - n % gen;
    const size_t total = trim + n + pad + trim;

    // signal complété de zéros : [trim][signal][pad][trim]
    std::vector<float> ml (total, 0.0f), mr (total, 0.0f);
    std::copy (in.l.begin(), in.l.end(), ml.begin() + (long) trim);
    std::copy (in.r.begin(), in.r.end(), mr.begin() + (long) trim);

    out.resize (n);
    const size_t count = (n + pad) / gen;
    std::vector<float> cl (chunk), cr (chunk);

    for (size_t k = 0; k < count; ++k)
    {
        if (progress && ! progress ((double) k / (double) count))
            return false;

        const size_t start = k * gen;
        runChunk (ml.data() + start, mr.data() + start, cl.data(), cr.data(), denoise);

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

} // namespace lsplit
