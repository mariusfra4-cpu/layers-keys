#include "TrackView.h"

#include <algorithm>
#include "../Core/Text.h"

namespace lsplit
{

//==============================================================================
StemLaneHeader::StemLaneHeader (LayersSplitProcessor& p, int stemId) : proc (p), id (stemId)
{
    for (auto* b : { &soloBtn, &muteBtn })
    {
        b->setClickingTogglesState (true);
        b->getProperties().set ("bold", true);
        addAndMakeVisible (*b);
    }
    soloBtn.setColour (juce::TextButton::buttonOnColourId, theme::solo);
    muteBtn.setColour (juce::TextButton::buttonOnColourId, theme::mute);
    muteBtn.getProperties().set ("onText", (juce::int64) 0xffffffff);
    soloBtn.setTooltip (lsplit::U ("Solo : n'écouter que ce stem (plusieurs possibles)"));
    muteBtn.setTooltip ("Muet");

    soloBtn.onClick = [this] { proc.controls[(size_t) id].solo = soloBtn.getToggleState(); };
    muteBtn.onClick = [this] { proc.controls[(size_t) id].mute = muteBtn.getToggleState(); };

    gain.setSliderStyle (juce::Slider::LinearHorizontal);
    gain.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gain.setRange (-40.0, 12.0, 0.1);
    gain.setSkewFactorFromMidPoint (-6.0);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setColour (juce::Slider::trackColourId, theme::stemColour (id));
    gain.setPopupDisplayEnabled (true, true, nullptr);
    gain.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    gain.onValueChange = [this] { proc.controls[(size_t) id].gainDb = (float) gain.getValue(); };
    gain.setTooltip ("Volume du stem (double-clic : 0 dB)");
    addAndMakeVisible (gain);

    if (id == stemBacking)
    {
        // petit bouton à côté de « Chœurs » : affiche les pupitres soprano, alto, ténor, basse
        detachBtn.setTooltip (lsplit::U ("Détacher les chœurs en pupitres : soprano, alto, ténor, basse (expérimental)"));
        detachBtn.getProperties().set ("bold", true);
        detachBtn.onClick = [this]
        {
            proc.choirDetached = ! proc.choirDetached.load();
            refresh();
            if (onLayoutChanged) onLayoutChanged();
        };
        addAndMakeVisible (detachBtn);
    }

    setTooltip ("Glissez ce stem vers une piste de votre logiciel de musique");
    refresh();
}

juce::File StemLaneHeader::stemFile() const
{
    auto d = proc.getData();
    if (d == nullptr || ! d->hasStems || d->cacheDir == juce::File()) return {};
    return d->cacheDir.getChildFile (juce::String (stemFileName (id)) + ".wav");
}

void StemLaneHeader::refresh()
{
    const auto& c = proc.controls[(size_t) id];
    soloBtn.setToggleState (c.solo.load(), juce::dontSendNotification);
    muteBtn.setToggleState (c.mute.load(), juce::dontSendNotification);
    gain.setValue (c.gainDb.load(), juce::dontSendNotification);
    auto d = proc.getData();
    const bool present = d != nullptr && d->hasStems && d->stems.present[id];
    for (juce::Component* comp : { (juce::Component*) &soloBtn, (juce::Component*) &muteBtn, (juce::Component*) &gain })
        comp->setEnabled (present);
    if (id == stemBacking)
    {
        const bool voices = d != nullptr && d->hasStems && d->stems.present[stemSoprano];
        detachBtn.setVisible (voices);
        detachBtn.setButtonText (proc.choirDetached.load() ? lsplit::U ("Regrouper ▴") : lsplit::U ("Détacher ▾"));
        detachBtn.setToggleState (proc.choirDetached.load(), juce::dontSendNotification);
    }
    repaint();
}

void StemLaneHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (theme::panel2);
    g.fillRect (r);
    g.setColour (theme::line);
    g.drawHorizontalLine (r.getBottom() - 1, 0.0f, (float) r.getWidth());
    g.setColour (theme::border);
    g.drawVerticalLine (r.getRight() - 1, 0.0f, (float) r.getHeight());

