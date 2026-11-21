"""Conversion du modèle Demucs « htdemucs_6s » (Meta, licence MIT) en ONNX pour Layers Split.

Le réseau est exporté SANS ses STFT / iSTFT (non exportables en ONNX) : Layers Split les calcule
lui-même en C++ (Source/Engine/DemucsModel.cpp), exactement comme Demucs.

  entrées  : mix [1, 2, L]           signal stéréo du morceau (L = longueur d'entraînement, 7,8 s)
             mag [1, 4, 2048, T]     spectre complexe (réel/imag par canal), T = ceil(L / 1024)
  sorties  : spec [1, S*4, 2048, T]  spectre de chaque source (branche fréquentielle)
             wave [1, S*2, L]        signal de chaque source (branche temporelle)
  source finale = iSTFT(spec) + wave

Usage :
  python export_htdemucs.py --out htdemucs_6s.onnx [--random] [--check exemple.wav]
"""
import argparse
import json
import math
import sys

import numpy as np
import torch
from torch.nn import functional as F
from einops import rearrange


class Core(torch.nn.Module):
    """Le corps de HTDemucs.forward, entre _magnitude et _mask (copié de demucs 4.1)."""

    def __init__(self, m):
        super().__init__()
        self.m = m

    def forward(self, mix, mag):
        self = self.m
        x = mag
        B, C, Fq, T = x.shape
        mean = x.mean(dim=(1, 2, 3), keepdim=True)
        std = x.std(dim=(1, 2, 3), keepdim=True)
        x = (x - mean) / (1e-5 + std)
        xt = mix
        meant = xt.mean(dim=(1, 2), keepdim=True)
        stdt = xt.std(dim=(1, 2), keepdim=True)
        xt = (xt - meant) / (1e-5 + stdt)

        saved, saved_t, lengths, lengths_t = [], [], [], []
        for idx, encode in enumerate(self.encoder):
            lengths.append(x.shape[-1])
            inject = None
            if idx < len(self.tencoder):
                lengths_t.append(xt.shape[-1])
                tenc = self.tencoder[idx]
                xt = tenc(xt)
                if not tenc.empty:
                    saved_t.append(xt)
                else:
                    inject = xt
            x = encode(x, inject)
            if idx == 0 and self.freq_emb is not None:
                frs = torch.arange(x.shape[-2], device=x.device)
                emb = self.freq_emb(frs).t()[None, :, :, None].expand_as(x)
                x = x + self.freq_emb_scale * emb
            saved.append(x)
        if self.crosstransformer:
            if self.bottom_channels:
                b, c, f, t = x.shape
                x = rearrange(x, "b c f t-> b c (f t)")
                x = self.channel_upsampler(x)
                x = rearrange(x, "b c (f t)-> b c f t", f=f)
                xt = self.channel_upsampler_t(xt)
            x, xt = self.crosstransformer(x, xt)
            if self.bottom_channels:
                x = rearrange(x, "b c f t-> b c (f t)")
                x = self.channel_downsampler(x)
                x = rearrange(x, "b c (f t)-> b c f t", f=f)
                xt = self.channel_downsampler_t(xt)

        for idx, decode in enumerate(self.decoder):
            skip = saved.pop(-1)
            x, pre = decode(x, skip, lengths.pop(-1))
            offset = self.depth - len(self.tdecoder)
            if idx >= offset:
                tdec = self.tdecoder[idx - offset]
                length_t = lengths_t.pop(-1)
                if tdec.empty:
                    pre = pre[:, :, 0]
                    xt, _ = tdec(pre, None, length_t)
                else:
                    skip = saved_t.pop(-1)
                    xt, _ = tdec(xt, skip, length_t)

        S = len(self.sources)
        x = x.view(B, S, -1, Fq, T)
        x = x * std[:, None] + mean[:, None]
        x = x.reshape(B, S * C, Fq, T)
        xt = xt.view(B, S, -1, mix.shape[-1])
        xt = xt * stdt[:, None] + meant[:, None]
        xt = xt.reshape(B, S * 2, mix.shape[-1])
        return x, xt


