// Spacepad by JB Praise — côté crowd : panneau des effets (PAD / CROWD), bouton PAD / CROWD au centre,
// la grande salle animée et les deux crowds à toucher.
#include "UIHelpers.h"
#include "BinaryData.h"

namespace spacepad
{
static const juce::Colour crowdCol[2] = { juce::Colour (0xffffb86b), juce::Colour (0xffff8a6b) };

// ============================================================================================
// FxPanel
// ============================================================================================
FxPanel::FxPanel (SpacepadProcessor& p) : proc (p)
{
    struct K { const char* id; const char* name; juce::uint32 col; bool accent; };
    const K pk[] = { { "shimmer", "Shimmer", 0xffbdf1ff, false }, { "warm", "Warm", 0xffffb86b, false }, { "air", "Air", 0, true },
                     { "reverb", "Reverb", 0xffa78bfa, false }, { "chorus", "Chorus", 0xffff9ecf, false }, { "delay", "Delay", 0xff7fd6ff, false },
                     { "motion", "Motion", 0xff5cffb0, false }, { "width", "Width", 0, true }, { "sub", "Sub oct.", 0xff3d7bff, false },
                     { "lowcut", "Low cut", 0xff9aa6bd, false }, { "attack", "Attack", 0xffffb3d9, false }, { "xfade", "Fondu", 0xffffcf8a, false },
                     { "dtime", "Temps", 0xff7fd6ff, false }, { "padvol", "Vol. Pad", 0, true }, { "crowdvol", "Vol. Crowd", 0xffffb86b, false } };
    auto tempoText = [this] (const char* id)
    {
        return [this, id = juce::String (id)]
        {
            auto* prm = proc.params.getParameter (id);
            return prm->getText (prm->getValue(), 8) + U (" \xc2\xb7 ") + juce::String (juce::roundToInt (proc.tempoBpm())) + (proc.hostTempo() ? " DAW" : "");
        };
    };
    for (auto& k : pk)
    {
        padKnobs.push_back (std::make_unique<Knob> (proc, k.id, U (k.name), k.accent ? theme::accent : juce::Colour (k.col)));
        padKnobs.back()->usesAccent = k.accent;
        if (juce::String (k.id) == "dtime") padKnobs.back()->valueText = tempoText ("dtime");
        addChildComponent (*padKnobs.back());
    }
    const K ck[] = { { "chorus", "Chorus", 0xffff9ecf, false }, { "delay", "Delay", 0xff7fd6ff, false }, { "dtime", "Temps", 0xff7fd6ff, false },
                     { "dfb", "R\xc3\xa9p\xc3\xa9titions", 0xff7fd6ff, false }, { "reverb", "Reverb", 0xffa78bfa, false }, { "width", "Width", 0xff5cc8ff, false },
                     { "dist", "Distance", 0xffffcf8a, false }, { "move", "Mouvement", 0xff5cffb0, false }, { "lowcut", "Low cut", 0xff9aa6bd, false },
                     { "attack", "Attack", 0xffffb3d9, false }, { "fade", "Fondu", 0xffffcf8a, false }, { "vol", "Vol. Crowd", 0xffffb86b, false } };
    for (int s = 0; s < 2; ++s)
        for (auto& k : ck)
        {
            const bool vol = juce::String (k.id) == "vol";
            const auto id = vol ? juce::String (s == 0 ? "crowdvol" : "crowdvol2") : SpacepadProcessor::crowdParam (s, k.id);
            crowdKnobs[s].push_back (std::make_unique<Knob> (proc, id, U (k.name), vol ? crowdCol[s] : juce::Colour (k.col)));
            if (juce::String (k.id) == "dtime") crowdKnobs[s].back()->valueText = tempoText (id.toRawUTF8());
            addChildComponent (*crowdKnobs[s].back());
        }

    for (auto* b : { &freezeBtn, &swellBtn, &tapBtn, &soloBtn })
    {
        b->getProperties().set ("weight", 2);
        b->getProperties().set ("kerning", 0.14f);
        addChildComponent (*b);
    }
    freezeBtn.setClickingTogglesState (true);
    swellBtn.setClickingTogglesState (true);
    swellBtn.setColour (juce::TextButton::buttonOnColourId, theme::violet);
    swellBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff12082e));
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "freeze", freezeBtn);
    swellAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "swell", swellBtn);
    tapBtn.onClick = [this] { proc.tapTempo(); tick(); };
    soloBtn.setClickingTogglesState (true);
    soloBtn.setColour (juce::TextButton::buttonOnColourId, theme::amber);
    soloBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1300));
    soloBtn.onClick = [this] { proc.setSolo (soloBtn.getToggleState()); };
    retranslate();
    showPage (uiSettings().fxPage);
}

