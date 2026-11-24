#include "DemucsModel.h"
#include "Dsp.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace lsplit
{

namespace
{
    std::string readText (const std::string& path)
    {
       #if defined(_WIN32)
        std::ifstream f (std::filesystem::u8path (path), std::ios::binary);
       #else
        std::ifstream f (path, std::ios::binary);
       #endif
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    int jsonInt (const std::string& s, const char* key)
    {
        std::smatch m;
        if (std::regex_search (s, m, std::regex (std::string ("\"") + key + "\"\\s*:\\s*([0-9]+)")))
            return std::stoi (m[1].str());
        return -1;
    }

    // remplissage « reflect » (sans répéter l'échantillon du bord), comme torch
    float reflectAt (const float* x, int n, int i)
    {
        if (n <= 1) return n == 1 ? x[0] : 0.0f;
        const int period = 2 * (n - 1);
        i = ((i % period) + period) % period;
        if (i >= n) i = period - i;
        return x[i];
    }
}

bool DemucsModel::load (const std::string& path, Device device, std::string& error, bool keepArena)
{
    session.reset();
    dev = device;
    names.clear();

    const std::string info = readText (path + ".json");
    segment = jsonInt (info, "segment_samples");
    frames = jsonInt (info, "frames");
    nfft = jsonInt (info, "nfft");
    hop = jsonInt (info, "hop");
    {
        std::smatch m;
        if (std::regex_search (info, m, std::regex ("\"sources\"\\s*:\\s*\\[([^\\]]*)\\]")))
        {
            const std::string list = m[1].str();
            std::regex item ("\"([a-z_]+)\"");
            for (auto it = std::sregex_iterator (list.begin(), list.end(), item); it != std::sregex_iterator(); ++it)
                names.push_back ((*it)[1].str());
        }
    }
    if (segment <= 0 || frames <= 0 || nfft != 4096 || hop != 1024 || names.empty()
        || frames != (segment + hop - 1) / hop)
    {
        error = "Réglages du modèle Demucs illisibles : " + path + ".json";
        return false;
    }

    try
    {
        Ort::SessionOptions opts;
        OrtRuntime::configure (opts, keepArena);
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
    }
    catch (const std::exception& e)
    {
        error = e.what();
        session.reset();
        return false;
    }
    return true;
}

int DemucsModel::sourceIndex (const std::string& name) const
{
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return (int) i;
    return -1;
}

// Un segment de « segment » échantillons commençant à « start » (zéros hors du morceau).
void DemucsModel::runSegment (const StereoBuffer& in, long start, std::vector<std::vector<float>>& out)
{
    const int L = segment, T = frames, F = nfft / 2;
    const int S = (int) names.size();
    const int pad = hop / 2 * 3;
    const float norm = 1.0f / std::sqrt ((float) nfft);

    // signal du segment, complété de zéros
    const int length = L;
    std::vector<float> mix ((size_t) 2 * (size_t) L, 0.0f);
    for (int i = 0; i < L; ++i)
    {
        const long j = start + i;
        if (j < 0 || j >= (long) in.size()) continue;
        mix[(size_t) i] = in.l[(size_t) j];
        mix[(size_t) (L + i)] = in.r[(size_t) j];
    }

    // spectre, exactement comme HTDemucs._spec
    Stft stft (nfft, hop);
    const int paddedLen = T * hop + 2 * pad;        // longueur après remplissage reflect
    const size_t plane = (size_t) F * (size_t) T;
    std::vector<float> mag (4 * plane);
    std::vector<float> xp ((size_t) paddedLen);
    std::vector<std::complex<float>> spec;
    for (int c = 0; c < 2; ++c)
    {
        const float* x = mix.data() + (size_t) c * (size_t) L;
        for (int i = 0; i < paddedLen; ++i)
            xp[(size_t) i] = reflectAt (x, L, i - pad);
        int fr = 0;
        stft.forward (xp.data(), paddedLen, spec, fr); // fr = T + 4
        const int nb = stft.bins();
        float* re = mag.data() + (size_t) (2 * c) * plane;
        float* im = re + plane;
        for (int f = 0; f < F; ++f)
            for (int t = 0; t < T; ++t)
            {
                const auto v = spec[(size_t) (t + 2) * (size_t) nb + (size_t) f] * norm;
                re[(size_t) f * (size_t) T + (size_t) t] = v.real();
                im[(size_t) f * (size_t) T + (size_t) t] = v.imag();
            }
    }

    // réseau
    auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 3> mixShape { 1, 2, L };
    const std::array<int64_t, 4> magShape { 1, 4, F, T };
    std::array<Ort::Value, 2> inputs { Ort::Value::CreateTensor<float> (memInfo, mix.data(), mix.size(), mixShape.data(), 3),
                                       Ort::Value::CreateTensor<float> (memInfo, mag.data(), mag.size(), magShape.data(), 4) };
    const char* inNames[] = { "mix", "mag" };
    const char* outNames[] = { "spec", "wave" };
    auto res = session->Run (Ort::RunOptions { nullptr }, inNames, inputs.data(), 2, outNames, 2);
    if (res.size() != 2
        || res[0].GetTensorTypeAndShapeInfo().GetElementCount() != (size_t) S * 4 * plane
        || res[1].GetTensorTypeAndShapeInfo().GetElementCount() != (size_t) S * 2 * (size_t) L)
        throw std::runtime_error ("Sortie du modèle Demucs de taille inattendue.");
    const float* zs = res[0].GetTensorData<float>();
    const float* wv = res[1].GetTensorData<float>();

    // iSTFT (HTDemucs._ispec) + branche temporelle
    out.assign ((size_t) S * 2, std::vector<float> ((size_t) length));
    const int nb = stft.bins();
    const int fr = T + 4;
    std::vector<std::complex<float>> s ((size_t) fr * (size_t) nb);
    std::vector<float> y ((size_t) hop * (size_t) (fr - 1));
    const float inv = std::sqrt ((float) nfft);
    for (int src = 0; src < S; ++src)
        for (int c = 0; c < 2; ++c)
        {
            std::fill (s.begin(), s.end(), std::complex<float> (0.0f, 0.0f));
            const float* re = zs + ((size_t) src * 4 + (size_t) (2 * c)) * plane;
            const float* im = re + plane;
            for (int f = 0; f < F; ++f)
                for (int t = 0; t < T; ++t)
                    s[(size_t) (t + 2) * (size_t) nb + (size_t) f] = { re[(size_t) f * (size_t) T + (size_t) t] * inv,
                                                                       im[(size_t) f * (size_t) T + (size_t) t] * inv };
            stft.inverse (s, fr, y.data());
            const float* w = wv + ((size_t) src * 2 + (size_t) c) * (size_t) L;
            auto& o = out[(size_t) src * 2 + (size_t) c];
            for (int i = 0; i < length; ++i)
                o[(size_t) i] = y[(size_t) (pad + i)] + w[i];
        }
}

bool DemucsModel::process (const StereoBuffer& in, std::vector<StereoBuffer>& out, const Progress& progress,
                           double overlap)
{
    if (session == nullptr)
        throw std::runtime_error ("Modèle Demucs non chargé.");

    const size_t n = in.size();
    const int S = (int) names.size();

    // normalisation globale (comme demucs.separate)
    double sum = 0.0, sum2 = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double m = 0.5 * ((double) in.l[i] + (double) in.r[i]);
        sum += m;
        sum2 += m * m;
    }
    const double mean = n > 0 ? sum / (double) n : 0.0;
    const double var = n > 1 ? (sum2 - (double) n * mean * mean) / (double) (n - 1) : 0.0;
    const float stdv = (float) std::max (1.0e-8, std::sqrt (std::max (0.0, var)));
    const float meanf = (float) mean;

    StereoBuffer x;
    x.resize (n);
    for (size_t i = 0; i < n; ++i)
    {
        x.l[i] = (in.l[i] - meanf) / stdv;
        x.r[i] = (in.r[i] - meanf) / stdv;
    }

    // segments recouvrants avec fondu triangulaire (demucs.apply) ; accumulation directe dans « out »
    const size_t segLen = (size_t) segment;
    const size_t stride = std::max<size_t> (1, (size_t) ((1.0 - std::clamp (overlap, 0.0, 0.5)) * (double) segLen));
    std::vector<float> weight (segLen);
    {
        const size_t half = segLen / 2;
        for (size_t i = 0; i < half; ++i) weight[i] = (float) (i + 1);
        for (size_t i = half; i < segLen; ++i) weight[i] = (float) (segLen - i);
        const float mx = *std::max_element (weight.begin(), weight.end());
        for (auto& w : weight) w /= mx;
    }

    out.assign ((size_t) S, StereoBuffer());
    for (auto& o : out) o.resize (n);
    std::vector<float> wsum (n, 0.0f);
    const size_t count = n == 0 ? 0 : (n + stride - 1) / stride;
    std::vector<std::vector<float>> seg;

    size_t k = 0;
    for (size_t off = 0; off < n; off += stride, ++k)
    {
        if (progress && ! progress ((double) k / (double) count))
            return false;
        const int len = (int) std::min (segLen, n - off);
        // segment plus court (fin du morceau) : centré, avec la musique qui précède (demucs TensorChunk.padded)
        const long delta = (long) segLen - len;
        runSegment (x, (long) off - delta / 2, seg);
        for (int src = 0; src < S; ++src)
            for (int c = 0; c < 2; ++c)
            {
                float* a = (c == 0 ? out[(size_t) src].l : out[(size_t) src].r).data() + off;
                const float* sg = seg[(size_t) (src * 2 + c)].data() + delta / 2;
                for (int i = 0; i < len; ++i)
                    a[i] += weight[(size_t) i] * sg[i];
            }
        for (int i = 0; i < len; ++i)
            wsum[off + (size_t) i] += weight[(size_t) i];
    }
    StereoBuffer().l.swap (x.l); // libère la copie normalisée au plus tôt
    x = StereoBuffer();

    for (auto& o : out)
        for (size_t i = 0; i < n; ++i)
        {
            const float w = wsum[i] > 0.0f ? wsum[i] : 1.0f;
            o.l[i] = o.l[i] / w * stdv + meanf;
            o.r[i] = o.r[i] / w * stdv + meanf;
        }
    if (progress)
        progress (1.0);
    return true;
}

double DemucsModel::timeOneSegment (const StereoBuffer& in)
{
    std::vector<std::vector<float>> seg;
    runSegment (in, 0, seg); // préchauffage
    const auto t0 = std::chrono::steady_clock::now();
    runSegment (in, 0, seg);
    return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
}

} // namespace lsplit
