#include "PluginEditor.h"
#include "Core/Text.h"

using namespace lsplit;

namespace
{
    constexpr int topH = 54;
    constexpr int footH = 28;
    constexpr int sideW = 344;

    void styleTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t)
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (11.0f, true));
        g.drawText (t, r, juce::Justification::centredLeft);
    }
}

LayersSplitEditor::LayersSplitEditor (LayersSplitProcessor& p) : AudioProcessorEditor (p), proc (p), tracks (p)
{
    setLookAndFeel (&lnf);

    // barre du haut
    for (auto* b : { &openBtn, &playBtn, &stopBtn, &abBtn, &loopBtn, &followBtn })
        addAndMakeVisible (*b);
    openBtn.onClick = [this] { chooseFile(); };
    openBtn.setTooltip ("Ouvrir un morceau (WAV, AIFF, FLAC, MP3, OGG)");
    playBtn.onClick = [this] { proc.togglePlay(); };
    playBtn.setTooltip ("Lecture / pause (barre d'espace)");
    stopBtn.onClick = [this] { proc.stop(); };
    abBtn.setClickingTogglesState (true);
    abBtn.setTooltip (lsplit::U ("Comparer : écouter le mix original au lieu des stems"));
    abBtn.onClick = [this] { proc.listenOriginal = abBtn.getToggleState(); tracks.repaint(); };
    loopBtn.setClickingTogglesState (true);
    loopBtn.onClick = [this] { proc.loop = loopBtn.getToggleState(); };
    followBtn.setClickingTogglesState (true);
    followBtn.setTooltip (lsplit::U ("Lire en même temps que le DAW, à la même position"));
    followBtn.onClick = [this] { proc.followHost = followBtn.getToggleState(); };
    followBtn.setVisible (! proc.isStandalone());

    master.setSliderStyle (juce::Slider::LinearHorizontal);
    master.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    master.setRange (-40.0, 6.0, 0.1);
    master.setSkewFactorFromMidPoint (-6.0);
    master.setDoubleClickReturnValue (true, 0.0);
    master.setPopupDisplayEnabled (true, true, nullptr);
    master.textFromValueFunction = [] (double v) { return lsplit::U ("Volume général ") + juce::String (v, 1) + " dB"; };
    master.onValueChange = [this] { proc.masterDb = (float) master.getValue(); };
    master.setTooltip (lsplit::U ("Volume général (double-clic : 0 dB)"));
    addAndMakeVisible (master);

    timeLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::plain)));
    timeLabel.setColour (juce::Label::textColourId, theme::accent);
    timeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (timeLabel);

    // pistes
    tracks.onOpenRequested = [this] { chooseFile(); };
    addAndMakeVisible (tracks);

    // panneau de droite : qualité
    for (auto* b : { &qFast, &qHigh, &qUltra })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        addAndMakeVisible (*b);
    }
    qFast.onClick  = [this] { if (qFast.getToggleState())  { proc.settings.quality = Quality::fast;  updateQualityButtons(); } };
    qHigh.onClick  = [this] { if (qHigh.getToggleState())  { proc.settings.quality = Quality::high;  updateQualityButtons(); } };
    qUltra.onClick = [this] { if (qUltra.getToggleState()) { proc.settings.quality = Quality::ultra; updateQualityButtons(); } };
    qualityInfo.setFont (theme::font (12.0f));
    qualityInfo.setColour (juce::Label::textColourId, theme::dim);
    qualityInfo.setJustificationType (juce::Justification::topLeft);
    qualityInfo.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (qualityInfo);

    for (auto* t : { &optVocals, &optDrums, &optGpu, &optDetail, &optSolo })
        addAndMakeVisible (*t);
    optVocals.setTooltip (lsplit::U ("Sépare la voix principale des chœurs"));
    optDrums.setTooltip (lsplit::U ("Sépare la batterie et la basse du reste des instruments"));
    optGpu.setTooltip (lsplit::U ("Utilise la carte graphique (DirectML) : beaucoup plus rapide. Sinon le processeur est utilisé."));
    optVocals.onClick = [this] { proc.settings.splitVocals = optVocals.getToggleState(); };
    optDrums.onClick  = [this] { proc.settings.splitDrumsBass = optDrums.getToggleState(); tracks.refresh(); };
    optGpu.onClick    = [this] { proc.settings.useGpu = optGpu.getToggleState(); };
    optDetail.setTooltip (lsplit::U ("Sépare aussi les guitares et le piano / claviers (modèle Demucs 6 sources, un peu plus long)"));
    optSolo.setTooltip (lsplit::U ("Expérimental : sépare la guitare solo (la mélodie) de la guitare rythmique"));
    optDetail.onClick = [this] { proc.settings.detailInstruments = optDetail.getToggleState(); syncFromProcessor(); tracks.refresh(); };
    optSolo.onClick   = [this] { proc.settings.splitGuitarSolo = optSolo.getToggleState(); tracks.refresh(); };
    optVocals.onClick = [this] { proc.settings.splitVocals = optVocals.getToggleState(); tracks.refresh(); };

    separateBtn.getProperties().set ("primary", true);
    separateBtn.onClick = [this]
    {
        if (proc.isBusy()) proc.cancelJob();
        else if (proc.getData() == nullptr) chooseFile();
        else proc.startSeparation();
    };
    addAndMakeVisible (separateBtn);

    // export
    format.addItem ("WAV 24 bits", 24);
    format.addItem ("WAV 16 bits", 16);
    format.addItem ("WAV 32 bits flottant", 32);
    format.setSelectedId (24, juce::dontSendNotification);
    addAndMakeVisible (format);
    expAll.onClick      = [this] { exportStems (0); };
    expAcapella.onClick = [this] { exportStems (1); };
    expInst.onClick     = [this] { exportStems (2); };
    expKaraoke.onClick  = [this] { exportStems (3); };
    expMix.onClick      = [this] { exportStems (4); };
    expFolder.onClick   = [this]
    {
        if (auto d = proc.getData(); d != nullptr && d->cacheDir.isDirectory())
            d->cacheDir.revealToUser();
    };
    expAll.setTooltip ("Un fichier WAV par stem");
    expAcapella.setTooltip (lsplit::U ("Voix lead + chœurs dans un seul fichier"));
    expInst.setTooltip ("Tout sauf les voix");
    expKaraoke.setTooltip (lsplit::U ("Tout sauf la voix lead (les chœurs restent)"));
    expMix.setTooltip ("Le mixage tel que vous l'entendez (volumes, solo, muet)");
    expFolder.setTooltip (lsplit::U ("Ouvrir le dossier où les stems calculés sont gardés"));
    for (auto* b : { &expAll, &expAcapella, &expInst, &expKaraoke, &expMix, &expFolder })
        addAndMakeVisible (*b);

    // taille adaptée à l'écran (portables 1366 x 768 compris)
    int w = 1400, h = 840;
    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = disp->userArea;
        w = juce::jlimit (940, 1400, area.getWidth() - 40);
        h = juce::jlimit (600, 840, area.getHeight() - 70);
    }
    setResizable (true, true);
    setResizeLimits (940, 600, 3200, 2000);
    setSize (w, h);
    setWantsKeyboardFocus (true);

    proc.addChangeListener (this);
    syncFromProcessor();
    startTimerHz (30);
}