    auto d = proc.getData();
    const bool present = d != nullptr && d->hasStems && d->stems.present[id];
    const auto col = theme::stemColour (id);

    const bool sub = isChoirVoice (id);   // pupitre : décalé sous « Chœurs »
    const float barX = sub ? 26.0f : 10.0f;
    if (sub)
    {
        g.setColour (theme::stemColour (stemBacking).withAlpha (0.35f));
        g.fillRect (12.0f, 0.0f, 2.0f, (float) getHeight());
        g.fillRect (12.0f, (float) getHeight() * 0.5f, 10.0f, 2.0f);
    }
    g.setColour (present ? col : col.withAlpha (0.25f));
    g.fillRoundedRectangle (juce::Rectangle<float> (barX, 8.0f, 4.0f, (float) getHeight() - 16.0f), 2.0f);

    auto txt = r.withTrimmedLeft ((int) barX + 12).withTrimmedRight (150);
    const int h = getHeight();
    g.setColour (present ? theme::textBright : theme::dim);
    g.setFont (theme::font (14.0f, true));
    const bool detailed = d != nullptr && d->hasStems && d->stems.present[stemGuitar];
    g.drawText (id == stemOther && ! detailed ? U ("Instruments") : U (stemName (id)), txt.withY (h / 2 - 19).withHeight (18), juce::Justification::centredLeft, true);
    g.setColour (theme::dim);
    g.setFont (theme::font (11.5f));
    if (! detachBtn.isVisible())
        g.drawText (id == stemOther && ! detailed ? U ("Claviers, guitares, etc.") : U (theme::stemSubtitle (id)), txt.withY (h / 2 + 1).withHeight (16), juce::Justification::centredLeft, true);

    // vu-mètre
    const float lvl = juce::jlimit (0.0f, 1.0f, proc.stemLevel[(size_t) id].load());
    auto meter = juce::Rectangle<float> ((float) getWidth() - 10.0f, 8.0f, 4.0f, (float) h - 16.0f);
    g.setColour (juce::Colour (0xff0a0b0e));
    g.fillRoundedRectangle (meter, 2.0f);
    const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (lvl > 0.98f ? theme::error : col);
    g.fillRoundedRectangle (meter.withTrimmedTop (meter.getHeight() * (1.0f - frac)), 2.0f);

    if (present && isMouseOver (true))
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (10.5f));
        g.drawText (lsplit::U ("⇩ glisser"), r.withTrimmedLeft (22).withHeight (14).withY (3), juce::Justification::topLeft);
    }
}

void StemLaneHeader::resized()
{
    auto r = getLocalBounds().reduced (6, 0).withTrimmedRight (12);
    const int cy = getHeight() / 2;
    auto right = r.removeFromRight (136);
    if (id == stemBacking)
        detachBtn.setBounds (r.getX() + 16, cy + 1, 92, 18);   // à la place du sous-titre
    soloBtn.setBounds (right.getX(), cy - 12, 24, 24);
    muteBtn.setBounds (right.getX() + 28, cy - 12, 24, 24);
    gain.setBounds (right.getX() + 58, cy - 10, right.getWidth() - 58, 20);
}

void StemLaneHeader::mouseDown (const juce::MouseEvent&)
{
    dragStarted = false;
}

void StemLaneHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6) return;
    const auto f = stemFile();
    if (! f.existsAsFile()) return;
    dragStarted = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

//==============================================================================
TrackView::TrackView (LayersSplitProcessor& p) : proc (p)
{
    for (int k = 0; k < numStems; ++k)
    {
        auto* h = headers.add (new StemLaneHeader (proc, k));
        h->onLayoutChanged = [this] { refresh(); };
        addAndMakeVisible (h);
    }
    refresh();
}

