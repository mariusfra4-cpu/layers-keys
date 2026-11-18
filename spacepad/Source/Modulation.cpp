#include "Modulation.h"

namespace spacepad
{
float delayDivision (int index)
{
    static const float d[] = { 2.0f, 1.0f, 0.75f, 0.5f, 2.0f / 3.0f, 0.25f };
    return d[juce::jlimit (0, 5, index)];
}

juce::StringArray delayDivisionNames() { return { "1/2", "1/4", "1/8 .", "1/8", "1/4 T", "1/16" }; }

static float readLinear (const std::vector<float>& b, int write, float delaySamples)
{
    const int n = (int) b.size();
    float pos = (float) write - delaySamples;
    while (pos < 0) pos += (float) n;
    const int i0 = (int) pos;
    const float f = pos - (float) i0;
    const int i1 = (i0 + 1) % n;
    return b[(size_t) (i0 % n)] * (1.0f - f) + b[(size_t) i1] * f;
}

// ---------------------------------------------------------------------------------------------
void StereoChorus::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000;
    const size_t n = (size_t) (sr * 0.06) + 4;
    bufL.assign (n, 0.0f); bufR.assign (n, 0.0f);
    amt.reset (sr, 0.08);
    reset();
}

void StereoChorus::reset() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); write = 0; }

float StereoChorus::read (const std::vector<float>& b, float d) const { return readLinear (b, write, d); }

void StereoChorus::process (float* L, float* R, int num, float amount)
{
    amt.setTargetValue (amount);
    if (amount < 0.001f && ! amt.isSmoothing()) { amt.setCurrentAndTargetValue (0); return; }
    const int n = (int) bufL.size();
    const double rate = 0.35 + 0.55 * amount;
    const double inc = juce::MathConstants<double>::twoPi * rate / sr;
    const float base = (float) (sr * 0.014);
    for (int i = 0; i < num; ++i)
    {
        const float a = amt.getNextValue();
        const float depth = (float) (sr * (0.0012 + 0.0045 * a));
        bufL[(size_t) write] = L[i];
        bufR[(size_t) write] = R[i];
        // deux voix par côté, décalées : un son plus épais et plus large
        const float m1 = (float) std::sin (phase), m2 = (float) std::sin (phase + juce::MathConstants<double>::halfPi);
        const float m3 = (float) std::sin (phase * 0.83 + 2.1);
        const float wl = 0.6f * read (bufL, base + depth * m1) + 0.4f * read (bufR, base * 1.37f + depth * m3);
        const float wr = 0.6f * read (bufR, base + depth * m2) + 0.4f * read (bufL, base * 1.21f - depth * m3);
        const float dry = 1.0f - 0.3f * a, wetG = 0.75f * a;
        L[i] = L[i] * dry + wl * wetG;
        R[i] = R[i] * dry + wr * wetG;
        write = (write + 1) % n;
        phase += inc;
        if (phase > juce::MathConstants<double>::twoPi * 100) phase -= juce::MathConstants<double>::twoPi * 100;
    }
}

// ---------------------------------------------------------------------------------------------
void TempoDelay::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000;
    const size_t n = (size_t) (sr * 3.2) + 4;
    bufL.assign (n, 0.0f); bufR.assign (n, 0.0f);
    time.reset (sr, 0.25);
    sMix.reset (sr, 0.08);
    sFb.reset (sr, 0.08);
    time.setCurrentAndTargetValue ((float) (sr * 0.5));
    reset();
}

void TempoDelay::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f);
    write = 0; dampL = dampR = 0;
}

float TempoDelay::read (const std::vector<float>& b, float d) const { return readLinear (b, write, d); }

void TempoDelay::process (float* L, float* R, int num, float seconds, float feedback, float mix)
{
    sMix.setTargetValue (mix);
    sFb.setTargetValue (juce::jlimit (0.0f, 0.88f, feedback));
    time.setTargetValue ((float) juce::jlimit (sr * 0.02, sr * 3.0, sr * (double) seconds));
    const bool quiet = mix < 0.001f && ! sMix.isSmoothing();
    const int n = (int) bufL.size();
    // adoucissement à chaque répétition (environ 4,5 kHz)
    const float k = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 4500.0 / sr));
    for (int i = 0; i < num; ++i)
    {
        const float d = time.getNextValue(), fb = sFb.getNextValue(), m = sMix.getNextValue();
        const float dl = read (bufL, d), dr = read (bufR, d);
        dampL += k * (dl - dampL);
        dampR += k * (dr - dampR);
        if (quiet)
        {
            // rien à ajouter : on laisse seulement mourir les répétitions déjà écrites
            bufL[(size_t) write] = dampR * fb;
            bufR[(size_t) write] = dampL * fb;
        }
        else
        {
            const float in = 0.5f * (L[i] + R[i]);
            bufL[(size_t) write] = in + dampR * fb;       // ping-pong : gauche puis droite
            bufR[(size_t) write] = dampL * fb;
            L[i] += m * 0.85f * dl;
            R[i] += m * 0.85f * dr;
        }
        write = (write + 1) % n;
    }
}