def load(random):
    if random:
        from demucs.htdemucs import HTDemucs
        torch.manual_seed(0)
        m = HTDemucs(sources=["drums", "bass", "other", "vocals", "guitar", "piano"])
    else:
        from demucs.pretrained import get_model
        m = get_model("htdemucs_6s")
        if hasattr(m, "models"):  # BagOfModels d'un seul modèle
            assert len(m.models) == 1, "htdemucs_6s doit contenir un seul modèle"
            m = m.models[0]
    m.eval()
    assert m.cac, "le modèle doit utiliser cac=True"
    assert m.nfft == 4096 and m.hop_length == 1024 and m.samplerate == 44100
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--random", action="store_true", help="poids aléatoires (test de la conversion)")
    ap.add_argument("--check", help="WAV 44,1 kHz stéréo : calcule la séparation de référence PyTorch")
    ap.add_argument("--ref-out", help="dossier où écrire les stems de référence")
    args = ap.parse_args()

    m = load(args.random)
    L = int(m.segment * m.samplerate)
    T = int(math.ceil(L / m.hop_length))
    core = Core(m).eval()

    mix = torch.randn(1, 2, L) * 0.1
    z = m._spec(mix)
    mag = m._magnitude(z)

    # vérification : Core + spec/ispec redonne exactement HTDemucs.forward
    with torch.no_grad():
        ref = m(mix)
        xs, xt = core(mix, mag)
        S = len(m.sources)
        zout = m._mask(z, xs.view(1, S, 4, 2048, T))
        y = m._ispec(zout, L).view(1, S, 2, L) + xt.view(1, S, 2, L)
    err = (y - ref).abs().max().item()
    print("ecart Core vs HTDemucs :", err)
    assert err < 1e-4

    torch.onnx.export(core, (mix, mag), args.out, input_names=["mix", "mag"], output_names=["spec", "wave"],
                      opset_version=17, do_constant_folding=True, dynamo=False)
    info = {"sources": list(m.sources), "segment_samples": L, "frames": T, "nfft": m.nfft, "hop": m.hop_length,
            "samplerate": m.samplerate}
    with open(args.out + ".json", "w") as f:
        json.dump(info, f)
    print("export ok", info)

    import onnxruntime as ort
    s = ort.InferenceSession(args.out, providers=["CPUExecutionProvider"])
    o = s.run(None, {"mix": mix.numpy(), "mag": mag.numpy()})
    e1 = np.abs(o[0] - xs.numpy()).max() / (np.abs(xs.numpy()).max() + 1e-9)
    e2 = np.abs(o[1] - xt.numpy()).max() / (np.abs(xt.numpy()).max() + 1e-9)
    print("ecart relatif ONNX vs PyTorch :", e1, e2)
    assert e1 < 1e-3 and e2 < 1e-3

    if args.check:
        # séparation de référence, réglages de Layers Split : chevauchement 25 %, sans décalage aléatoire
        import soundfile as sf
        from demucs.apply import apply_model
        wav, sr = sf.read(args.check, dtype="float32", always_2d=True)
        assert sr == 44100 and wav.shape[1] == 2
        w = torch.from_numpy(wav.T.copy())
        refm = w.mean(0)
        w = (w - refm.mean()) / refm.std()
        with torch.no_grad():
            src = apply_model(m, w[None], shifts=0, split=True, overlap=0.25, progress=False)[0]
        src = src * refm.std() + refm.mean()
        import os
        os.makedirs(args.ref_out, exist_ok=True)
        for i, name in enumerate(m.sources):
            sf.write(os.path.join(args.ref_out, name + ".wav"), src[i].numpy().T, 44100, subtype="FLOAT")
        print("references ecrites", args.ref_out)


if __name__ == "__main__":
    sys.exit(main())