juce::Rectangle<int> TrackView::overviewArea() const { return { 0, 0, getWidth(), overviewH }; }
juce::Rectangle<int> TrackView::rulerArea() const { return { headerWidth, overviewH, getWidth() - headerWidth, rulerH }; }

void TrackView::updateVisibleLanes()
{
    // stems calculés, ou ceux que les réglages actuels vont produire
    visible.clear();
    const auto& s = proc.settings;
    // ordre d'affichage : les pupitres juste sous « Chœurs »
    static const int order[] = { stemLead, stemBacking, stemSoprano, stemAlto, stemTenor, stemBassVoice,
                                 stemDrums, stemBass, stemGuitar, stemGuitarSolo, stemPiano, stemOther };
    for (int k : order)
    {
        bool show;
        if (data != nullptr && data->hasStems)
            show = data->stems.present[k] && (! isChoirVoice (k) || proc.choirDetached.load());
        else if (isChoirVoice (k))
            show = false;
        else
            show = k == stemLead || k == stemOther
                || (k == stemBacking && s.splitVocals)
                || ((k == stemDrums || k == stemBass) && s.splitDrumsBass)
                || ((k == stemGuitar || k == stemPiano) && s.detailInstruments)
                || (k == stemGuitarSolo && s.detailInstruments && s.splitGuitarSolo);
        if (show) visible.push_back (k);
    }
    for (int k = 0; k < numStems; ++k)
        headers[k]->setVisible (std::find (visible.begin(), visible.end(), k) != visible.end());
}

juce::Rectangle<int> TrackView::laneArea (int stem) const
{
    const auto it = std::find (visible.begin(), visible.end(), stem);
    if (it == visible.end()) return {};
    const int idx = (int) (it - visible.begin());
    const int top = overviewH + rulerH;
    const int count = juce::jmax (1, (int) visible.size());
    const int h = juce::jmax (36, (getHeight() - top) / count);
    return { 0, top + idx * h, getWidth(), h };
}

juce::Rectangle<int> TrackView::waveColumn() const
{
    return { headerWidth, 0, juce::jmax (1, getWidth() - headerWidth - 12), getHeight() };
}

void TrackView::resized()
{
    updateVisibleLanes();
    for (int k = 0; k < numStems; ++k)
        headers[k]->setBounds (laneArea (k).withWidth (headerWidth));
}

void TrackView::refresh()
{
    data = proc.getData();
    resized();
    for (auto* h : headers) h->refresh();
    lastPlayheadX = -1;
    repaint();
}

void TrackView::drawWave (juce::Graphics& g, const Peaks& p, juce::Rectangle<int> r, juce::Colour c, float gainLin) const
{
    if (p.empty() || r.getWidth() <= 0) return;
    const int n = (int) p.lo.size();
    const float mid = (float) r.getCentreY();
    const float half = (float) r.getHeight() * 0.46f;
    g.setColour (c);
    for (int x = 0; x < r.getWidth(); ++x)
    {
        const int a = (int) ((double) x / r.getWidth() * n);
        const int b = juce::jmax (a + 1, (int) ((double) (x + 1) / r.getWidth() * n));
        float lo = 0.0f, hi = 0.0f;
        for (int i = a; i < b && i < n; ++i)
        {
            lo = juce::jmin (lo, p.lo[(size_t) i]);
            hi = juce::jmax (hi, p.hi[(size_t) i]);
        }
        lo = juce::jlimit (-1.0f, 1.0f, lo * gainLin);
        hi = juce::jlimit (-1.0f, 1.0f, hi * gainLin);
        const float y1 = mid - hi * half, y2 = mid - lo * half;
        g.fillRect ((float) (r.getX() + x), y1, 1.0f, juce::jmax (1.0f, y2 - y1));
    }
}