// ---------------------------------------------------------------------------------------------
void CrowdFx::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 0 ? sampleRate : 48000;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, maxBlock), 2 };
    lowCut.prepare (spec); lowCut.setType (juce::dsp::StateVariableTPTFilterType::highpass); lowCut.setResonance (0.7071f);
    far.prepare (spec); far.setType (juce::dsp::StateVariableTPTFilterType::lowpass); far.setResonance (0.7071f);
    chorus.prepare (sr);
    delay.prepare (sr);
    reverb.prepare (spec);
    wet.setSize (2, juce::jmax (1, maxBlock));
    for (auto* s : { &sDist, &sRev, &sWidth, &sMove }) s->reset (sr, 0.08);
    lastLowCut = lastFar = -1;
    reset();
}

void CrowdFx::reset() { lowCut.reset(); far.reset(); chorus.reset(); delay.reset(); reverb.reset(); }

void CrowdFx::process (juce::AudioBuffer<float>& buf, int num, const CrowdFxParams& p)
{
    if (num <= 0) return;
    if (wet.getNumSamples() < num) wet.setSize (2, num, false, false, true);
    sDist.setTargetValue (p.distance); sRev.setTargetValue (p.reverb); sWidth.setTargetValue (p.width); sMove.setTargetValue (p.movement);
    sDist.skip (num); sRev.skip (num); sWidth.skip (num); sMove.skip (num);
    const float dist = sDist.getCurrentValue(), rev = sRev.getCurrentValue(), width = sWidth.getCurrentValue(), move = sMove.getCurrentValue();
    float* L = buf.getWritePointer (0);
    float* R = buf.getWritePointer (1);
    juce::dsp::AudioBlock<float> block (buf);
    auto blk = block.getSubBlock (0, (size_t) num);

    // 1. Low cut : 20 Hz … 500 Hz
    if (p.lowCut > 0.005f)
    {
        if (std::abs (p.lowCut - lastLowCut) > 0.001f) { lowCut.setCutoffFrequency (20.0f * std::pow (25.0f, p.lowCut)); lastLowCut = p.lowCut; }
        lowCut.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }
    // 2. Distance : la foule s'éloigne (aigus adoucis, un peu moins fort, plus de salle)
    if (dist > 0.003f)
    {
        const float fc = juce::jlimit (900.0f, (float) (sr * 0.45), 18000.0f * std::pow (0.12f, dist));
        if (std::abs (fc - lastFar) > 1.0f) { far.setCutoffFrequency (fc); lastFar = fc; }
        far.process (juce::dsp::ProcessContextReplacing<float> (blk));
        buf.applyGain (0, num, 1.0f - 0.3f * dist);
    }
    // 3. Chorus  4. Delay au tempo
    chorus.process (L, R, num, p.chorus);
    delay.process (L, R, num, p.delaySeconds, p.feedback, p.delay);
    // 5. Mouvement : le son se promène doucement de gauche à droite
    if (move > 0.003f)
    {
        const double rate = 0.06 + 0.4 * move;
        for (int i = 0; i < num; ++i)
        {
            const float pan = 0.75f * move * (float) std::sin (panPhase);   // au plus 3/4 à gauche ou à droite : jamais un côté muet
            panPhase += juce::MathConstants<double>::twoPi * rate / sr;
            const float a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float gl = std::cos (a) * 1.41421356f, gr = std::sin (a) * 1.41421356f;
            L[i] *= juce::jmin (1.0f, gl); R[i] *= juce::jmin (1.0f, gr);
        }
        if (panPhase > juce::MathConstants<double>::twoPi) panPhase -= juce::MathConstants<double>::twoPi;
    }
    // 6. Reverb propre au crowd (la distance en ajoute)
    const float revAmt = juce::jlimit (0.0f, 1.0f, rev + 0.4f * dist);
    if (revAmt > 0.003f)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.6f + 0.38f * revAmt; rp.damping = 0.4f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
        reverb.setParameters (rp);
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, 0, buf, ch, 0, num);
        juce::dsp::AudioBlock<float> wb (wet);
        auto ws = wb.getSubBlock (0, (size_t) num);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (ws));
        for (int ch = 0; ch < 2; ++ch)
        {
            buf.applyGain (ch, 0, num, 1.0f - 0.3f * revAmt);
            buf.addFrom (ch, 0, wet, ch, 0, num, 0.3f * revAmt);
        }
    }
    // 7. Width
    if (std::abs (width - 0.5f) > 0.002f)
    {
        const float k = width * 2.0f;
        for (int i = 0; i < num; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * k;
            L[i] = m + s; R[i] = m - s;
        }
    }
}
}
