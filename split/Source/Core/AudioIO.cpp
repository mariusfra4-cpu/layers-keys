#include "AudioIO.h"
#include "Text.h"

namespace lsplit
{

namespace
{
   #if JUCE_WINDOWS
    const char* kRuntimeLib = "onnxruntime.dll";
   #elif JUCE_MAC
    const char* kRuntimeLib = "libonnxruntime.dylib";
   #else
    const char* kRuntimeLib = "libonnxruntime.so";
   #endif

    bool looksLikeRuntimeDir (const juce::File& d)
    {
        return d.isDirectory() && d.getChildFile (kRuntimeLib).existsAsFile()
            && d.getChildFile ("models").isDirectory();
    }

    const int kCacheVersion = 2;
}

juce::File findRuntimeDir()
{
    juce::Array<juce::File> candidates;

    const auto env = juce::SystemStats::getEnvironmentVariable ("LAYERS_SPLIT_RUNTIME", {});
    if (env.isNotEmpty())
        candidates.add (juce::File (env));

   #if JUCE_WINDOWS
    const auto reg = juce::WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\Software\\JB Praise\\Layers Split\\InstallDir",
                                                      {}, juce::WindowsRegistry::WoW64_64bit);
    if (reg.isNotEmpty())
        candidates.add (juce::File (reg));
   #endif

    // à côté du programme (application autonome, outil en ligne de commande)
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    candidates.add (exe.getParentDirectory());
    candidates.add (exe.getParentDirectory().getChildFile ("runtime"));

   #if JUCE_WINDOWS
    candidates.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Layers Split"));
   #endif

    for (const auto& c : candidates)
        if (looksLikeRuntimeDir (c))
            return c;

    return {};
}

juce::File modelsDirFor (const juce::File& runtimeDir)
{
    return runtimeDir.getChildFile ("models");
}

juce::File userDataDir()
{
   #if JUCE_WINDOWS
    // AppData\Local : les stems sont volumineux, ils ne doivent pas suivre un profil itinérant
    auto d = juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("LayersSplit");
   #else
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LayersSplit");
   #endif
    d.createDirectory();
    return d;
}

juce::String audioFileWildcard()
{
    return "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg";
}

bool isSupportedAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg");
}

static juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager fm;
    static bool init = false;
    if (! init)
    {
        fm.registerBasicFormats();
        init = true;
    }
    return fm;
}

bool loadAudioFile (const juce::File& f, StereoBuffer& out, double& sampleRate, juce::String& error, double maxSeconds)
{
    static juce::CriticalSection lock; // AudioFormatManager n'est pas prévu pour plusieurs fils à la fois
    std::unique_ptr<juce::AudioFormatReader> reader;
    {
        const juce::ScopedLock sl (lock);
        reader.reset (formats().createReaderFor (f));
    }
    if (reader == nullptr)
    {
        error = "Format non reconnu ou fichier illisible : " + f.getFileName();
        return false;
    }

    sampleRate = reader->sampleRate;
    if (sampleRate < 8000.0 || sampleRate > 384000.0)
    {
        error = lsplit::U ("Fréquence d'échantillonnage non prise en charge (") + juce::String (sampleRate) + " Hz).";
        return false;
    }

    const juce::int64 total = reader->lengthInSamples;
    if (total <= 0)
    {
        error = "Le fichier est vide : " + f.getFileName();
        return false;
    }
    if ((double) total / sampleRate > maxSeconds)
    {
        error = "Morceau trop long (" + formatTime ((double) total / sampleRate)
              + "). Maximum : " + formatTime (maxSeconds) + ".";
        return false;
    }

    const int numCh = (int) juce::jlimit (1u, 2u, reader->numChannels);
    out.resize ((size_t) total);

    const int block = 1 << 16;
    juce::AudioBuffer<float> tmp (numCh, block);
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        const int n = (int) juce::jmin ((juce::int64) block, total - pos);
        if (! reader->read (&tmp, 0, n, pos, true, numCh > 1))
        {
            error = "Erreur de lecture dans " + f.getFileName();
            return false;
        }
        const float* l = tmp.getReadPointer (0);
        const float* r = tmp.getReadPointer (numCh > 1 ? 1 : 0);
        std::copy (l, l + n, out.l.begin() + (long) pos);
        std::copy (r, r + n, out.r.begin() + (long) pos);
    }

    // sécurité : aucune valeur invalide (NaN / infini) ne doit entrer dans le moteur
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (! std::isfinite (out.l[i])) out.l[i] = 0.0f;
        if (! std::isfinite (out.r[i])) out.r[i] = 0.0f;
    }
    return true;
}