void TrackView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    const auto wc = waveColumn();

    // aperçu du mix
    auto ov = overviewArea();
    g.setColour (theme::panel);
    g.fillRect (ov);
    g.setColour (theme::border);
    g.drawHorizontalLine (ov.getBottom() - 1, 0.0f, (float) getWidth());
    g.setColour (theme::dim);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("MIX ORIGINAL", ov.withTrimmedLeft (14).withHeight (24), juce::Justification::centredLeft);

    if (data != nullptr)
    {
        g.setFont (theme::font (12.0f));
        g.setColour (theme::text);
        juce::String info = data->sourceFile.getFileName();
        g.drawText (info, ov.withTrimmedLeft (14).withY (26).withHeight (20).withWidth (headerWidth - 24),
                    juce::Justification::centredLeft, true);
        g.setColour (theme::dim);
        g.drawText (formatTime (data->durationSec()) + lsplit::U ("  ·  ") + juce::String (data->sampleRate / 1000.0, 1) + " kHz",
                    ov.withTrimmedLeft (14).withY (46).withHeight (20).withWidth (headerWidth - 24),
                    juce::Justification::centredLeft, true);
        drawWave (g, data->mixPeaks, ov.withX (wc.getX()).withWidth (wc.getWidth()).reduced (0, 10),
                  proc.listenOriginal.load() || ! data->hasStems ? juce::Colour (0xffa3aab8) : juce::Colour (0xff6c7385), 1.0f);
    }

    // règle de temps
    auto ru = rulerArea();
    g.setColour (juce::Colour (0xff0d0f13));
    g.fillRect (ru.withX (0).withWidth (getWidth()));
    g.setColour (theme::border);
    g.drawHorizontalLine (ru.getBottom() - 1, 0.0f, (float) getWidth());
    if (data != nullptr && data->durationSec() > 0)
    {
        const double dur = data->durationSec();
        const double steps[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300 };
        double step = 300;
        for (double s : steps)
            if (wc.getWidth() / (dur / s) >= 70.0) { step = s; break; }
        g.setFont (theme::font (10.5f));
        for (double t = 0; t <= dur; t += step)
        {
            const int x = wc.getX() + (int) (t / dur * wc.getWidth());
            g.setColour (juce::Colour (0xff2a2e38));
            g.drawVerticalLine (x, (float) ru.getY() + 12, (float) ru.getBottom());
            g.setColour (theme::dim);
            g.drawText (formatTime (t), x + 4, ru.getY(), 60, ru.getHeight(), juce::Justification::centredLeft);
        }
    }

    // pistes
    const bool anySolo = data != nullptr && proc.anySoloInMix (*data);
    for (size_t vi = 0; vi < visible.size(); ++vi)
    {
        const int k = visible[vi];
        auto la = laneArea (k);
        g.setColour (vi % 2 == 0 ? juce::Colour (0xff0c0d11) : theme::bg);
        g.fillRect (la.withTrimmedLeft (headerWidth));
        g.setColour (theme::line);
        g.drawHorizontalLine (la.getBottom() - 1, (float) headerWidth, (float) getWidth());

        if (data != nullptr && data->hasStems && data->stems.present[k])
        {
            const auto& c = proc.controls[(size_t) k];
            const bool audible = proc.stemInMix (*data, k) && ! c.mute.load() && (! anySolo || c.solo.load());
            const float gl = juce::Decibels::decibelsToGain (c.gainDb.load(), -60.0f);
            auto col = theme::stemColour (k);
            if (! audible || proc.listenOriginal.load()) col = col.withSaturation (0.15f).withAlpha (0.45f);
            drawWave (g, data->stemPeaks[k], la.withX (wc.getX()).withWidth (wc.getWidth()).reduced (0, 6), col,
                      juce::jlimit (0.25f, 2.0f, gl));
        }
        else if (data != nullptr && vi == 0)
        {
            g.setColour (theme::dim);
            g.setFont (theme::font (13.0f));
            g.drawText (proc.isBusy() ? lsplit::U ("Séparation en cours…") : lsplit::U ("Cliquez sur SÉPARER pour extraire les stems"),
                        la.withTrimmedLeft (headerWidth + 16), juce::Justification::centredLeft);
        }
    }

    // état vide
    if (data == nullptr)
    {
        auto zone = juce::Rectangle<int> (headerWidth + 30, overviewH + rulerH + 30,
                                          getWidth() - headerWidth - 60, getHeight() - overviewH - rulerH - 60);
        if (zone.getWidth() > 100 && zone.getHeight() > 100)
        {
            g.setColour (dragOver ? theme::accent.withAlpha (0.08f) : juce::Colour (0x08ffffff));
            g.fillRoundedRectangle (zone.toFloat(), 12.0f);
            juce::Path p;
            p.addRoundedRectangle (zone.toFloat(), 12.0f);
            const float dashes[] = { 8.0f, 6.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
            g.setColour (dragOver ? theme::accent : juce::Colour (0xff2e3442));
            g.fillPath (dashed);
            g.setColour (theme::textBright);
            g.setFont (theme::font (20.0f, true));
            g.drawText ("Glissez un morceau ici", zone.withTrimmedBottom (zone.getHeight() / 2 - 10), juce::Justification::centredBottom);
            g.setColour (theme::dim);
            g.setFont (theme::font (14.0f));
            g.drawText (lsplit::U ("ou cliquez pour choisir un fichier  ·  WAV, AIFF, FLAC, MP3, OGG"),
                        zone.withTrimmedTop (zone.getHeight() / 2 + 6), juce::Justification::centredTop);
        }
    }
    else if (dragOver)
    {
        g.setColour (theme::accent.withAlpha (0.10f));
        g.fillRect (getLocalBounds());
        g.setColour (theme::accent);
        g.drawRect (getLocalBounds(), 2);
    }
}