void FxPanel::retranslate()
{
    if (padKnobs.size() >= 15)
    {
        padKnobs[10]->setLabel (L ("Attack", "Attack", "Ataque"));
        padKnobs[11]->setLabel (L ("Fondu", "Crossfade", "Fundido"));
        padKnobs[12]->setLabel (L ("Temps", "Time", "Tiempo"));
    }
    for (int s = 0; s < 2; ++s)
        if (crowdKnobs[s].size() >= 12)
        {
            crowdKnobs[s][2]->setLabel (L ("Temps", "Time", "Tiempo"));
            crowdKnobs[s][3]->setLabel (L ("R\xc3\xa9p\xc3\xa9titions", "Repeats", "Repeticiones"));
            crowdKnobs[s][6]->setLabel (L ("Distance", "Distance", "Distancia"));
            crowdKnobs[s][7]->setLabel (L ("Mouvement", "Movement", "Movimiento"));
            crowdKnobs[s][9]->setLabel (L ("Attack", "Attack", "Ataque"));
            crowdKnobs[s][10]->setLabel (L ("Fondu", "Fade", "Fundido"));
        }
    soloBtn.setButtonText (L ("\xc3\x89""COUTER SEUL", "SOLO", "ESCUCHAR SOLO"));
    soloBtn.setTooltip (L ("Coupe le pad pour entendre les crowds pendant le r\xc3\xa9glage", "Mutes the pad so you hear the crowds while adjusting", "Silencia el pad para o\xc3\xadr los crowds"));
    tapBtn.setTooltip (L ("Tapez le tempo du chant (pour le delay)", "Tap the song tempo (for the delay)", "Marca el tempo de la canci\xc3\xb3n (para el delay)"));
    tick();
    repaint();
}

void FxPanel::applyColours()
{
    for (auto& k : padKnobs) if (k->usesAccent) k->setColour (theme::accent);
}

void FxPanel::showPage (int pg)
{
    pageNow = pg == 1 ? 1 : 0;
    for (auto& k : padKnobs) k->setVisible (pageNow == 0);
    for (int s = 0; s < 2; ++s) for (auto& k : crowdKnobs[s]) k->setVisible (pageNow == 1 && s == slot);
    freezeBtn.setVisible (pageNow == 0);
    swellBtn.setVisible (pageNow == 0);
    tapBtn.setVisible (pageNow == 1);
    soloBtn.setVisible (pageNow == 1);
    if (pageNow == 0 && proc.solo()) { proc.setSolo (false); soloBtn.setToggleState (false, juce::dontSendNotification); }
    if (uiSettings().fxPage != pageNow) { auto u = uiSettings(); u.fxPage = pageNow; setUiSettings (u); }
    resized();
    repaint();
}

void FxPanel::showSlot (int s)
{
    slot = s & 1;
    showPage (pageNow);
}

void FxPanel::tick()
{
    tapBtn.setButtonText ((proc.hostTempo() ? juce::String ("DAW  ") : juce::String ("TAP  ")) + juce::String (juce::roundToInt (proc.tempoBpm())));
    soloBtn.setToggleState (proc.solo(), juce::dontSendNotification);
    const auto nm = proc.crowdSoundName (slot) + juce::String ((int) proc.crowdSounding (slot)) + juce::String ((int) proc.crowdSounding (1 - slot));
    if (nm != lastName) { lastName = nm; repaint (nameArea.getUnion (slotArea)); }
    if (pageNow == 1) for (auto& k : crowdKnobs[slot]) if (k->isVisible()) k->repaint();
}

void FxPanel::resized()
{
    auto r = getLocalBounds().reduced (18, 14);
    tabsArea = r.removeFromTop (40);
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (50);
    r.removeFromBottom (8);
    auto left = bottom.removeFromLeft (bottom.getWidth() / 2).reduced (5, 0);
    freezeBtn.setBounds (left);
    tapBtn.setBounds (left);
    swellBtn.setBounds (bottom.reduced (5, 0));
    soloBtn.setBounds (bottom.reduced (5, 0));
    slotArea = nameArea = {};
    if (pageNow == 1)
    {
        slotArea = r.removeFromTop (32);
        nameArea = r.removeFromTop (22);
        r.removeFromTop (4);
    }
    auto& list = pageNow == 0 ? padKnobs : crowdKnobs[slot];
    const int rowsN = ((int) list.size() + 2) / 3;
    const int cw = r.getWidth() / 3, ch = r.getHeight() / juce::jmax (1, rowsN);
    for (int i = 0; i < (int) list.size(); ++i)
        list[(size_t) i]->setBounds (r.getX() + (i % 3) * cw, r.getY() + (i / 3) * ch, cw, juce::jmin (ch, 116));
    // l'autre page garde des positions valides (elle est cachée)
    auto& other = pageNow == 0 ? crowdKnobs[slot] : padKnobs;
    for (int i = 0; i < (int) other.size(); ++i) other[(size_t) i]->setBounds (list.empty() ? juce::Rectangle<int>() : list[0]->getBounds());
}

void FxPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (theme::panel2, 0, 0, theme::panel, b.getRight(), 0, false));
    g.fillRect (b);
    g.setColour (theme::line);
    g.drawLine (0.5f, 0, 0.5f, b.getBottom());

    // EFFETS PAD | EFFETS CROWD
    auto t = tabsArea.toFloat();
    g.setColour (theme::ink.withAlpha (0.06f));
    g.fillRoundedRectangle (t, t.getHeight() * 0.5f);
    const juce::String names[] = { L ("EFFETS PAD", "PAD FX", "EFECTOS PAD"), L ("EFFETS CROWD", "CROWD FX", "EFECTOS CROWD") };
    for (int i = 0; i < 2; ++i)
    {
        auto cr = t.reduced (4).withWidth ((t.getWidth() - 8) / 2).translated (i * (t.getWidth() - 8) / 2, 0);
        const bool on = i == pageNow;
        const auto col = i == 0 ? theme::accent : theme::amber;
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (col.brighter (0.3f), cr.getX(), cr.getY(), col.darker (0.15f), cr.getX(), cr.getBottom(), false));
            g.fillRoundedRectangle (cr, cr.getHeight() * 0.5f);
        }
        auto f = theme::ui (12.0f, 2); f.setExtraKerningFactor (0.1f);
        g.setFont (f);
        g.setColour (on ? (i == 0 ? juce::Colour (0xff04121c) : juce::Colour (0xff2a1300)) : theme::text.withAlpha (0.8f));
        g.drawText (names[i], cr, juce::Justification::centred);
    }
    if (pageNow != 1) return;
    // CROWD 1 | CROWD 2
    auto sa = slotArea.toFloat();
    for (int s = 0; s < 2; ++s)
    {
        auto cr = sa.withWidth (sa.getWidth() / 2 - 4).translated (s * (sa.getWidth() / 2 + 4), 0);
        const bool on = s == slot;
        g.setColour (on ? crowdCol[s].withAlpha (0.22f) : theme::ink.withAlpha (0.05f));
        g.fillRoundedRectangle (cr, cr.getHeight() * 0.5f);
        if (on) { g.setColour (crowdCol[s].withAlpha (0.8f)); g.drawRoundedRectangle (cr.reduced (0.5f), cr.getHeight() * 0.5f, 1.2f); }
        auto dot = juce::Rectangle<float> (cr.getX() + 14, cr.getCentreY() - 4, 8, 8);
        g.setColour (proc.crowdSounding (s) ? crowdCol[s] : theme::muted.withAlpha (0.5f));
        g.fillEllipse (dot);
        auto f = theme::ui (11.5f, 2); f.setExtraKerningFactor (0.14f);
        g.setFont (f);
        g.setColour (on ? theme::text : theme::muted);
        g.drawText ("CROWD " + juce::String (s + 1), cr.withTrimmedLeft (18), juce::Justification::centred);
    }
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.0f));
    const auto nm = proc.crowdSounding (slot) ? proc.crowdSoundName (slot) : L ("aucun son pour l'instant", "no sound yet", "ning\xc3\xban sonido por ahora");
    g.drawText (nm + (proc.crowdSounding (slot) && proc.crowdPrefs().hasFx (proc.crowdSoundKey (slot)) ? U ("  \xc2\xb7  ") + L ("r\xc3\xa9glages m\xc3\xa9moris\xc3\xa9s", "settings remembered", "ajustes recordados") : juce::String()),
                nameArea.toFloat(), juce::Justification::centred, true);
}

void FxPanel::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (tabsArea.contains (p)) { showPage (p.x < tabsArea.getCentreX() ? 0 : 1); return; }
    if (pageNow == 1 && slotArea.contains (p)) showSlot (p.x < slotArea.getCentreX() ? 0 : 1);
}

// ============================================================================================
// MixBar : toucher « PAD » ou « CROWD » ouvre ses effets
// ============================================================================================
void MixBar::mouseUp (const juce::MouseEvent& e)
{
    if (! onLabel) return;
    if (e.x < 62) onLabel (0);
    else if (e.x > getWidth() - 62) onLabel (1);
}

// ============================================================================================
// CenterSwitch
// ============================================================================================
void CenterSwitch::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
    for (int i = 0; i < 2; ++i)
    {
        auto cr = b.reduced (4).withWidth ((b.getWidth() - 8) / 2).translated (i * (b.getWidth() - 8) / 2, 0);
        const bool on = i == value;
        const auto col = i == 0 ? theme::accent : theme::amber;
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (col.brighter (0.3f), cr.getX(), cr.getY(), col.darker (0.15f), cr.getX(), cr.getBottom(), false));
            g.fillRoundedRectangle (cr, cr.getHeight() * 0.5f);
        }
        const auto ink = on ? (i == 0 ? juce::Colour (0xff04121c) : juce::Colour (0xff2a1300)) : juce::Colour (0xffe8edf7).withAlpha (0.8f);
        auto f = theme::ui (12.0f, 2); f.setExtraKerningFactor (0.14f);
        g.setFont (f);
        const juce::String txt = i == 0 ? "PAD" : "CROWD";
        const float tw = juce::GlyphArrangement::getStringWidth (f, txt);
        const float x0 = cr.getCentreX() - (tw + 18) / 2;
        g.setColour (ink);
        if (i == 0) g.drawEllipse (x0, cr.getCentreY() - 5, 10, 10, 1.8f);
        else
        {
            // deux petites silhouettes
            g.fillEllipse (x0 + 1, cr.getCentreY() - 6, 5, 5);
            g.fillEllipse (x0 + 7, cr.getCentreY() - 6, 5, 5);
            g.fillRoundedRectangle (x0, cr.getCentreY(), 13, 6, 3);
        }
        g.drawText (txt, juce::Rectangle<float> (x0 + 18, cr.getY(), tw + 4, cr.getHeight()), juce::Justification::centredLeft);
    }
}

