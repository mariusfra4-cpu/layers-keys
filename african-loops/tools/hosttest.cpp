// Ouvre le vrai fichier VST3 construit, comme le ferait un logiciel de musique :
// chargement, lecture calée sur un tempo hôte, ouverture de la fenêtre, état.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdio>

struct FakeHost : juce::AudioPlayHead {
    double bpm = 100, ppq = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setBpm(bpm);
        p.setIsPlaying(playing);
        p.setPpqPosition(ppq);
        p.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
        return p;
    }
};

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) {
        std::printf("Utilisation : hosttest <plugin.vst3> [loop.wav]\n");
        return 2;
    }
    juce::AudioPluginFormatManager fm;
    fm.addFormat(new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* f : fm.getFormats()) f->findAllTypesForFile(types, argv[1]);
    if (types.isEmpty()) {
        std::printf("ÉCHEC : le VST3 n'est pas reconnu\n");
        return 1;
    }
    auto& d = *types[0];
    std::printf("Plugin : %s (%s), instrument=%d, entrées=%d sorties=%d\n", d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(),
                (int)d.isInstrument, d.numInputChannels, d.numOutputChannels);
    juce::String err;
    auto inst = fm.createPluginInstance(d, 48000, 512, err);
    if (!inst) {
        std::printf("ÉCHEC : impossible de créer le plugin : %s\n", err.toRawUTF8());
        return 1;
    }
    FakeHost host;
    inst->setPlayHead(&host);
    inst->setPlayConfigDetails(0, 2, 48000, 512);
    inst->prepareToPlay(48000, 512);
    std::printf("Paramètres : %d\n", inst->getParameters().size());

    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    int bad = 0;
    for (int b = 0; b < 400; ++b) {
        inst->processBlock(buf, midi);
        host.ppq += 512.0 / 48000 * host.bpm / 60;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                if (!std::isfinite(buf.getSample(c, i))) ++bad;
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed(inst->createEditorIfNeeded());
    if (!ed) {
        std::printf("ÉCHEC : pas de fenêtre\n");
        return 1;
    }
    std::printf("Fenêtre : %d x %d\n", ed->getWidth(), ed->getHeight());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    for (int b = 0; b < 200; ++b) inst->processBlock(buf, midi);
    juce::MemoryBlock state;
    inst->getStateInformation(state);
    inst->setStateInformation(state.getData(), (int)state.getSize());
    std::printf("État sauvegardé : %d octets\n", (int)state.getSize());
    ed.reset();
    inst->releaseResources();
    inst.reset();
    if (bad) {
        std::printf("ÉCHEC : %d échantillons invalides\n", bad);
        return 1;
    }
    std::printf("Le VST3 fonctionne dans un hôte.\n");
    return 0;
}
