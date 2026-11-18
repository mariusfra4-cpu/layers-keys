#include "Effects.h"

namespace spacepad
{
void FxChain::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 0 ? sampleRate : 48000;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, maxBlock), 2 };
    lowCut.prepare (spec);   lowCut.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    motionLp.prepare (spec); motionLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    warmLp.prepare (spec);   warmLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowCut.setResonance (0.7071f); motionLp.setResonance (0.9f); warmLp.setResonance (0.7071f);
    juce::dsp::ProcessSpec mono { sr, spec.maximumBlockSize, 1 };
    airL.prepare (mono); airR.prepare (mono);
    reverb.prepare (spec);
    chorus.prepare (sr);
    delay.prepare (sr);
    wet.setSize (2, juce::jmax (1, maxBlock));
    for (auto* s : { &sWarm, &sAir, &sSub, &sShim, &sRev, &sWidth, &sMotion }) s->reset (sr, 0.08);
    swellGain.reset (sr, 0.5);
    swellGain.setCurrentAndTargetValue (1.0f);
    lastAirDb = -100; lastLowCut = -1;
    reset();
}

void FxChain::reset()
{
    lowCut.reset(); motionLp.reset(); warmLp.reset(); airL.reset(); airR.reset(); reverb.reset(); chorus.reset(); delay.reset();
}