LayersSplitEditor::~LayersSplitEditor()
{
    proc.removeChangeListener (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void LayersSplitEditor::updateQualityButtons()
{
    qFast.setToggleState (proc.settings.quality == Quality::fast, juce::dontSendNotification);
    qHigh.setToggleState (proc.settings.quality == Quality::high, juce::dontSendNotification);
    qUltra.setToggleState (proc.settings.quality == Quality::ultra, juce::dontSendNotification);
    switch (proc.settings.quality)
    {
        case Quality::fast:  qualityInfo.setText (lsplit::U ("Rapide : un passage par modèle. Idéal pour un premier essai."), juce::dontSendNotification); break;
        case Quality::high:  qualityInfo.setText (lsplit::U ("Haute : double passage anti-bruit sur les voix. Recommandé."), juce::dontSendNotification); break;
        case Quality::ultra: qualityInfo.setText (lsplit::U ("Ultra : deux modèles de voix combinés, anti-bruit partout. Le plus propre, environ 2x plus long."), juce::dontSendNotification); break;
    }
}

void LayersSplitEditor::syncFromProcessor()
{
    updateQualityButtons();
    optVocals.setToggleState (proc.settings.splitVocals, juce::dontSendNotification);
    optDrums.setToggleState (proc.settings.splitDrumsBass, juce::dontSendNotification);
    optGpu.setToggleState (proc.settings.useGpu, juce::dontSendNotification);
    optDetail.setToggleState (proc.settings.detailInstruments, juce::dontSendNotification);
    optSolo.setToggleState (proc.settings.splitGuitarSolo, juce::dontSendNotification);
    followBtn.setToggleState (proc.followHost.load(), juce::dontSendNotification);
    loopBtn.setToggleState (proc.loop.load(), juce::dontSendNotification);
    abBtn.setToggleState (proc.listenOriginal.load(), juce::dontSendNotification);
    master.setValue (proc.masterDb.load(), juce::dontSendNotification);

    const auto d = proc.getData();
    const bool busy = proc.isBusy();
    const bool hasStems = d != nullptr && d->hasStems;
    separateBtn.setButtonText (busy ? "ANNULER" : (d == nullptr ? "OUVRIR UN MORCEAU" : (hasStems ? lsplit::U ("SÉPARER À NOUVEAU") : lsplit::U ("SÉPARER"))));
    for (juce::Component* c : { (juce::Component*) &qFast, (juce::Component*) &qHigh, (juce::Component*) &qUltra,
                                (juce::Component*) &optVocals, (juce::Component*) &optDrums, (juce::Component*) &optGpu,
                                (juce::Component*) &optDetail,
                                (juce::Component*) &openBtn })
        c->setEnabled (! busy);
    optSolo.setEnabled (! busy && proc.settings.detailInstruments);
    for (auto* b : { &expAll, &expAcapella, &expInst, &expKaraoke, &expMix })
        b->setEnabled (hasStems && ! busy && ! exporting.load());
    expAcapella.setEnabled (expAcapella.isEnabled() && d->stems.present[stemBacking]);
    expKaraoke.setEnabled (expKaraoke.isEnabled() && d->stems.present[stemBacking]);
    expFolder.setEnabled (hasStems && d->cacheDir.isDirectory());
    playBtn.setEnabled (d != nullptr);
    stopBtn.setEnabled (d != nullptr);
    abBtn.setEnabled (hasStems);

    if (d != shownData)
    {
        shownData = d;
        tracks.refresh();
    }
    repaint();
}

void LayersSplitEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    exportStatus.clear();
    syncFromProcessor();
}

void LayersSplitEditor::timerCallback()
{
    const auto d = proc.getData();
    const double pos = proc.positionSeconds();
    timeLabel.setText (formatTime (pos, true) + (d != nullptr ? "  /  " + formatTime (d->durationSec()) : juce::String()),
                       juce::dontSendNotification);
    playBtn.setButtonText (proc.isPlaying() ? "Pause" : "Lecture");
    tracks.updatePlayhead();

    const bool busy = proc.isBusy();
    if (busy != wasBusy)
    {
        wasBusy = busy;
        syncFromProcessor();
    }
    if (busy)
        repaint (sideArea());
}

//==============================================================================
juce::Rectangle<int> LayersSplitEditor::sideArea() const
{
    return { getWidth() - sideW, topH, sideW, getHeight() - topH - footH };
}

juce::Rectangle<int> LayersSplitEditor::cardsArea() const
{
    auto s = sideArea().reduced (14, 12);
    s.removeFromTop (22);
    const int cardH = getHeight() < 700 ? 44 : (getHeight() < 780 ? 52 : 62);
    return s.removeFromTop (cardH * 3 + 16);
}

void LayersSplitEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (topH).reduced (12, 10);
    const bool narrow = getWidth() < 1250;
    top.removeFromLeft (narrow ? 150 : 200); // logo
    openBtn.setBounds (top.removeFromLeft (92));
    top.removeFromLeft (18);
    playBtn.setBounds (top.removeFromLeft (84));
    top.removeFromLeft (6);
    stopBtn.setBounds (top.removeFromLeft (62));
    top.removeFromLeft (8);
    timeLabel.setBounds (top.removeFromLeft (narrow ? 170 : 200));
    top.removeFromLeft (8);
    abBtn.setBounds (top.removeFromLeft (narrow ? 100 : 124));
    top.removeFromLeft (6);
    loopBtn.setBounds (top.removeFromLeft (narrow ? 64 : 72));
    top.removeFromLeft (6);
    if (followBtn.isVisible())
        followBtn.setBounds (top.removeFromLeft (118));
    master.setBounds (top.removeFromRight (narrow ? 100 : 150).reduced (0, 4));

    r.removeFromBottom (footH);
    auto side = r.removeFromRight (sideW);
    tracks.setBounds (r);

    // panneau de droite
    auto s = side.reduced (14, 12);
    s.removeFromTop (22);
    s.removeFromTop (cardsArea().getHeight());
    s.removeFromTop (16);
    s.removeFromTop (22); // titre « SÉPARATION »
    auto q = s.removeFromTop (30);
    const int qw = (q.getWidth() - 12) / 3;
    qFast.setBounds (q.removeFromLeft (qw));
    q.removeFromLeft (6);
    qHigh.setBounds (q.removeFromLeft (qw));
    q.removeFromLeft (6);
    qUltra.setBounds (q);
    // mode compact pour les petits écrans : tout reste visible
    const bool compact = getHeight() < 760;
    s.removeFromTop (4);
    qualityInfo.setVisible (! compact);
    if (! compact)
        qualityInfo.setBounds (s.removeFromTop (36));
    const int rowH = compact ? 22 : 24;
    // options sur deux colonnes
    auto twoCols = [&] (juce::Component& a, juce::Component* b)
    {
        auto row = s.removeFromTop (rowH);
        const int half = row.getWidth() / 2;
        a.setBounds (row.removeFromLeft (b != nullptr ? half : row.getWidth()));
        if (b != nullptr) b->setBounds (row);
    };
    twoCols (optVocals, &optDrums);
    twoCols (optDetail, &optSolo);
    twoCols (optGpu, nullptr);
    s.removeFromTop (compact ? 4 : 8);
    separateBtn.setBounds (s.removeFromTop (compact ? 34 : 40));
    s.removeFromTop (compact ? 48 : 54); // progression (dessinée)

    s.removeFromTop (compact ? 2 : 6);
    s.removeFromTop (22); // titre « EXPORT »
    format.setBounds (s.removeFromTop (compact ? 26 : 28));
    s.removeFromTop (compact ? 6 : 8);
    const int bh = compact ? 26 : 30, gap = compact ? 4 : 6;
    const int bw = (s.getWidth() - 8) / 2;
    juce::TextButton* btns[] = { &expAll, &expAcapella, &expInst, &expKaraoke, &expMix, &expFolder };
    for (int i = 0; i < 6; ++i)
        btns[i]->setBounds (s.getX() + (i % 2) * (bw + 8), s.getY() + (i / 2) * (bh + gap), bw, bh);
}