void CenterSwitch::mouseUp (const juce::MouseEvent& e)
{
    value = e.x < getWidth() / 2 ? 0 : 1;
    repaint();
    if (onChange) onChange (value);
}

// ============================================================================================
// HallView : la grande salle (dessinée une fois, puis animée par la lumière et les poussières)
// ============================================================================================
// la photo de la foule (mains levées, lumière de scène), traitée une fois : douce (floutée), naturelle ou teinte Spacepad
static void drawCrowdPhoto (juce::Image& img, int style)
{
    auto photo = juce::ImageCache::getFromMemory (BinaryData::foule_jpg, BinaryData::foule_jpgSize).convertedToFormat (juce::Image::ARGB);
    if (! photo.isValid()) return;
    if (style == 0)
    {
        juce::ImageConvolutionKernel k (15);
        k.createGaussianBlur (6.0f);
        auto blurred = photo.createCopy();
        k.applyToImage (blurred, photo, photo.getBounds());
        photo = blurred;
    }
    const int W = img.getWidth(), H = img.getHeight();
    {
        // remplir toute la zone en gardant les proportions (recadré un peu vers le haut : la lumière de la scène)
        juce::Graphics g (img);
        const float sc = juce::jmax ((float) W / photo.getWidth(), (float) H / photo.getHeight());
        const float dw = photo.getWidth() * sc, dh = photo.getHeight() * sc;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (photo, juce::Rectangle<float> ((W - dw) * 0.5f, (H - dh) * 0.35f, dw, dh));
    }
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    auto lerp = [] (float a, float b, float t) { return a + (b - a) * t; };
    for (int y = 0; y < bd.height; ++y)
    {
        // assombri pour que les crowds restent lisibles ; le bas s'efface vers les boutons
        const float fy = (float) y / bd.height;
        const float m = (style == 0 ? 0.62f : 0.72f) * (1.0f - 0.55f * juce::jlimit (0.0f, 1.0f, (fy - 0.6f) / 0.4f));
        for (int x = 0; x < bd.width; ++x)
        {
            auto* px = bd.getPixelPointer (x, y);
            auto* pp = reinterpret_cast<juce::PixelARGB*> (px);
            float r = pp->getRed() / 255.0f, g = pp->getGreen() / 255.0f, b = pp->getBlue() / 255.0f;
            if (style == 2)
            {
                // teinte Spacepad : ombres bleu nuit, milieux ambre, lumières dorées
                const float t = juce::jlimit (0.0f, 1.0f, (0.3f * r + 0.59f * g + 0.11f * b) * 1.1f);
                if (t < 0.5f) { const float u = t / 0.5f; r = lerp (0.03f, 0.62f, u); g = lerp (0.03f, 0.30f, u); b = lerp (0.10f, 0.12f, u); }
                else { const float u = (t - 0.5f) / 0.5f; r = lerp (0.62f, 1.0f, u); g = lerp (0.30f, 0.86f, u); b = lerp (0.12f, 0.62f, u); }
            }
            pp->setARGB (255, (juce::uint8) juce::jlimit (0, 255, (int) (r * m * 255)), (juce::uint8) juce::jlimit (0, 255, (int) (g * m * 255)), (juce::uint8) juce::jlimit (0, 255, (int) (b * m * 255)));
        }
    }
}