void TrackView::paintOverChildren (juce::Graphics& g)
{
    if (data == nullptr || data->length() == 0) return;
    const auto wc = waveColumn();
    const double frac = juce::jlimit (0.0, 1.0, proc.positionSeconds() / juce::jmax (1.0e-6, data->durationSec()));
    const int x = wc.getX() + (int) std::round (frac * wc.getWidth());
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillRect (x, 0, 1, getHeight());
    g.setColour (theme::accent.withAlpha (0.25f));
    g.fillRect (x - 2, 0, 5, overviewH);
    lastPlayheadX = x;
}

void TrackView::updatePlayhead()
{
    if (data == nullptr) return;
    const auto wc = waveColumn();
    const double frac = juce::jlimit (0.0, 1.0, proc.positionSeconds() / juce::jmax (1.0e-6, data->durationSec()));
    const int x = wc.getX() + (int) std::round (frac * wc.getWidth());
    if (x != lastPlayheadX)
    {
        if (lastPlayheadX >= 0) repaint (lastPlayheadX - 3, 0, 7, getHeight());
        repaint (x - 3, 0, 7, getHeight());
    }
    for (auto* h : headers) h->repaint (h->getWidth() - 14, 0, 14, h->getHeight());
}

void TrackView::seekFromX (int x)
{
    if (data == nullptr) return;
    const auto wc = waveColumn();
    const double frac = juce::jlimit (0.0, 1.0, (double) (x - wc.getX()) / wc.getWidth());
    proc.seekSeconds (frac * data->durationSec());
    updatePlayhead();
}

void TrackView::mouseDown (const juce::MouseEvent& e)
{
    if (data == nullptr)
    {
        if (onOpenRequested) onOpenRequested();
        return;
    }
    if (e.x >= headerWidth)
        seekFromX (e.x);
}

void TrackView::mouseDrag (const juce::MouseEvent& e)
{
    if (data != nullptr && e.getMouseDownX() >= headerWidth)
        seekFromX (e.x);
}

bool TrackView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isSupportedAudioFile (juce::File (f))) return true;
    return false;
}

void TrackView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (const auto& f : files)
    {
        juce::File file (f);
        if (isSupportedAudioFile (file))
        {
            proc.openFile (file);
            return;
        }
    }
}

} // namespace lsplit
