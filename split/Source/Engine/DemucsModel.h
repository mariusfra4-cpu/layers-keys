// Layers Split — modèle Demucs « htdemucs_6s » (Meta, MIT) : batterie, basse, autres, voix, guitare, piano.
// Le réseau est en ONNX ; les STFT / iSTFT et le découpage en segments sont faits ici, comme Demucs.
#pragma once

#include "MdxModel.h"

namespace lsplit
{

class DemucsModel
{
public:
    using Progress = std::function<bool (double)>;

    // pathUtf8 : fichier .onnx ; ses réglages sont lus dans « fichier.onnx.json ».
    bool load (const std::string& pathUtf8, Device device, std::string& error);
    bool isLoaded() const { return session != nullptr; }

    int sourceIndex (const std::string& name) const;   // -1 si absent
    const std::vector<std::string>& sources() const { return names; }

    // in : signal 44,1 kHz. out : une source par entrée de sources(). Faux si annulé.
    bool process (const StereoBuffer& in, std::vector<StereoBuffer>& out, const Progress& progress);

private:
    void runSegment (const StereoBuffer& in, long start, std::vector<std::vector<float>>& out);

    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> names;
    int segment = 0, frames = 0, nfft = 4096, hop = 1024;
    Device dev = Device::cpu;
};

} // namespace lsplit