void HallView::render (float px)
{
    const int w = getWidth(), h = getHeight();
    image = makeCacheImage (juce::roundToInt (w * px), juce::roundToInt (h * px));
    imageScale = px;
    imageStyle = uiSettings().crowdBg;
    if (imageStyle < 3) { drawCrowdPhoto (image, imageStyle); return; }
    juce::Graphics g (image);
    // dessin dans un espace de 676 × 734, étiré à la taille réelle
    const float W = 676, H = 734, vx = W / 2, vy = 300;
    g.addTransform (juce::AffineTransform::scale (w * px / W, h * px / H));
    {
        juce::ColourGradient bgr (juce::Colour (0xffffcf8a), vx, H * 0.38f, juce::Colour (0xff07060b), vx, H * 0.38f + H * 0.72f, true);
        bgr.addColour (0.12, juce::Colour (0xffe59a4a));
        bgr.addColour (0.35, juce::Colour (0xff5a2f1c));
        bgr.addColour (0.7, juce::Colour (0xff1a0f12));
        g.setGradientFill (bgr);
        g.fillRect (0.0f, 0.0f, W, H);
    }
    // sol
    {
        juce::Path fl;
        fl.addTriangle (0, H, W, H, vx + 60, vy + 40);
        fl.addTriangle (0, H, vx + 60, vy + 40, vx - 60, vy + 40);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xe63a2216), 0, vy + 40, juce::Colour (0xe607050a), 0, H, false));
        g.fillPath (fl);
    }
    // vitrail rond
    const juce::Point<float> rose (vx, vy - 120);
    {
        juce::ColourGradient rg (juce::Colour (0xfffff3d6), rose.x, rose.y, juce::Colour (0x003a1a3a), rose.x + 58, rose.y, true);
        rg.addColour (0.4, juce::Colour (0xffffc06a));
        rg.addColour (0.8, juce::Colour (0x99a04a6a));
        g.setGradientFill (rg);
        g.fillEllipse (rose.x - 58, rose.y - 58, 116, 116);
        g.setColour (juce::Colour (0xbf4a2414));
        for (int i = 0; i < 12; ++i)
        {
            const float a = i * juce::MathConstants<float>::pi / 6;
            g.drawLine (rose.x, rose.y, rose.x + std::cos (a) * 44, rose.y + std::sin (a) * 44, 2.2f);
        }
        g.drawEllipse (rose.x - 22, rose.y - 22, 44, 44, 2.0f);
    }
    // lumière de l'autel
    softGlow (g, { vx, vy + 20 }, 20, 130, juce::Colour (0xffffd08a).withAlpha (0.45f));
    // rayons de lumière
    auto ray = [&] (float x0, float x1, float xb0, float xb1, float o)
    {
        for (int k = 0; k < 3; ++k)
        {
            const float sp = k * 6.0f;
            juce::Path p;
            p.startNewSubPath (x0 - sp, vy - 200); p.lineTo (x1 + sp, vy - 200); p.lineTo (xb1 + sp * 3, H); p.lineTo (xb0 - sp * 3, H); p.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffe2b0).withAlpha (0.5f * o / (1 + k)), 0, vy - 200, juce::Colour (0x00ffe2b0), 0, H, false));
            g.fillPath (p);
        }
    };
    const float rays[3][3] = { { -40, 40, 0.3f }, { 20, 26, 0.22f }, { 60, 34, 0.26f } };
    for (auto& rr : rays)
    {
        const float x0 = rr[0], w0 = rr[1], o = rr[2];
        ray (vx - 80 + x0, vx - 80 + x0 + w0, vx - 260 + x0 * 3, vx - 260 + x0 * 3 + w0 * 3, o);
        ray (vx + 80 - x0 - w0, vx + 80 - x0, vx + 260 - x0 * 3 - w0 * 3, vx + 260 - x0 * 3, o * 0.8f);
    }
    // colonnes et arches qui s'éloignent
    for (int i = 6; i >= 0; --i)
    {
        const float t = std::pow (0.72f, (float) i), half = 400 * t, top = vy - 430 * t, bot = vy + 60 + (H - vy) * t * 0.95f, cw = 26 * t + 3;
        const float op = 0.55f + 0.45f * (1.0f - i / 7.0f);
        for (int sd = -1; sd <= 1; sd += 2)
        {
            const float x = vx + sd * half;
            juce::ColourGradient cg (juce::Colour (0xff2a1a14).withAlpha (op), x - cw / 2, 0, juce::Colour (0xff1a110e).withAlpha (op), x + cw / 2, 0, false);
            cg.addColour (0.5, juce::Colour (0xff7a4a2a).withAlpha (op));
            g.setGradientFill (cg);
            g.fillRect (x - cw / 2, top, cw, bot - top);
            // fenêtres
            const float wx = vx + sd * half * 0.6f;
            juce::Path wp;
            wp.startNewSubPath (wx - 10 * t, top + 150 * t); wp.lineTo (wx - 10 * t, top + 40 * t);
            wp.quadraticTo (wx, top + 15 * t, wx + 10 * t, top + 40 * t); wp.lineTo (wx + 10 * t, top + 150 * t); wp.closeSubPath();
            g.setColour (juce::Colour (0xffffb86b).withAlpha (0.35f * (1.0f - i / 8.0f)));
            g.fillPath (wp);
        }
        const float ah = half * 1.05f;
        juce::Path arch;
        arch.startNewSubPath (vx - half, top);
        arch.quadraticTo (vx - half, top - ah * 0.75f, vx, top - ah);
        arch.quadraticTo (vx + half, top - ah * 0.75f, vx + half, top);
        g.setColour (juce::Colour (0xff6a3e24).withAlpha (op));
        g.strokePath (arch, juce::PathStrokeType (7 * t + 1.5f));
    }
    // la foule, mains levées
    juce::Random r (11);
    for (int row = 2; row >= 0; --row)
    {
        const float y0 = H - 30 - row * 38, sc = 1.0f - row * 0.22f;
        const int n = (int) (16 / sc);
        const auto c = row == 0 ? juce::Colour (0xff050307) : row == 1 ? juce::Colour (0xff0d0709) : juce::Colour (0xff1a0f0e);
        g.setColour (c);
        for (int k = 0; k < n; ++k)
        {
            const float x = (k + 0.5f + (row % 2) * 0.5f) * W / n + (r.nextFloat() - 0.5f) * 10, hr = 11 * sc;
            g.fillEllipse (x - hr * 2, y0 - hr * 1.6f, hr * 4, hr * 3.2f);
            g.fillEllipse (x - hr, y0 - hr * 2.8f, hr * 2, hr * 2);
            if (r.nextFloat() < 0.45f)
            {
                const float sd = r.nextBool() ? -1.0f : 1.0f;
                g.drawLine (x + sd * hr * 1.2f, y0 - hr * 0.6f, x + sd * hr * 2.2f, y0 - hr * 4.2f, hr * 0.8f);
                if (r.nextFloat() < 0.6f) g.drawLine (x - sd * hr * 1.2f, y0 - hr * 0.6f, x - sd * hr * 2.0f, y0 - hr * 4.0f, hr * 0.8f);
            }
        }
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00050307), 0, H * 0.55f, juce::Colour (0xf2050307), 0, H, false));
    g.fillRect (0.0f, H * 0.55f, W, H * 0.45f);
}