void LayersSplitEditor::paintCard (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title,
                                   const juce::String& value, const juce::String& sub) const
{
    g.setColour (theme::panel2);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
    auto in = r.reduced (10, 7);
    g.setColour (theme::dim);
    g.setFont (theme::font (10.0f, true));
    g.drawText (title, in.removeFromTop (13), juce::Justification::centredLeft);
    const bool small = r.getHeight() < 60;
    g.setColour (theme::textBright);
    g.setFont (theme::font (small ? 17.0f : 20.0f, true));
    g.drawFittedText (value, in.removeFromTop (small ? 22 : 26), juce::Justification::centredLeft, 1);
    g.setColour (theme::accent);
    g.setFont (theme::font (11.0f));
    g.drawFittedText (sub, in, juce::Justification::topLeft, 1);
}

void LayersSplitEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    // barre du haut
    auto top = getLocalBounds().removeFromTop (topH);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff171a21), 0, 0, theme::header, 0, (float) topH, false));
    g.fillRect (top);
    g.setColour (theme::border);
    g.drawHorizontalLine (topH - 1, 0.0f, (float) getWidth());
    {
        auto logo = top.reduced (16, 0).withWidth (190);
        const bool narrowLogo = getWidth() < 1250;
        g.setFont (theme::font (19.0f, true));
        g.setColour (theme::textBright);
        juce::GlyphArrangement ga;
        ga.addLineOfText (theme::font (19.0f, true), "LAYERS ", 0, 0);
        const float w1 = ga.getBoundingBox (0, -1, true).getWidth();
        g.drawText ("LAYERS", logo, juce::Justification::centredLeft);
        g.setColour (theme::accent);
        g.drawText ("SPLIT", logo.withTrimmedLeft ((int) w1), juce::Justification::centredLeft);
        g.setColour (theme::dim);
        g.setFont (theme::font (10.0f));
        if (! narrowLogo)
            g.drawText ("by JB Praise", logo.withTrimmedLeft ((int) w1 + 58).withTrimmedTop (6), juce::Justification::centredLeft);
    }

    // panneau de droite
    auto side = sideArea();
    g.setColour (theme::panel);
    g.fillRect (side);
    g.setColour (theme::border);
    g.drawVerticalLine (side.getX(), (float) side.getY(), (float) side.getBottom());

    auto s = side.reduced (14, 12);
    styleTitle (g, s.removeFromTop (22), "ANALYSE");

    const auto d = proc.getData();
    auto cards = cardsArea();
    const int cardH = (cards.getHeight() - 16) / 3;
    const int cw = (cards.getWidth() - 8) / 2;
    auto cell = [&] (int i) { return juce::Rectangle<int> (cards.getX() + (i % 2) * (cw + 8), cards.getY() + (i / 2) * (cardH + 8), cw, cardH); };

    const bool hasA = d != nullptr && d->hasAnalysis;
    const auto& a = d != nullptr ? d->analysis : AnalysisResult();
    juce::String bpm = lsplit::U ("—"), bpmSub = hasA ? lsplit::U ("non détecté") : lsplit::U ("après séparation");
    if (hasA && a.bpm > 0)
    {
        bpm = juce::String (a.bpm, std::abs (a.bpm - std::round (a.bpm)) < 0.05 ? 0 : 1);
        bpmSub = lsplit::U ("BPM · ") + juce::String (a.bpmConfidence > 0.5 ? "fiable" : (a.bpmConfidence > 0.2 ? "probable" : "incertain"));
    }
    juce::String key = lsplit::U ("—"), keySub = hasA ? lsplit::U ("non détectée") : lsplit::U ("après séparation");
    if (hasA && a.keyRoot >= 0 && a.keyConfidence > 0.05)
    {
        key = juce::String::fromUTF8 (a.keyName().c_str());
        keySub = juce::String (a.keyShort()) + lsplit::U (" · Camelot ") + juce::String (a.camelot());
    }
    paintCard (g, cell (0), "TEMPO", bpm, bpmSub);
    paintCard (g, cell (1), lsplit::U ("TONALITÉ"), key, keySub);
    paintCard (g, cell (2), "LOUDNESS", d != nullptr && a.lufs > -99 ? juce::String (a.lufs, 1) : lsplit::U ("—"), lsplit::U ("LUFS intégré"));
    paintCard (g, cell (3), lsplit::U ("CRÊTE"), d != nullptr && a.peakDb > -99 ? juce::String (a.peakDb, 1) : lsplit::U ("—"), lsplit::U ("dBFS (échantillon)"));
    paintCard (g, cell (4), lsplit::U ("DURÉE"), d != nullptr ? formatTime (d->durationSec()) : lsplit::U ("—"),
               d != nullptr ? juce::String (d->sampleRate / 1000.0, 1) + lsplit::U (" kHz · stéréo") : juce::String());
    juce::String stemsCount = lsplit::U ("—");
    if (d != nullptr && d->hasStems)
    {
        int c = 0;
        int voices = 0;
        for (int k = 0; k < numStems; ++k)
            (isChoirVoice (k) ? voices : c) += d->stems.present[k] ? 1 : 0;
        stemsCount = juce::String (c) + " stems" + (voices > 0 ? " + " + juce::String (voices) + " voix" : juce::String());
    }
    paintCard (g, cell (5), lsplit::U ("SÉPARATION"), stemsCount,
               d != nullptr && d->hasStems ? (d->deviceUsed.isNotEmpty() ? d->deviceUsed : juce::String ("depuis le cache")) : juce::String ("somme = original"));

    s.removeFromTop (cards.getHeight() + 16);
    styleTitle (g, s.removeFromTop (22), lsplit::U ("SÉPARATION IA"));

    // progression
    auto pr = juce::Rectangle<int> (separateBtn.getX(), separateBtn.getBottom() + 8, separateBtn.getWidth(), 44);
    const auto& job = proc.getJob();
    if (proc.isBusy())
    {
        const double p = job.progress();
        g.setColour (theme::text);
        g.setFont (theme::font (12.0f));
        g.drawText (job.stepText(), pr.withHeight (16), juce::Justification::centredLeft, true);
        g.drawText (juce::String ((int) std::round (p * 100.0)) + " %", pr.withHeight (16), juce::Justification::centredRight);
        auto bar = pr.withY (pr.getY() + 20).withHeight (6).toFloat();
        g.setColour (juce::Colour (0xff22262f));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setGradientFill (juce::ColourGradient (theme::accent, bar.getX(), 0, juce::Colour (0xff7c6cf0), bar.getRight(), 0, false));
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * (float) p)), 3.0f);
        const double el = job.elapsedSeconds();
        juce::String t = lsplit::U ("Écoulé ") + formatTime (el);
        if (p > 0.03 && p < 0.999)
            t << lsplit::U ("  ·  reste ≈ ") << formatTime (el * (1.0 - p) / p);
        g.setColour (theme::dim);
        g.setFont (theme::font (11.0f));
        g.drawText (juce::String::fromUTF8 (t.toRawUTF8()), pr.withY (pr.getY() + 30).withHeight (14), juce::Justification::centredLeft);
    }
    else if (d != nullptr && d->hasStems)
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (11.5f));
        g.drawFittedText (lsplit::U ("Les stems additionnés redonnent exactement le mix original. Glissez une piste vers votre DAW pour l'importer."),
                          pr, juce::Justification::centredLeft, 3);
    }

    // titre export
    styleTitle (g, juce::Rectangle<int> (format.getX(), format.getY() - 24, format.getWidth(), 22), "EXPORT");

    // pied de page
    auto foot = getLocalBounds().removeFromBottom (footH);
    g.setColour (juce::Colour (0xff07080a));
    g.fillRect (foot);
    g.setColour (theme::border);
    g.drawHorizontalLine (foot.getY(), 0.0f, (float) getWidth());
    auto f = foot.reduced (14, 0);
    const bool err = exportStatus.isNotEmpty() ? exportError : proc.statusIsError();
    const auto msg = exportStatus.isNotEmpty() ? exportStatus : proc.getStatus();
    g.setColour (err ? theme::error : (proc.isBusy() ? theme::warn : juce::Colour (0xff45d483)));
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) f.getX() + 4.0f, (float) f.getCentreY() }));
    g.setColour (err ? theme::error : theme::text);
    g.setFont (theme::font (12.0f));
    g.drawText (msg.isNotEmpty() ? msg : juce::String (lsplit::U ("Prêt. Ouvrez ou glissez un morceau.")),
                f.withTrimmedLeft (16).withTrimmedRight (420), juce::Justification::centredLeft, true);
    g.setColour (theme::dim);
    g.setFont (theme::font (11.0f));
    g.drawText (proc.engineDescription() + lsplit::U ("  ·  Layers Split ") + JucePlugin_VersionString, f, juce::Justification::centredRight);
}

