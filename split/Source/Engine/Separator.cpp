#include "Separator.h"
#include "DemucsModel.h"
#include "ChoirSplit.h"
#include "Dsp.h"
#include "LeadSplit.h"

#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
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
    const char* kDemucs = "htdemucs_6s.onnx";
    const char* kBasicPitch = "basic_pitch.onnx";

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

    void release (StereoBuffer& b)
    {
        std::vector<float>().swap (b.l);
        std::vector<float>().swap (b.r);
    }

    // Choix de l'appareil, une fois par modèle et par session : la carte graphique n'est gardée
    // que si elle est réellement plus rapide que le processeur (puces Intel intégrées souvent lentes).
    std::mutex gChoiceLock;
    std::map<std::string, Device> gChoice;
    std::map<std::string, std::string> gChoiceNote;

    void addDevice (std::string& deviceUsed, const std::string& dev)
    {
        if (deviceUsed.empty()) deviceUsed = dev;
        else if (deviceUsed.find (dev) == std::string::npos) deviceUsed += " + " + dev;
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
        case stemGuitar:  return "Guitares";
        case stemGuitarSolo: return "Guitare solo";
        case stemPiano:   return "Piano / claviers";
        case stemOther:   return "Autres";
        case stemSoprano: return "Soprano";
        case stemAlto:    return "Alto";
        case stemTenor:   return "Ténor";
        case stemBassVoice: return "Basse (voix)";
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
        case stemGuitar:  return "Guitares";
        case stemGuitarSolo: return "Guitare solo";
        case stemPiano:   return "Piano";
        case stemOther:   return "Instruments";
        case stemSoprano: return "Choeur - Soprano";
        case stemAlto:    return "Choeur - Alto";
        case stemTenor:   return "Choeur - Tenor";
        case stemBassVoice: return "Choeur - Basse";
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

bool Separator::checkModels (std::string& missing, const SeparationSettings& s) const
{
    missing.clear();
    std::vector<std::string> files { kDemucs, std::string (kDemucs) + ".json" };
    if (s.quality != Quality::fast) files.push_back (kVocals.file);
    if (s.quality == Quality::ultra) files.push_back (kInstHQ3.file);
    if (s.splitVocals)
    {
        files.push_back (kKaraoke.file);
        files.push_back (kBasicPitch);
    }
    for (const auto& f : files)
        if (! fileExists (modelsDir + f))
            missing += (missing.empty() ? "" : ", ") + f;
    return missing.empty();
}

std::string Separator::deviceReport()
{
    std::lock_guard<std::mutex> lock (gChoiceLock);
    std::string r;
    for (const auto& [k, v] : gChoiceNote)
        r += (r.empty() ? "" : " ; ") + v;
    return r;
}

bool Separator::runModel (const char* file, const MdxParams& params, const StereoBuffer& in, StereoBuffer& out,
                          bool denoise, bool gpu, const std::string& label, double from, double to,
                          const Progress& progress, std::string& deviceUsed, const std::vector<char>* active)
{
    const std::string path = modelsDir + file;
    auto report = [&] (double f) { return ! progress || progress (label, from + (to - from) * f); };
    if (! report (0.0))
        return false;

    MdxModel model;
    std::string err;
    Device dev = Device::cpu;
    if (gpu && OrtRuntime::directMLAvailable())
    {
        bool known = false;
        {
            std::lock_guard<std::mutex> lock (gChoiceLock);
            if (auto it = gChoice.find (file); it != gChoice.end()) { dev = it->second; known = true; }
        }
        if (! known)
        {
            // mesure : un morceau sur la carte graphique, un sur le processeur
            double tg = 1.0e9, tc = 1.0e9;
            try
            {
                if (model.load (path, params, Device::gpu, err)) tg = model.timeOneChunk (in);
            }
            catch (const std::exception&) { tg = 1.0e9; }
            MdxModel cpuModel;
            if (cpuModel.load (path, params, Device::cpu, err)) tc = cpuModel.timeOneChunk (in);
            dev = tg < 0.85 * tc ? Device::gpu : Device::cpu;
            std::lock_guard<std::mutex> lock (gChoiceLock);
            gChoice[file] = dev;
            char note[160];
            std::snprintf (note, sizeof (note), "%s : GPU %.1f s / processeur %.1f s par morceau", file,
                           tg > 1.0e8 ? -1.0 : tg, tc);
            gChoiceNote[file] = note;
            if (dev == Device::cpu) model = std::move (cpuModel);
        }
    }

    if (! model.isLoaded() || model.device() != dev)
        if (! model.load (path, params, dev, err))
        {
            if (dev == Device::gpu && model.load (path, params, Device::cpu, err))
                dev = Device::cpu;
            else
                throw std::runtime_error ("Chargement du modèle " + std::string (file) + " impossible : " + err);
        }

    try
    {
        if (! model.process (in, out, denoise, report, active))
            return false;
    }
    catch (const std::exception&)
    {
        if (dev != Device::gpu)
            throw;
        // la carte graphique a échoué en cours de route : on recommence sur le processeur
        dev = Device::cpu;
        {
            std::lock_guard<std::mutex> lock (gChoiceLock);
            gChoice[file] = Device::cpu;
        }
        if (! model.load (path, params, Device::cpu, err))
            throw std::runtime_error ("Chargement du modèle " + std::string (file) + " impossible : " + err);
        if (! model.process (in, out, denoise, report, active))
            return false;
    }
    addDevice (deviceUsed, dev == Device::gpu ? "GPU (DirectML)" : "Processeur");
    return true;
}

bool Separator::runDemucs (const StereoBuffer& in, std::vector<StereoBuffer>& out, std::vector<std::string>& names,
                           bool gpu, double overlap, double from, double to, const Progress& progress, std::string& deviceUsed,
                           bool keepArena)
{
    const std::string path = modelsDir + kDemucs;
    const std::string label = "Batterie, basse, guitares, piano";
    auto report = [&] (double f) { return ! progress || progress (label, from + (to - from) * f); };
    if (! report (0.0)) return false;

    DemucsModel model;
    std::string err;
    Device dev = Device::cpu;
    if (gpu && OrtRuntime::directMLAvailable())
    {
        bool known = false;
        {
            std::lock_guard<std::mutex> lock (gChoiceLock);
            if (auto it = gChoice.find (kDemucs); it != gChoice.end()) { dev = it->second; known = true; }
        }
        if (! known)
        {
            double tg = 1.0e9, tc = 1.0e9;
            try
            {
                if (model.load (path, Device::gpu, err, keepArena)) tg = model.timeOneSegment (in);
            }
            catch (const std::exception&) { tg = 1.0e9; }
            DemucsModel cpuModel;
            if (cpuModel.load (path, Device::cpu, err, keepArena)) tc = cpuModel.timeOneSegment (in);
            dev = tg < 0.85 * tc ? Device::gpu : Device::cpu;
            std::lock_guard<std::mutex> lock (gChoiceLock);
            gChoice[kDemucs] = dev;
            char note[160];
            std::snprintf (note, sizeof (note), "%s : GPU %.1f s / processeur %.1f s par segment", kDemucs,
                           tg > 1.0e8 ? -1.0 : tg, tc);
            gChoiceNote[kDemucs] = note;
        }
    }
    if (! model.isLoaded() || dev == Device::cpu)
        if (! model.load (path, dev, err, keepArena))
        {
            if (dev == Device::gpu && model.load (path, Device::cpu, err, keepArena))
                dev = Device::cpu;
            else
                throw std::runtime_error ("Chargement du modèle " + std::string (kDemucs) + " impossible : " + err);
        }

    try
    {
        if (! model.process (in, out, report, overlap)) return false;
    }
    catch (const std::exception&)
    {
        if (dev != Device::gpu) throw;
        dev = Device::cpu;
        {
            std::lock_guard<std::mutex> lock (gChoiceLock);
            gChoice[kDemucs] = Device::cpu;
        }
        if (! model.load (path, Device::cpu, err, keepArena))
            throw std::runtime_error ("Chargement du modèle " + std::string (kDemucs) + " impossible : " + err);
        if (! model.process (in, out, report, overlap)) return false;
    }
    names = model.sources();
    addDevice (deviceUsed, dev == Device::gpu ? "GPU (DirectML)" : "Processeur");
    return true;
}

bool Separator::run (const StereoBuffer& mixIn, double sr, const SeparationSettings& s,
                     StemSet& result, const Progress& progress, const Partial& onPartial)
{
    const size_t n = mixIn.size();
    if (n == 0)
        throw std::runtime_error ("Le fichier audio est vide.");

    std::string missing;
    if (! checkModels (missing, s))
        throw std::runtime_error ("Modèles introuvables : " + missing);

    result = StemSet();
    result.sampleRate = sr;

    const bool needResample = std::abs (sr - modelRate) > 0.5;
    StereoBuffer mixResampled;
    if (needResample) mixResampled = resampled (mixIn, sr, modelRate);
    const StereoBuffer& mix = needResample ? mixResampled : mixIn;
    const size_t m = mix.size();

    const bool fast = s.quality == Quality::fast;
    const bool ultra = s.quality == Quality::ultra;
    const bool detail = s.detailInstruments;

    // progression : poids estimés de chaque étape
    const double wDem = fast ? 0.8 : 1.0;
    const double wVoc = fast ? 0.0 : (ultra ? 5.0 : 1.2);
    const double wKar = s.splitVocals ? (ultra ? 1.6 : 0.8) : 0.0;
    const double wChoir = s.splitVocals ? 0.1 : 0.0;
    const double wSolo = detail && s.splitGuitarSolo ? 0.1 : 0.0;
    const double wTotal = wDem + wVoc + wKar + wChoir + wSolo + 0.05;
    double pos = 0.0;
    auto span = [&] (double w) { const double a = pos / wTotal; pos += w; return std::make_pair (a, pos / wTotal); };

    // 1. Demucs sur le morceau entier : batterie, basse, guitares, piano, autres… et repérage du chant
    StereoBuffer dVocals, drums, bass, guitar, piano;
    {
        auto [a, b] = span (wDem);
        std::vector<StereoBuffer> src;
        std::vector<std::string> names;
        if (! runDemucs (mix, src, names, s.useGpu, fast ? 0.1 : 0.25, a, b, progress, result.deviceUsed, ! s.lowMemory))
            return false;
        auto take = [&] (const char* name, StereoBuffer& dst)
        {
            for (size_t i = 0; i < names.size(); ++i)
                if (names[i] == name) { dst = std::move (src[i]); fitLength (dst, m); return true; }
            return false;
        };
        if (! take ("vocals", dVocals) || ! take ("drums", drums) || ! take ("bass", bass)
            || ! take ("guitar", guitar) || ! take ("piano", piano))
            throw std::runtime_error ("Le modèle Demucs ne contient pas les sources attendues.");
    }
    if (! s.splitDrumsBass) { release (drums); release (bass); }
    if (! detail) { release (guitar); release (piano); }

    // résultat provisoire : les instruments sont prêts, la voix est celle de Demucs (affinée ensuite)
    if (onPartial && ! fast && ! s.lowMemory)
    {
        StemSet part;
        part.sampleRate = modelRate;
        auto put = [&] (int id, const StereoBuffer& b) { if (! b.l.empty()) { part.stems[id] = b; part.present[id] = true; } };
        put (stemLead, dVocals);
        put (stemDrums, drums);
        put (stemBass, bass);
        put (stemGuitar, guitar);
        put (stemPiano, piano);
        StereoBuffer other = mix;
        for (int k = 0; k < stemOther; ++k)
            if (part.present[k])
                for (size_t i = 0; i < m; ++i) { other.l[i] -= part.stems[k].l[i]; other.r[i] -= part.stems[k].r[i]; }
        part.stems[stemOther] = std::move (other);
        part.present[stemOther] = true;
        part.deviceUsed = result.deviceUsed;
        onPartial (part);
    }

    // 2. voix
    StereoBuffer vocals;
    if (fast)
    {
        vocals = std::move (dVocals);
    }
    else if (ultra)
    {
        // deux modèles complets avec double passage anti-bruit, moyennés
        auto [a, b] = span (wVoc * 0.5);
        if (! runModel (kVocals.file, kVocals.params, mix, vocals, true, s.useGpu,
                        "Voix (modèle 1/2)", a, b, progress, result.deviceUsed))
            return false;
        StereoBuffer inst;
        auto [c, d] = span (wVoc * 0.5);
        if (! runModel (kInstHQ3.file, kInstHQ3.params, mix, inst, true, s.useGpu,
                        "Voix (modèle 2/2)", c, d, progress, result.deviceUsed))
            return false;
        for (size_t i = 0; i < m; ++i)
        {
            vocals.l[i] = 0.5f * (vocals.l[i] + (mix.l[i] - inst.l[i]));
            vocals.r[i] = 0.5f * (vocals.r[i] + (mix.r[i] - inst.r[i]));
        }
        release (dVocals);
    }
    else
    {
        // modèle de voix de haute qualité, seulement là où Demucs a entendu du chant
        auto [a, b] = span (wVoc);
        const size_t chunk = (size_t) kVocals.params.hop * (size_t) (kVocals.params.dimT - 1);
        const size_t gen = chunk - (size_t) kVocals.params.nfft;
        // activité par morceau (même découpage que le modèle)
        std::vector<char> active;
        {
            const size_t count = (m + (gen - m % gen)) / gen;
            double total = 0.0;
            for (size_t i = 0; i < m; ++i) total += (double) dVocals.l[i] * dVocals.l[i] + (double) dVocals.r[i] * dVocals.r[i];
            const double thr = std::max (1.0e-10, total / (double) m * 1.0e-3); // 30 dB sous le niveau moyen du chant
            active.assign (count, 0);
            const size_t trim = (size_t) kVocals.params.nfft / 2;
            for (size_t k = 0; k < count; ++k)
            {
                const size_t s0 = k * gen > trim ? k * gen - trim : 0, s1 = std::min (m, k * gen + gen + trim);
                double en = 0.0;
                for (size_t i = s0; i < s1; ++i) en += (double) dVocals.l[i] * dVocals.l[i] + (double) dVocals.r[i] * dVocals.r[i];
                active[k] = s1 > s0 && en / (double) (s1 - s0) > thr ? 1 : 0;
            }
        }
        if (! runModel (kVocals.file, kVocals.params, mix, vocals, false, s.useGpu,
                        "Voix (haute qualité)", a, b, progress, result.deviceUsed, &active))
            return false;
        // morceaux sans chant : la (très faible) voix de Demucs
        for (size_t k = 0; k < active.size(); ++k)
            if (! active[k])
                for (size_t i = k * gen; i < std::min (m, (k + 1) * gen); ++i)
                {
                    vocals.l[i] = dVocals.l[i];
                    vocals.r[i] = dVocals.r[i];
                }
        release (dVocals);
    }

    // 3. voix lead / chœurs : le modèle karaoké garde les chœurs (seulement là où on chante)
    StereoBuffer lead, backing;
    StereoBuffer choir[numVoices];
    if (s.splitVocals)
    {
        auto [a, b] = span (wKar);
        std::vector<char> active;
        {
            const size_t chunk = (size_t) kKaraoke.params.hop * (size_t) (kKaraoke.params.dimT - 1);
            const size_t gen = chunk - (size_t) kKaraoke.params.nfft;
            const size_t trim = (size_t) kKaraoke.params.nfft / 2;
            const size_t count = (m + (gen - m % gen)) / gen;
            double total = 0.0;
            for (size_t i = 0; i < m; ++i) total += (double) vocals.l[i] * vocals.l[i] + (double) vocals.r[i] * vocals.r[i];
            const double thr = std::max (1.0e-10, total / (double) m * 1.0e-3);
            active.assign (count, 0);
            for (size_t k = 0; k < count; ++k)
            {
                const size_t s0 = k * gen > trim ? k * gen - trim : 0, s1 = std::min (m, k * gen + gen + trim);
                double en = 0.0;
                for (size_t i = s0; i < s1; ++i) en += (double) vocals.l[i] * vocals.l[i] + (double) vocals.r[i] * vocals.r[i];
                active[k] = s1 > s0 && en / (double) (s1 - s0) > thr ? 1 : 0;
            }
        }
        if (! runModel (kKaraoke.file, kKaraoke.params, vocals, backing, ultra, s.useGpu,
                        "Voix lead / chœurs", a, b, progress, result.deviceUsed, &active))
            return false;
        lead.resize (m);
        for (size_t i = 0; i < m; ++i)
        {
            lead.l[i] = vocals.l[i] - backing.l[i];
            lead.r[i] = vocals.r[i] - backing.r[i];
        }
        release (vocals);

        auto [c, d] = span (wChoir);
        if (! splitChoir (backing, modelRate, choir, modelsDir + kBasicPitch,
                          [&] (double f) { return ! progress || progress ("Pupitres du chœur", c + (d - c) * f); }))
            return false;
    }
    else
    {
        lead = std::move (vocals);
    }

    // 4. guitare solo / rythmique
    StereoBuffer guitarSolo;
    if (detail && s.splitGuitarSolo)
    {
        auto [c, d] = span (wSolo);
        if (! splitLead (guitar, modelRate, guitarSolo,
                         [&] (double f) { return ! progress || progress ("Guitare solo", c + (d - c) * f); }))
            return false;
        for (size_t i = 0; i < m; ++i)
        {
            guitar.l[i] -= guitarSolo.l[i];
            guitar.r[i] -= guitarSolo.r[i];
        }
    }

    if (progress && ! progress ("Finalisation", pos / wTotal))
        return false;

    // 5. retour à la fréquence d'origine ; « Autres » = original − tout le reste (somme exacte)
    auto store = [&] (int id, StereoBuffer& b)
    {
        if (b.l.empty()) return;
        if (needResample) b = resampled (b, modelRate, sr);
        fitLength (b, n);
        result.stems[id] = std::move (b);
        result.present[id] = true;
    };
    store (stemLead, lead);
    store (stemBacking, backing);
    store (stemDrums, drums);
    store (stemBass, bass);
    store (stemGuitar, guitar);
    store (stemGuitarSolo, guitarSolo);
    store (stemPiano, piano);
    mixResampled = StereoBuffer();

    if (result.present[stemBacking])
    {
        // pupitres : la basse vocale par soustraction, pour que leur somme = Chœurs exactement
        const int ids[numVoices] = { stemSoprano, stemAlto, stemTenor, stemBassVoice };
        for (int v = 0; v < numVoices - 1; ++v)
            store (ids[v], choir[v]);
        StereoBuffer last = result.stems[stemBacking];
        for (int v = 0; v < numVoices - 1; ++v)
            for (size_t i = 0; i < n; ++i)
            {
                last.l[i] -= result.stems[ids[v]].l[i];
                last.r[i] -= result.stems[ids[v]].r[i];
            }
        result.stems[stemBassVoice] = std::move (last);
        result.present[stemBassVoice] = true;
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