void FxChain::process (juce::AudioBuffer<float>& main, juce::AudioBuffer<float>& sub, juce::AudioBuffer<float>& shim, int num, const FxParams& p)
{
    if (num <= 0) return;
    if (wet.getNumSamples() < num) wet.setSize (2, num, false, false, true);
    sWarm.setTargetValue (p.warm);  sAir.setTargetValue (p.air);    sSub.setTargetValue (p.sub);
    sShim.setTargetValue (p.shimmer); sRev.setTargetValue (p.reverb); sWidth.setTargetValue (p.width);
    sMotion.setTargetValue (p.motion);
    sWarm.skip (num); sAir.skip (num); sSub.skip (num); sShim.skip (num); sRev.skip (num); sWidth.skip (num); sMotion.skip (num);
    const float warm = sWarm.getCurrentValue(), air = sAir.getCurrentValue(), subG = sSub.getCurrentValue();
    const float shimG = sShim.getCurrentValue(), rev = sRev.getCurrentValue(), width = sWidth.getCurrentValue(), motion = sMotion.getCurrentValue();

    float* L = main.getWritePointer (0);
    float* R = main.getWritePointer (1);

    // 1. Sub : une octave plus bas
    if (subG > 0.0005f)
    {
        main.addFrom (0, 0, sub, 0, 0, num, subG * 0.9f);
        main.addFrom (1, 0, sub, 1, 0, num, subG * 0.9f);
    }

    juce::dsp::AudioBlock<float> block (main);
    auto blk = block.getSubBlock (0, (size_t) num);

    // 2. Low cut : 20 Hz … 500 Hz
    if (p.lowCut > 0.005f)
    {
        if (std::abs (p.lowCut - lastLowCut) > 0.001f) { lowCut.setCutoffFrequency (20.0f * std::pow (25.0f, p.lowCut)); lastLowCut = p.lowCut; }
        lowCut.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }

    // 3. Warm : saturation douce + aigus adoucis
    if (warm > 0.002f)
    {
        const float drive = 1.0f + 4.0f * warm;
        for (int i = 0; i < num; ++i)
        {
            L[i] = (1.0f - warm) * L[i] + warm * std::tanh (drive * L[i]) / drive * 1.6f;
            R[i] = (1.0f - warm) * R[i] + warm * std::tanh (drive * R[i]) / drive * 1.6f;
        }
        warmLp.setCutoffFrequency (juce::jlimit (2000.0f, (float) (sr * 0.45), 20000.0f * std::pow (0.22f, warm)));
        warmLp.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }

    // 4. Motion : filtre qui respire lentement (0,05 à 0,4 Hz)
    if (motion > 0.002f)
    {
        const double rate = 0.05 + 0.35 * motion;
        const float top = (float) juce::jmin (18000.0, sr * 0.45);
        for (int s = 0; s < num; s += 32)
        {
            const int n = juce::jmin (32, num - s);
            const float lfo = 0.5f + 0.5f * (float) std::sin (lfoPhase);
            lfoPhase += juce::MathConstants<double>::twoPi * rate * n / sr;
            if (lfoPhase > juce::MathConstants<double>::twoPi) lfoPhase -= juce::MathConstants<double>::twoPi;
            const float depth = motion * lfo;
            motionLp.setCutoffFrequency (std::exp (std::log (top) + (std::log (450.0f) - std::log (top)) * depth));
            auto sb = blk.getSubBlock ((size_t) s, (size_t) n);
            motionLp.process (juce::dsp::ProcessContextReplacing<float> (sb));
        }
    }

    // 5. Air : brillance au-dessus de 8 kHz
    const float airDb = air * 9.0f;
    if (std::abs (airDb - lastAirDb) > 0.05f)
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, juce::jmin (8000.0, sr * 0.4), 0.7f, juce::Decibels::decibelsToGain (airDb));
        *airL.coefficients = *c; *airR.coefficients = *c;
        lastAirDb = airDb;
    }
    if (airDb > 0.05f)
        for (int i = 0; i < num; ++i) { L[i] = airL.processSample (L[i]); R[i] = airR.processSample (R[i]); }

    // 5b. Chorus puis delay au tempo (les répétitions passent ensuite dans la réverbération)
    chorus.process (L, R, num, p.chorus);
    delay.process (L, R, num, p.delaySeconds, 0.42f, p.delay * 0.8f);

    // 6. Réverbération + Shimmer (couche une octave plus haut envoyée dans la réverbération)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = juce::jlimit (0.0f, 1.0f, 0.62f + 0.37f * rev);
        rp.damping = 0.35f - 0.15f * shimG;
        rp.wetLevel = 1.0f;
        rp.dryLevel = 0.0f;
        rp.width = 1.0f;
        rp.freezeMode = p.freeze ? 1.0f : 0.0f;
        reverb.setParameters (rp);
        const float send = 0.25f + 0.75f * rev;
        for (int ch = 0; ch < 2; ++ch)
        {
            wet.copyFrom (ch, 0, main, ch, 0, num);
            wet.applyGain (ch, 0, num, send);
            wet.addFrom (ch, 0, shim, ch, 0, num, shimG * 1.1f);
        }
        juce::dsp::AudioBlock<float> wb (wet);
        auto wsub = wb.getSubBlock (0, (size_t) num);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (wsub));
        const float wetAmt = (p.freeze ? 0.55f : 0.0f) + 0.5f * rev + 0.35f * shimG;
        const float dryAmt = p.freeze ? 0.6f : 1.0f - 0.35f * rev;
        for (int ch = 0; ch < 2; ++ch)
        {
            main.applyGain (ch, 0, num, dryAmt);
            main.addFrom (ch, 0, wet, ch, 0, num, juce::jmin (0.9f, wetAmt) * 0.35f);
        }
    }

    // 7. Width : 0 = mono, 0,5 = normal, 1 = très large
    if (std::abs (width - 0.5f) > 0.002f)
    {
        const float k = width * 2.0f;
        for (int i = 0; i < num; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * k;
            L[i] = m + s; R[i] = m - s;
        }
    }

    // 8. Swell : le volume monte et redescend doucement (environ 14 secondes)
    swellGain.setTargetValue (1.0f);
    if (p.swell || swellGain.isSmoothing() || swellGain.getCurrentValue() < 0.999f)
    {
        for (int i = 0; i < num; ++i)
        {
            float target = 1.0f;
            if (p.swell)
            {
                swellPhase += juce::MathConstants<double>::twoPi * 0.07 / sr;
                if (swellPhase > juce::MathConstants<double>::twoPi) swellPhase -= juce::MathConstants<double>::twoPi;
                target = 0.45f + 0.55f * (0.5f + 0.5f * (float) std::sin (swellPhase - juce::MathConstants<double>::halfPi));
            }
            swellGain.setTargetValue (target);
            const float g = swellGain.getNextValue();
            L[i] *= g; R[i] *= g;
        }
    }
}
}