bool writeWav (const juce::File& f, const StereoBuffer& b, double sampleRate, int bits, juce::String& error)
{
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (f);
    {
        std::unique_ptr<juce::OutputStream> os (tmp.getFile().createOutputStream().release());
        if (os == nullptr)
        {
            error = lsplit::U ("Impossible d'écrire ") + f.getFullPathName();
            return false;
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sampleRate, 2, bits, {}, 0));
        if (w == nullptr)
        {
            error = "Format WAV impossible (" + juce::String (bits) + " bits).";
            return false;
        }
        os.release(); // appartient maintenant au writer

        const int block = 1 << 15;
        juce::AudioBuffer<float> tmpBuf (2, block);
        for (size_t pos = 0; pos < b.size(); pos += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, b.size() - pos);
            tmpBuf.copyFrom (0, 0, b.l.data() + pos, n);
            tmpBuf.copyFrom (1, 0, b.r.data() + pos, n);
            if (bits < 32)
            {
                // évite l'écrêtage brutal en entier : limite douce à ±1
                for (int c = 0; c < 2; ++c)
                {
                    float* d = tmpBuf.getWritePointer (c);
                    for (int i = 0; i < n; ++i) d[i] = juce::jlimit (-1.0f, 1.0f, d[i]);
                }
            }
            if (! w->writeFromAudioSampleBuffer (tmpBuf, 0, n))
            {
                error = lsplit::U ("Erreur d'écriture (disque plein ?) : ") + f.getFileName();
                return false;
            }
        }
    }
    if (! tmp.overwriteTargetFileWithTemporary())
    {
        error = "Impossible de remplacer " + f.getFullPathName() + " (fichier ouvert ailleurs ?)";
        return false;
    }
    return true;
}

juce::String cacheKeyFor (const juce::File& source, const SeparationSettings& s)
{
    juce::String k;
    k << source.getFullPathName() << "|" << source.getSize() << "|" << source.getLastModificationTime().toMilliseconds()
      << "|q" << (int) s.quality << "|v" << (int) s.splitVocals << "|d" << (int) s.splitDrumsBass
      << "|i" << (int) s.detailInstruments << "|g" << (int) (s.detailInstruments && s.splitGuitarSolo) << "|c" << kCacheVersion;
    return juce::String::toHexString (k.hashCode64()).paddedLeft ('0', 16);
}

juce::File cacheDirFor (const juce::String& key)
{
    return userDataDir().getChildFile ("Cache").getChildFile (key);
}

bool cacheIsComplete (const juce::File& dir)
{
    return dir.getChildFile ("info.xml").existsAsFile();
}

bool saveToCache (const juce::File& dir, const PlayData& d, juce::String& error)
{
    dir.createDirectory();
    for (int k = 0; k < numStems; ++k)
        if (d.stems.present[k])
            if (! writeWav (dir.getChildFile (juce::String (stemFileName (k)) + ".wav"), d.stems.stems[k], d.sampleRate, 32, error))
                return false;

    juce::XmlElement info ("LayersSplitCache");
    info.setAttribute ("version", kCacheVersion);
    info.setAttribute ("source", d.sourceFile.getFullPathName());
    info.setAttribute ("sampleRate", d.sampleRate);
    info.setAttribute ("device", d.deviceUsed);
    info.setAttribute ("seconds", d.separationSeconds);
    juce::String present;
    for (int k = 0; k < numStems; ++k) present << (d.stems.present[k] ? "1" : "0");
    info.setAttribute ("stems", present);
    const auto& a = d.analysis;
    info.setAttribute ("bpm", a.bpm);
    info.setAttribute ("bpmConf", a.bpmConfidence);
    info.setAttribute ("keyRoot", a.keyRoot);
    info.setAttribute ("keyMinor", a.keyMinor);
    info.setAttribute ("keyConf", a.keyConfidence);
    info.setAttribute ("lufs", a.lufs);
    info.setAttribute ("peak", a.peakDb);
    if (! info.writeTo (dir.getChildFile ("info.xml")))
    {
        error = lsplit::U ("Impossible d'écrire le cache.");
        return false;
    }
    dir.setLastModificationTime (juce::Time::getCurrentTime());
    return true;
}