void HallView::tick (float dt)
{
    t += dt;
    const float target = juce::jlimit (0.0f, 1.0f, (proc.crowdLevel (0) + proc.crowdLevel (1)) * 2.5f);
    glow += (target - glow) * (1.0f - std::exp (-dt / (target > glow ? 0.25f : 1.2f)));
    if (dust.empty())
        for (int i = 0; i < 70; ++i) dust.push_back ({ rng.nextFloat(), rng.nextFloat() * 0.8f, 0.004f + rng.nextFloat() * 0.012f, rng.nextFloat() * 6.28f, 0.6f + rng.nextFloat() * 1.2f });
    if (! uiSettings().animations) return;
    for (auto& d : dust)
    {
        d.y -= d.v * dt * (0.6f + glow);
        d.x += std::sin (t * 0.4f + d.a) * 0.0009f;
        if (d.y < 0.02f) { d.y = 0.8f; d.x = rng.nextFloat(); }
    }
}

void HallView::paint (juce::Graphics& g)
{
    const float px = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! image.isValid() || std::abs (px - imageScale) > 0.01f || imageStyle != uiSettings().crowdBg) render (px);
    g.setOpacity (1.0f);
    g.drawImage (image, getLocalBounds().toFloat());
    const float w = (float) getWidth(), h = (float) getHeight();
    const float br = 0.6f + 0.8f * uiSettings().brightness;
    const float breath = 0.5f + 0.5f * std::sin (t * 0.7f);
    // la lumière du fond respire avec le son des crowds
    softGlow (g, { w * 0.5f, h * 0.43f }, 10, w * 0.32f, juce::Colour (0xffffd9a0).withAlpha (juce::jlimit (0.0f, 0.6f, (0.05f + 0.06f * breath + 0.32f * glow) * br)));
    softGlow (g, { w * 0.5f, h * (180.0f / 734.0f) }, 6, w * 0.13f, juce::Colour (0xfffff0d0).withAlpha (juce::jlimit (0.0f, 0.6f, (0.05f + 0.25f * glow) * br)));
    for (auto& d : dust)
    {
        const float a = (0.25f + 0.35f * (0.5f + 0.5f * std::sin (t * 1.3f + d.a))) * (0.6f + 0.6f * glow);
        g.setColour (juce::Colour (0xffffe7c2).withAlpha (juce::jlimit (0.0f, 1.0f, a)));
        g.fillEllipse (d.x * w, d.y * h, d.r, d.r);
    }
    if (! proc.crowdSounding())
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRect (getLocalBounds());
    }
}

// ============================================================================================
// CrowdStage
// ============================================================================================
juce::Rectangle<float> CrowdStage::orbRect (int s) const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float r = juce::jmin (w * 0.136f, h * 0.17f);
    const float cy = juce::jmax (r + 104.0f, h * 0.43f);
    const float cx = w * 0.5f + (s == 0 ? -1.0f : 1.0f) * (r + 30);
    return { cx - r, cy - r, r * 2, r * 2 };
}

juce::Rectangle<float> CrowdStage::stopRect (int s) const
{
    auto o = orbRect (s);
    return juce::Rectangle<float> (36, 36).withCentre ({ o.getCentreX(), o.getCentreY() + o.getHeight() * 0.27f });
}

juce::Rectangle<float> CrowdStage::linkRect() const
{
    auto o = orbRect (0);
    return juce::Rectangle<float> (78, 32).withCentre ({ getWidth() * 0.5f, o.getCentreY() });
}

void CrowdStage::refreshTiles()
{
    struct C { juce::String lib, name, sub, key; int index; bool fav; juce::int64 played; };
    std::vector<C> all;
    auto libs = proc.libraries().snapshot();
    for (auto& l : libs)
    {
        if (l.type != LibType::Crowd) continue;
        int flat = 0;
        for (auto& c : l.categories)
            for (auto& s : c.sounds)
            {
                const auto key = CrowdPrefs::keyFor (l.id, s.path);
                all.push_back ({ l.id, s.name, c.name.isEmpty() ? l.name : c.name, key, flat++, proc.crowdPrefs().isFavourite (key), proc.crowdPrefs().lastPlayed (key) });
            }
    }
    // favoris d'abord, puis les derniers joués, puis le reste
    std::stable_sort (all.begin(), all.end(), [] (const C& a, const C& b)
    {
        if (a.fav != b.fav) return a.fav;
        if (a.played != b.played) return a.played > b.played;
        return false;
    });
    tiles.clear();
    for (size_t i = 0; i < all.size() && i < 6; ++i) tiles.push_back ({ all[i].lib, all[i].name, all[i].sub, all[i].index, all[i].fav, {} });
    repaint();
}

