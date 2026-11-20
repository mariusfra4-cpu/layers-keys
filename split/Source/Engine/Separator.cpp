#include "Separator.h"
#include "Dsp.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace lsplit
{

namespace
{
    struct ModelDef
    {
        const char* file;
        MdxParams params;
    };

    // Paramètres officiels de chaque modèle (n_fft, dim_f, dim_t, compensation).
    const ModelDef kVocals   { "Kim_Vocal_2.onnx",           { 7680,  3072, 256, 1.009f } };
    const ModelDef kInstHQ3  { "UVR-MDX-NET-Inst_HQ_3.onnx", { 6144,  3072, 256, 1.022f } };
    const ModelDef kKaraoke  { "UVR_MDXNET_KARA_2.onnx",     { 5120,  2048, 256, 1.065f } };
    const ModelDef kDrums    { "kuielab_a_drums.onnx",       { 4096,  2048, 512, 1.035f } };
    const ModelDef kBass     { "kuielab_a_bass.onnx",        { 16384, 2048, 512, 1.035f } };

    bool fileExists (const std::string& path)
    {
       #if defined(_WIN32)
        std::ifstream f (std::filesystem::u8path (path), std::ios::binary);
       #else
        std::ifstream f (path, std::ios::binary);
       #endif
        return f.good();
    }

    StereoBuffer resampled (const StereoBuffer& in, double from, double to)
    {
        StereoBuffer out;
        out.l = resample (in.l.data(), in.size(), from, to);
        out.r = resample (in.r.data(), in.size(), from, to);
        return out;
    }

    void fitLength (StereoBuffer& b, size_t n)
    {
        b.l.resize (n, 0.0f);
        b.r.resize (n, 0.0f);
    }
}

const char* stemName (int id)
{
    switch (id)
    {
        case stemLead:    return "Voix lead";
        case stemBacking: return "Chœurs";
        case stemDrums:   return "Batterie";
        case stemBass:    return "Basse";
        case stemOther:   return "Instruments";
        default:          return "?";
    }
}

const char* stemFileName (int id)
{
    switch (id)
    {
        case stemLead:    return "Voix lead";
        case stemBacking: return "Choeurs";
        case stemDrums:   return "Batterie";
        case stemBass:    return "Basse";
        case stemOther:   return "Instruments";
        default:          return "Stem";
    }
}

size_t StemSet::numSamples() const
{
    for (int i = 0; i < numStems; ++i)
        if (present[i]) return stems[i].size();
    return 0;
}

Separator::Separator (std::string dir) : modelsDir (std::move (dir))
{
    if (! modelsDir.empty() && modelsDir.back() != '/' && modelsDir.back() != '\\')
        modelsDir += '/';
}

bool Separator::checkModels (std::string& missing) const
{
    missing.clear();
    for (const auto* m : { &kVocals, &kInstHQ3, &kKaraoke, &kDrums, &kBass })
        if (! fileExists (modelsDir + m->file))
            missing += (missing.empty() ? "" : ", ") + std::string (m->file);
    return missing.empty();
}

bool Separator::runModel (const char* file, const MdxParams& params, const StereoBuffer& in, StereoBuffer& out,
                          bool denoise, bool gpu, const std::string& label, double from, double to,
                          const Progress& progress, std::string& deviceUsed)
{
    const std::string path = modelsDir + file;
    auto report = [&] (double f) { return ! progress || progress (label, from + (to - from) * f); };

    if (! report (0.0))
        return false;

    MdxModel model;
    std::string err;
    bool onGpu = false;

    if (gpu && OrtRuntime::directMLAvailable())
        onGpu = model.load (path, params, Device::gpu, err);

    if (! onGpu && ! model.load (path, params, Device::cpu, err))
        throw std::runtime_error ("Chargement du modèle " + std::string (file) + " impossible : " + err);

    try
    {
        if (! model.process (in, out, denoise, report))
            return false;
    }
    catch (const std::exception&)
    {
        if (! onGpu)
            throw;

        // La carte graphique a échoué en cours de route : on recommence sur le processeur.
        onGpu = false;
        if (! model.load (path, params, Device::cpu, err))
            throw std::runtime_error ("Chargement du modèle " + std::string (file) + " impossible : " + err);
        if (! model.process (in, out, denoise, report))
            return false;
    }

    const std::string dev = onGpu ? "GPU (DirectML)" : "Processeur";
    if (deviceUsed.empty())
        deviceUsed = dev;
    else if (deviceUsed.find (dev) == std::string::npos)
        deviceUsed += " + " + dev;
    return true;
}

bool Separator::run (const StereoBuffer& mixIn, double sr, const SeparationSettings& s,
                     StemSet& result, const Progress& progress)
{
    const size_t n = mixIn.size();
    if (n == 0)
        throw std::runtime_error ("Le fichier audio est vide.");

    std::string missing;
    if (! checkModels (missing))
        throw std::runtime_error ("Modèles introuvables : " + missing);

    result = StemSet();
    result.sampleRate = sr;

    const bool needResample = std::abs (sr - modelRate) > 0.5;
    const StereoBuffer mix = needResample ? resampled (mixIn, sr, modelRate) : mixIn;
    const size_t m = mix.size();

    const bool fast = s.quality == Quality::fast;
    const bool ultra = s.quality == Quality::ultra;

    // répartition de la barre de progression selon le coût estimé de chaque étape
    double wVoc = ultra ? 4.0 : (fast ? 1.0 : 2.0);
    double wKar = s.splitVocals ? (fast ? 0.7 : 1.4) : 0.0;
    double wDB  = s.splitDrumsBass ? (ultra ? 2.4 : 1.2) : 0.0;
    const double wTotal = wVoc + wKar + wDB + 0.1;
    double pos = 0.0;
    auto span = [&] (double w) { const double a = pos / wTotal; pos += w; return std::make_pair (a, pos / wTotal); };

    // 1. voix
    StereoBuffer vocals;
    {
        if (ultra)
        {
            auto [a, b] = span (wVoc * 0.5);
            if (! runModel (kVocals.file, kVocals.params, mix, vocals, true, s.useGpu,
                            "Voix (modèle 1/2)", a, b, progress, result.deviceUsed))
                return false;

            StereoBuffer inst;
            auto [c, d] = span (wVoc * 0.5);
            if (! runModel (kInstHQ3.file, kInstHQ3.params, mix, inst, true, s.useGpu,
                            "Voix (modèle 2/2)", c, d, progress, result.deviceUsed))
                return false;

            // ensemble : moyenne des deux estimations des voix
            for (size_t i = 0; i < m; ++i)
            {
                vocals.l[i] = 0.5f * (vocals.l[i] + (mix.l[i] - inst.l[i]));
                vocals.r[i] = 0.5f * (vocals.r[i] + (mix.r[i] - inst.r[i]));
            }
        }
        else
        {
            auto [a, b] = span (wVoc);
            if (! runModel (kVocals.file, kVocals.params, mix, vocals, ! fast, s.useGpu,
                            "Séparation des voix", a, b, progress, result.deviceUsed))
                return false;
        }
    }

    StereoBuffer inst;
    inst.resize (m);
    for (size_t i = 0; i < m; ++i)
    {
        inst.l[i] = mix.l[i] - vocals.l[i];
        inst.r[i] = mix.r[i] - vocals.r[i];
    }

    // 2. voix lead / chœurs : le modèle karaoké garde les chœurs et retire la voix principale
    StereoBuffer lead, backing;
    if (s.splitVocals)
    {
        auto [a, b] = span (wKar);
        if (! runModel (kKaraoke.file, kKaraoke.params, vocals, backing, ! fast, s.useGpu,
                        "Voix lead / chœurs", a, b, progress, result.deviceUsed))
            return false;
        lead.resize (m);
        for (size_t i = 0; i < m; ++i)
        {
            lead.l[i] = vocals.l[i] - backing.l[i];
            lead.r[i] = vocals.r[i] - backing.r[i];
        }
    }
    else
    {
        lead = vocals;
    }

    // 3. batterie et basse, extraites de l'instrumental
    StereoBuffer drums, bass;
    if (s.splitDrumsBass)
    {
        auto [a, b] = span (wDB * 0.5);
        if (! runModel (kDrums.file, kDrums.params, inst, drums, ultra, s.useGpu,
                        "Batterie", a, b, progress, result.deviceUsed))
            return false;
        auto [c, d] = span (wDB * 0.5);
        if (! runModel (kBass.file, kBass.params, inst, bass, ultra, s.useGpu,
                        "Basse", c, d, progress, result.deviceUsed))
            return false;
    }

    if (progress && ! progress ("Finalisation", pos / wTotal))
        return false;

    // retour à la fréquence d'origine, puis « Instruments » = mix − tout le reste (somme exacte)
    auto back = [&] (StereoBuffer& b)
    {
        if (needResample) b = resampled (b, modelRate, sr);
        fitLength (b, n);
    };

    back (lead);
    result.stems[stemLead] = std::move (lead);
    result.present[stemLead] = true;

    if (s.splitVocals)
    {
        back (backing);
        result.stems[stemBacking] = std::move (backing);
        result.present[stemBacking] = true;
    }
    if (s.splitDrumsBass)
    {
        back (drums);
        back (bass);
        result.stems[stemDrums] = std::move (drums);
        result.stems[stemBass] = std::move (bass);
        result.present[stemDrums] = result.present[stemBass] = true;
    }

    StereoBuffer other;
    other.l = mixIn.l;
    other.r = mixIn.r;
    for (int k = 0; k < stemOther; ++k)
    {
        if (! result.present[k]) continue;
        const auto& st = result.stems[k];
        for (size_t i = 0; i < n; ++i)
        {
            other.l[i] -= st.l[i];
            other.r[i] -= st.r[i];
        }
    }
    result.stems[stemOther] = std::move (other);
    result.present[stemOther] = true;

    if (progress)
        progress ("Terminé", 1.0);
    return true;
}

} // namespace lsplit
