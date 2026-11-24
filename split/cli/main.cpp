// Layers Split — outil en ligne de commande (tests automatiques et traitement par lots).
//
//   LayersSplitCli --in morceau.mp3 --out dossier [--qualite rapide|haute|ultra] [--cpu]
//                  [--runtime dossier_moteur] [--sans-choeurs] [--sans-batterie]
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "../Source/Core/AudioIO.h"

#include <iostream>

using namespace lsplit;

static double rmsDb (const StereoBuffer& b)
{
    double s = 0.0;
    for (size_t i = 0; i < b.size(); ++i) s += (double) b.l[i] * b.l[i] + (double) b.r[i] * b.r[i];
    s /= std::max<size_t> (1, 2 * b.size());
    return 10.0 * std::log10 (s + 1.0e-20);
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    // options : « --in fichier » ou « --in=fichier »
    juce::StringPairArray opt;
    juce::StringArray flags;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a = juce::String::fromUTF8 (argv[i]);
        if (! a.startsWith ("--")) continue;
        if (a.contains ("="))
            opt.set (a.upToFirstOccurrenceOf ("=", false, false), a.fromFirstOccurrenceOf ("=", false, false).unquoted());
        else if (i + 1 < argc && ! juce::String (argv[i + 1]).startsWith ("--")
                 && (a == "--in" || a == "--out" || a == "--qualite" || a == "--runtime"))
            opt.set (a, juce::String::fromUTF8 (argv[++i]));
        else
            flags.add (a);
    }
    auto absFile = [] (const juce::String& p) { return juce::File::getCurrentWorkingDirectory().getChildFile (p); };
    if (opt["--in"].isEmpty() || opt["--out"].isEmpty())
    {
        std::cerr << "usage : LayersSplitCli --in morceau.mp3 --out dossier [--qualite rapide|haute|ultra] [--cpu] [--runtime dossier]\n";
        return 2;
    }
    const auto in = absFile (opt["--in"]);
    const auto out = absFile (opt["--out"]);
    if (! in.existsAsFile())
    {
        std::cerr << "ERREUR : fichier introuvable " << in.getFullPathName() << "\n";
        return 2;
    }
    SeparationSettings s;
    const auto q = opt["--qualite"];
    s.quality = q == "rapide" ? Quality::fast : (q == "ultra" ? Quality::ultra : Quality::high);
    s.useGpu = ! flags.contains ("--cpu");
    s.splitVocals = ! flags.contains ("--sans-choeurs");
    s.splitDrumsBass = ! flags.contains ("--sans-batterie");
    s.detailInstruments = ! flags.contains ("--simple");
    s.splitGuitarSolo = flags.contains ("--solo");

    juce::File runtime = opt["--runtime"].isNotEmpty() ? absFile (opt["--runtime"]) : findRuntimeDir();
    if (runtime == juce::File() || ! runtime.isDirectory())
    {
        std::cerr << "ERREUR : moteur introuvable (onnxruntime + models)\n";
        return 3;
    }
    std::cout << "moteur : " << runtime.getFullPathName() << "\n";

    std::string err;
    if (! OrtRuntime::load (runtime.getFullPathName().toStdString(), err))
    {
        std::cerr << "ERREUR : " << err << "\n";
        return 3;
    }
    std::cout << "onnxruntime " << OrtRuntime::version() << (OrtRuntime::directMLAvailable() ? " (DirectML disponible)" : "") << "\n";

    StereoBuffer mix;
    double sr = 0.0;
    juce::String jerr;
    if (! loadAudioFile (in, mix, sr, jerr))
    {
        std::cerr << "ERREUR : " << jerr << "\n";
        return 4;
    }
    std::cout << "fichier : " << in.getFileName() << " " << formatTime ((double) mix.size() / sr) << " " << sr << " Hz\n";

    Separator sep (modelsDirFor (runtime).getFullPathName().toStdString());
    StemSet res;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    int lastPct = -10;
    try
    {
        sep.run (mix, sr, s, res, [&] (const std::string& step, double p)
        {
            static std::string lastStep;
            if (step != lastStep)
            {
                std::cout << "  [" << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) << " s] " << step << "\n";
                lastStep = step;
            }
            const int pct = (int) (p * 100);
            if (pct - lastPct >= 10) { std::cout << "  " << pct << " % " << step << "\n"; lastPct = pct; }
            return true;
        });
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERREUR : " << e.what() << "\n";
        return 5;
    }
    const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    std::cout << "separation : " << secs << " s sur " << res.deviceUsed << "\n";

    out.createDirectory();
    StereoBuffer sum;
    sum.resize (mix.size());
    for (int k = 0; k < numStems; ++k)
    {
        if (! res.present[k]) continue;
        const auto& st = res.stems[k];
        if (! isChoirVoice (k)) // les pupitres sont une décomposition de « Chœurs »
            for (size_t i = 0; i < sum.size(); ++i) { sum.l[i] += st.l[i]; sum.r[i] += st.r[i]; }
        if (! writeWav (out.getChildFile (juce::String (stemFileName (k)) + ".wav"), st, sr, 24, jerr))
        {
            std::cerr << "ERREUR : " << jerr << "\n";
            return 6;
        }
        std::cout << "stem " << stemFileName (k) << " : " << juce::String (rmsDb (st), 1) << " dB\n";
    }
    for (size_t i = 0; i < sum.size(); ++i) { sum.l[i] -= mix.l[i]; sum.r[i] -= mix.r[i]; }
    const double residual = rmsDb (sum) - rmsDb (mix);
    double choirResidual = -200.0;
    if (res.present[stemSoprano])
    {
        StereoBuffer c = res.stems[stemBacking];
        for (int k = stemSoprano; k <= stemBassVoice; ++k)
            for (size_t i = 0; i < c.size(); ++i) { c.l[i] -= res.stems[k].l[i]; c.r[i] -= res.stems[k].r[i]; }
        choirResidual = rmsDb (c) - rmsDb (res.stems[stemBacking]);
        std::cout << "residu (somme des pupitres - choeurs) : " << juce::String (choirResidual, 1) << " dB\n";
    }
    std::cout << "residu (somme des stems - original) : " << juce::String (residual, 1) << " dB\n";

    AnalysisResult a;
    a.lufs = integratedLoudness (mix, sr);
    a.peakDb = samplePeakDb (mix);
    a.bpm = estimateTempo (res.present[stemDrums] ? res.stems[stemDrums] : mix, sr, a.bpmConfidence);
    estimateKey (mix, sr, a.keyRoot, a.keyMinor, a.keyConfidence);
    std::cout << "analyse : " << a.bpm << " BPM, " << a.keyName() << " (" << a.camelot() << "), "
              << juce::String (a.lufs, 1) << " LUFS, crete " << juce::String (a.peakDb, 1) << " dBFS\n";

    if (residual > -60.0 || choirResidual > -60.0)
    {
        std::cerr << "ERREUR : la somme des stems ne redonne pas l'original\n";
        return 7;
    }
    std::cout << "OK\n";
    return 0;
}