bool CrowdStage::hitTest (int x, int y)
{
    const juce::Point<float> p ((float) x, (float) y);
    if (linkRect().contains (p)) return true;
    for (int s = 0; s < 2; ++s) if (orbRect (s).expanded (14).contains (p)) return true;
    for (auto& t : tiles) if (t.r.contains (p)) return true;
    return false;
}

void CrowdStage::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (linkRect().contains (p))
    {
        auto* prm = proc.params.getParameter ("crowdlink");
        prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
        repaint();
        return;
    }
    for (int s = 0; s < 2; ++s)
    {
        if (proc.crowdSounding (s) && stopRect (s).expanded (4).contains (p)) { proc.stopCrowd (s); repaint(); return; }
        if (orbRect (s).expanded (14).contains (p)) { proc.setCrowdTarget (s); repaint(); return; }
    }
    for (auto& t : tiles)
        if (t.r.contains (p))
        {
            int slot = proc.crowdTarget();
            for (int s = 0; s < 2; ++s)
                if (proc.crowdSounding (s) && proc.crowdLibraryId (s) == t.libId && proc.crowdSoundIndex (s) == t.index) slot = s;
            proc.playCrowd (t.libId, t.index, slot);
            repaint();
            return;
        }
}

void CrowdStage::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const juce::Colour warm (0xfff1e2cc);
    // ligne d'information
    {
        const int m = proc.currentMode();
        juce::String info = proc.padActive() ? L ("Pad en ", "Pad in ", "Pad en ") + keyText (proc.currentKey()) + (m == 1 ? "m" : "") : L ("Pad arr\xc3\xaat\xc3\xa9", "Pad stopped", "Pad detenido");
        info << U (" \xc2\xb7 ") << (proc.params.getRawParameterValue ("crowdfollow")->load() > 0.5f ? L ("les crowds suivent la tonalit\xc3\xa9", "crowds follow the key", "los crowds siguen la tonalidad")
                                                                                                    : L ("les crowds gardent leur tonalit\xc3\xa9", "crowds keep their key", "los crowds mantienen su tonalidad"));
        g.setColour (warm);
        g.setFont (theme::ui (12.5f, 1));
        g.drawText (info, juce::Rectangle<float> (0, orbRect (0).getY() - 64, w, 20), juce::Justification::centred);
    }
    const bool linked = proc.params.getRawParameterValue ("crowdlink")->load() > 0.5f;
    for (int s = 0; s < 2; ++s)
    {
        auto o = orbRect (s);
        const auto c = o.getCentre();
        const float r = o.getWidth() * 0.5f;
        const auto col = crowdCol[s];
        const bool on = proc.crowdSounding (s);
        const float lvl = juce::jlimit (0.0f, 1.0f, proc.crowdLevel (s) * 3.0f);
        if (on) softGlow (g, c, r * 0.9f, r * 1.55f, col.withAlpha (0.12f + 0.35f * lvl));
        juce::ColourGradient og (col.withAlpha (on ? 0.42f : 0.18f), c.x - r * 0.2f, c.y - r * 0.45f, juce::Colour (0xd81a0d0a), c.x - r * 0.2f + r * 1.1f, c.y - r * 0.45f, true);
        g.setGradientFill (og);
        g.fillEllipse (o);
        g.setColour (col.withAlpha (on ? 0.5f : 0.25f));
        g.drawEllipse (o.reduced (0.5f), 1.2f);
        // anneau de lecture
        const float rr = r + 12;
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 5.0f);
        if (on)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, rr, rr, 0, 0, juce::MathConstants<float>::twoPi * juce::jlimit (0.001f, 1.0f, proc.crowdPosition (s)), true);
            g.setColour (col);
            g.strokePath (arc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // crowd qui recevra le prochain son
        if (proc.crowdTarget() == s)
        {
            auto f = theme::ui (10.0f, 2); f.setExtraKerningFactor (0.14f);
            g.setFont (f);
            g.setColour (col);
            const auto txt = L ("PROCHAIN SON ICI", "NEXT SOUND HERE", "PR\xc3\x93XIMO SONIDO AQU\xc3\x8d");
            const float tw2 = juce::GlyphArrangement::getStringWidth (f, txt);
            auto tr2 = juce::Rectangle<float> (c.x - tw2 / 2 + 7, o.getY() - 36, tw2 + 4, 16);
            g.drawText (txt, tr2, juce::Justification::centredLeft);
            juce::Path tri;
            tri.addTriangle (tr2.getX() - 14, tr2.getCentreY() - 3, tr2.getX() - 6, tr2.getCentreY() - 3, tr2.getX() - 10, tr2.getCentreY() + 3);
            g.fillPath (tri);
        }
        auto f = theme::ui (10.5f, 2); f.setExtraKerningFactor (0.22f);
        g.setFont (f);
        g.setColour (col);
        g.drawText ("CROWD " + juce::String (s + 1), juce::Rectangle<float> (c.x - r, c.y - r * 0.56f, r * 2, 16), juce::Justification::centred);
        g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.55f));
        g.setFont (theme::ui (juce::jmin (21.0f, r * 0.23f), 2));
        g.drawFittedText (on ? proc.crowdSoundName (s) : L ("Aucun son", "No sound", "Sin sonido"), juce::Rectangle<float> (c.x - r * 0.82f, c.y - r * 0.36f, r * 1.64f, r * 0.3f).toNearestInt(), juce::Justification::centred, 1, 0.75f);
        juce::String sub;
        if (on)
        {
            if (proc.crowdSemitones (s) != 0) sub << L ("jou\xc3\xa9 en ", "played in ", "en ") << keyText (proc.currentKey()) << (proc.currentMode() == 1 ? "m" : "") << U (" \xc2\xb7 ");
            sub << (proc.loopOn() ? juce::String ("LOOP") : L ("1 COUP", "ONE SHOT", "1 GOLPE"));
        }
        else sub = L ("touchez un son", "tap a sound", "toca un sonido");
        g.setColour (warm.withAlpha (0.85f));
        g.setFont (theme::ui (11.5f));
        g.drawText (sub, juce::Rectangle<float> (c.x - r, c.y + r * 0.0f, r * 2, 18), juce::Justification::centred, true);
        if (on)
        {
            auto sr = stopRect (s);
            glassShape (g, sr, sr.getHeight() * 0.5f, false);
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (sr.withSizeKeepingCentre (11, 11), 2.0f);
        }
    }
    // pastille LIÉ
    {
        auto lr = linkRect();
        if (linked)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8ff5c4), lr.getX(), lr.getY(), juce::Colour (0xff3fd68e), lr.getX(), lr.getBottom(), false));
            g.fillRoundedRectangle (lr, lr.getHeight() * 0.5f);
        }
        else glassShape (g, lr, lr.getHeight() * 0.5f, false);
        auto f = theme::ui (11.0f, 2); f.setExtraKerningFactor (0.1f);
        g.setFont (f);
        g.setColour (linked ? juce::Colour (0xff06210f) : juce::Colours::white);
        g.drawText (linked ? L ("LI\xc3\x89 AU PAD", "LINKED", "UNIDO") : L ("LIBRE", "FREE", "LIBRE"), lr, juce::Justification::centred);
    }
    // sons à toucher
    const float tw = (w - 2 * 40 - 2 * 12) / 3, th = 48;
    const float y0 = orbRect (0).getBottom() + 34;
    for (int i = 0; i < (int) tiles.size(); ++i)
    {
        auto& t = tiles[(size_t) i];
        t.r = { 40 + (i % 3) * (tw + 12), y0 + (i / 3) * (th + 10), tw, th };
        int inSlot = -1;
        for (int s = 0; s < 2; ++s)
            if (proc.crowdSounding (s) && proc.crowdLibraryId (s) == t.libId && proc.crowdSoundIndex (s) == t.index) inSlot = s;
        if (inSlot >= 0)
        {
            g.setColour (crowdCol[inSlot].withAlpha (0.24f)); g.fillRoundedRectangle (t.r, th * 0.5f);
            g.setColour (crowdCol[inSlot].withAlpha (0.75f)); g.drawRoundedRectangle (t.r.reduced (0.5f), th * 0.5f, 1.2f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.3f)); g.fillRoundedRectangle (t.r, th * 0.5f);
            g.setColour (juce::Colours::white.withAlpha (0.16f)); g.drawRoundedRectangle (t.r.reduced (0.5f), th * 0.5f, 1.0f);
        }
        auto dot = juce::Rectangle<float> (t.r.getX() + 16, t.r.getCentreY() - 4, 8, 8);
        g.setColour (inSlot >= 0 ? crowdCol[inSlot] : juce::Colours::white.withAlpha (0.25f));
        g.fillEllipse (dot);
        auto tr = t.r.withTrimmedLeft (32).withTrimmedRight (t.fav ? 30 : 12);
        g.setColour (juce::Colours::white);
        g.setFont (theme::ui (13.0f, 1));
        g.drawText (t.name, tr.withTrimmedBottom (th * 0.45f), juce::Justification::bottomLeft, true);
        g.setColour (warm.withAlpha (0.7f));
        g.setFont (theme::ui (11.0f));
        g.drawText (t.sub, tr.withTrimmedTop (th * 0.56f), juce::Justification::topLeft, true);
        if (t.fav)
        {
            drawStar (g, { t.r.getRight() - 22, t.r.getCentreY() }, 7.5f, true, juce::Colour (0xffffd36b));
        }
    }
    if (tiles.empty())
    {
        g.setColour (warm.withAlpha (0.75f));
        g.setFont (theme::ui (13.0f));
        g.drawText (L ("Ajoutez un dossier de crowds dans l'onglet CROWDS", "Add a crowds folder in the CROWDS tab", "A\xc3\xb1""ade una carpeta de crowds en la pesta\xc3\xb1""a CROWDS"),
                    juce::Rectangle<float> (0, y0, w, 30), juce::Justification::centred);
    }
}
}