bool loadFromCache (const juce::File& dir, PlayData& d, juce::String& error)
{
    auto info = juce::XmlDocument::parse (dir.getChildFile ("info.xml"));
    if (info == nullptr || info->getIntAttribute ("version") != kCacheVersion)
    {
        error = "Cache illisible.";
        return false;
    }
    const auto present = info->getStringAttribute ("stems");
    for (int k = 0; k < numStems; ++k)
    {
        d.stems.present[k] = present[k] == '1';
        if (! d.stems.present[k]) continue;
        double sr = 0.0;
        if (! loadAudioFile (dir.getChildFile (juce::String (stemFileName (k)) + ".wav"), d.stems.stems[k], sr, error))
            return false;
        if (d.stems.stems[k].size() != d.mix.size())
        {
            error = "Cache incomplet.";
            return false;
        }
    }
    d.stems.sampleRate = d.sampleRate;
    d.hasStems = true;
    d.cacheDir = dir;
    d.deviceUsed = info->getStringAttribute ("device");
    d.separationSeconds = info->getDoubleAttribute ("seconds");
    auto& a = d.analysis;
    a.bpm = info->getDoubleAttribute ("bpm");
    a.bpmConfidence = info->getDoubleAttribute ("bpmConf");
    a.keyRoot = info->getIntAttribute ("keyRoot", -1);
    a.keyMinor = info->getBoolAttribute ("keyMinor");
    a.keyConfidence = info->getDoubleAttribute ("keyConf");
    a.lufs = info->getDoubleAttribute ("lufs", -100.0);
    a.peakDb = info->getDoubleAttribute ("peak", -100.0);
    a.durationSec = d.durationSec();
    d.hasAnalysis = true;
    dir.setLastModificationTime (juce::Time::getCurrentTime());
    return true;
}

void pruneCache (int keep)
{
    auto root = userDataDir().getChildFile ("Cache");
    auto dirs = root.findChildFiles (juce::File::findDirectories, false);
    if (dirs.size() <= keep) return;
    std::sort (dirs.begin(), dirs.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    for (int i = keep; i < dirs.size(); ++i)
        dirs.getReference (i).deleteRecursively();
}

juce::String formatTime (double s, bool millis)
{
    if (s < 0) s = 0;
    const int total = (int) s;
    juce::String t = juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    if (millis)
        t << "." << juce::String ((int) ((s - total) * 1000.0)).paddedLeft ('0', 3);
    return t;
}

Peaks computePeaks (const StereoBuffer& b, int columns)
{
    Peaks p;
    const size_t n = b.size();
    if (n == 0 || columns <= 0) return p;
    p.lo.assign ((size_t) columns, 0.0f);
    p.hi.assign ((size_t) columns, 0.0f);
    for (int c = 0; c < columns; ++c)
    {
        const size_t a = (size_t) ((double) c / columns * (double) n);
        const size_t e = std::max (a + 1, (size_t) ((double) (c + 1) / columns * (double) n));
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = a; i < e && i < n; ++i)
        {
            const float v = 0.5f * (b.l[i] + b.r[i]);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        p.lo[(size_t) c] = lo;
        p.hi[(size_t) c] = hi;
    }
    return p;
}

} // namespace lsplit