bool LayersSplitEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey)
    {
        proc.togglePlay();
        return true;
    }
    if (k == juce::KeyPress::homeKey)
    {
        proc.seekSeconds (0.0);
        return true;
    }
    return false;
}

//==============================================================================
void LayersSplitEditor::chooseFile()
{
    if (proc.isBusy()) return;
    auto start = proc.getData() != nullptr ? proc.getData()->sourceFile.getParentDirectory()
                                           : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Choisir un morceau", start, audioFileWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<LayersSplitEditor> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  safe->proc.openFile (f);
                          });
}

void LayersSplitEditor::exportStems (int preset)
{
    const auto d = proc.getData();
    if (d == nullptr || ! d->hasStems || exporting.load()) return;

    const int bits = format.getSelectedId() > 0 ? format.getSelectedId() : 24;
    float gains[numStems];
    const bool anySolo = proc.anySoloInMix (*d);
    for (int k = 0; k < numStems; ++k)
    {
        const auto& c = proc.controls[(size_t) k];
        gains[k] = (! proc.stemInMix (*d, k) || c.mute.load() || (anySolo && ! c.solo.load())) ? 0.0f
                 : juce::Decibels::decibelsToGain (c.gainDb.load(), -60.0f) * juce::Decibels::decibelsToGain (proc.masterDb.load(), -60.0f);
    }
    std::vector<float> g (gains, gains + numStems);

    chooser = std::make_unique<juce::FileChooser> (lsplit::U ("Dossier où enregistrer les stems"),
                                                   d->sourceFile.getParentDirectory());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<LayersSplitEditor> (this), d, preset, bits, g] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto dir = fc.getResult();
        if (dir == juce::File() || ! dir.isDirectory()) return;

        safe->exporting = true;
        safe->exportStatus = lsplit::U ("Export en cours…");
        safe->exportError = false;
        safe->syncFromProcessor();

        juce::Thread::launch ([safe, d, preset, bits, g, dir]
        {
            const auto base = juce::File::createLegalFileName (d->sourceFile.getFileNameWithoutExtension());
            juce::String err;
            juce::Array<juce::File> written;
            auto sumOf = [&] (std::initializer_list<int> ids, const float* gainOrNull)
            {
                StereoBuffer b;
                b.resize (d->length());
                for (int k : ids)
                {
                    if (! d->stems.present[k]) continue;
                    const float gk = gainOrNull != nullptr ? gainOrNull[k] : 1.0f;
                    if (gk <= 0.0f) continue;
                    const auto& st = d->stems.stems[k];
                    for (size_t i = 0; i < b.size(); ++i) { b.l[i] += st.l[i] * gk; b.r[i] += st.r[i] * gk; }
                }
                return b;
            };
            auto write = [&] (const juce::String& suffix, const StereoBuffer& b)
            {
                const auto f = dir.getChildFile (base + " - " + suffix + ".wav");
                if (err.isEmpty() && writeWav (f, b, d->sampleRate, bits, err))
                    written.add (f);
            };

            switch (preset)
            {
                case 0:
                    for (int k = 0; k < numStems; ++k)
                        if (d->stems.present[k])
                            write (stemFileName (k), d->stems.stems[k]);
                    break;
                case 1: write ("Acapella", sumOf ({ stemLead, stemBacking }, nullptr)); break;
                case 2: write ("Instrumental", sumOf ({ stemDrums, stemBass, stemGuitar, stemGuitarSolo, stemPiano, stemOther }, nullptr)); break;
                case 3: write ("Sans voix lead", sumOf ({ stemBacking, stemDrums, stemBass, stemGuitar, stemGuitarSolo, stemPiano, stemOther }, nullptr)); break;
                case 4: write ("Mixage", sumOf ({ stemLead, stemBacking, stemDrums, stemBass, stemGuitar, stemGuitarSolo, stemPiano, stemOther,
                                                  stemSoprano, stemAlto, stemTenor, stemBassVoice }, g.data())); break;
                default: break;
            }

            juce::MessageManager::callAsync ([safe, err, written, dir]
            {
                if (safe == nullptr) return;
                safe->exporting = false;
                if (err.isNotEmpty())
                {
                    safe->exportStatus = "Export impossible : " + err;
                    safe->exportError = true;
                }
                else
                {
                    safe->exportStatus = juce::String (written.size()) + (written.size() > 1 ? lsplit::U (" fichiers exportés dans ") : lsplit::U (" fichier exporté dans "))
                                       + dir.getFullPathName();
                    safe->exportError = false;
                    if (! written.isEmpty())
                        written.getFirst().revealToUser();
                }
                safe->syncFromProcessor();
            });
        });
    });
}
