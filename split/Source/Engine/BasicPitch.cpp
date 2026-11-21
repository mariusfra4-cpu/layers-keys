#include "BasicPitch.h"
#include "Dsp.h"

#include <stdexcept>

namespace lsplit
{

void BasicPitchFrames::compute (const StereoBuffer& in, double sr, const std::string& path)
{
    constexpr int kWin = 43844;           // 2 s - 256 échantillons, comme Basic Pitch
    constexpr int kOverlapFrames = 30;
    constexpr int kOverlap = kOverlapFrames * 256;
    constexpr int kHopWin = kWin - kOverlap;
    constexpr int kFramesPerWin = 172;

    std::vector<float> mono (in.size());
    for (size_t i = 0; i < in.size(); ++i) mono[i] = 0.5f * (in.l[i] + in.r[i]);
    std::vector<float> x = resample (mono.data(), mono.size(), sr, 22050.0);
    const size_t origLen = x.size();
    x.insert (x.begin(), (size_t) (kOverlap / 2), 0.0f);

    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads (2);
    Ort::Session session (OrtRuntime::env(), toOrtPath (path).c_str(), opts);
    Ort::AllocatorWithDefaultOptions alloc;
    const std::string inName = session.GetInputNameAllocated (0, alloc).get();
    std::vector<std::string> outNames;
    for (size_t i = 0; i < session.GetOutputCount(); ++i)
        outNames.push_back (session.GetOutputNameAllocated (i, alloc).get());

    numFrames = (size_t) ((double) origLen / 256.0) + 1;
    notes.assign (numFrames * 88, 0.0f);
    contours.assign (numFrames * 264, 0.0f);

    auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<float> win (kWin);
    size_t outFrame = 0;
    for (size_t start = 0; start < x.size() && outFrame < numFrames; start += kHopWin)
    {
        for (int i = 0; i < kWin; ++i)
            win[(size_t) i] = start + (size_t) i < x.size() ? x[start + (size_t) i] : 0.0f;
        const std::array<int64_t, 3> shape { 1, kWin, 1 };
        Ort::Value t = Ort::Value::CreateTensor<float> (mem, win.data(), win.size(), shape.data(), 3);
        const char* inN[] = { inName.c_str() };
        std::vector<const char*> outN;
        for (auto& s : outNames) outN.push_back (s.c_str());
        auto res = session.Run (Ort::RunOptions { nullptr }, inN, &t, 1, outN.data(), outN.size());

        // sorties identifiées par leur taille : contour = 264 colonnes ; notes = la sortie 88 la plus « nette »
        const float* cont = nullptr;
        const float* note = nullptr;
        for (size_t i = 0; i < res.size(); ++i)
        {
            const auto sh = res[i].GetTensorTypeAndShapeInfo().GetShape();
            if (sh.size() != 3 || sh[1] != kFramesPerWin) continue;
            if (sh[2] == 264) cont = res[i].GetTensorData<float>();
            // « StatefulPartitionedCall:1 » = activations des notes ; « :2 » = attaques
            else if (sh[2] == 88 && outNames[i].find (":1") != std::string::npos) note = res[i].GetTensorData<float>();
        }
        if (cont == nullptr || note == nullptr)
            throw std::runtime_error ("Modèle Basic Pitch inattendu.");

        for (int f = kOverlapFrames / 2; f < kFramesPerWin - kOverlapFrames / 2 && outFrame < numFrames; ++f, ++outFrame)
        {
            std::copy (note + (size_t) f * 88, note + (size_t) f * 88 + 88, notes.begin() + (long) (outFrame * 88));
            std::copy (cont + (size_t) f * 264, cont + (size_t) f * 264 + 264, contours.begin() + (long) (outFrame * 264));
        }
    }
}

} // namespace lsplit
