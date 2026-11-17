// Spacepad by JB Praise — l'espace animé derrière l'orbe, l'anneau des tonalités, l'orbe qui tourne,
// le crochet majeur/mineur et le visualiseur audio.
// Règle d'or : rien de coûteux à chaque image. Les décors sont préparés une fois dans des images, puis simplement
// tournés et déplacés ; les lueurs sont des dégradés, jamais des flous.
#include "UIHelpers.h"
#include "BinaryData.h"

namespace spacepad
{
// ============================================================================================
// SpaceView
// ============================================================================================
namespace
{
constexpr int planeSize = 512;
// ellipse de la galaxie dans la photo (centre, inclinaison, aplatissement, rayon utile)
constexpr float photoCx = 500, photoCy = 255, photoTilt = 24.0f * juce::MathConstants<float>::pi / 180.0f, photoRatio = 0.60f, photoR = 370;

float hash2 (int x, int y)
{
    juce::uint32 h = (juce::uint32) x * 374761393u + (juce::uint32) y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((h ^ (h >> 16)) & 0xffffff) / (float) 0xffffff;
}
float valueNoise (float x, float y)
{
    const int xi = (int) std::floor (x), yi = (int) std::floor (y);
    const float fx = x - xi, fy = y - yi;
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float a = hash2 (xi, yi), b = hash2 (xi + 1, yi), c = hash2 (xi, yi + 1), d = hash2 (xi + 1, yi + 1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}
float fbm (float x, float y)
{
    return 0.55f * valueNoise (x, y) + 0.3f * valueNoise (x * 2.1f, y * 2.1f) + 0.15f * valueNoise (x * 4.3f, y * 4.3f);
}
}

void SpaceView::buildPlane (int mode)
{
    planeMode = mode;
    plane = {};
    if (mode == 2) return;
    plane = juce::Image (juce::Image::ARGB, planeSize, planeSize, true);
    const float half = planeSize * 0.5f;
    if (mode == 0)
    {
        // la photo : la galaxie est « redressée » (vue de face) pour pouvoir tourner sur elle-même
        auto photo = juce::ImageCache::getFromMemory (BinaryData::galaxie_jpg, BinaryData::galaxie_jpgSize);
        juce::Graphics pg (plane);
        pg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const float k = half / photoR;
        pg.drawImageTransformed (photo, juce::AffineTransform::translation (-photoCx, -photoCy).rotated (-photoTilt).scaled (1.0f, 1.0f / photoRatio).scaled (k).translated (half, half));
    }
    juce::Image::BitmapData bd (plane, juce::Image::BitmapData::readWrite);
    const juce::Colour blue (0xff5c8cff), violet (0xffa78bfa);
    for (int y = 0; y < planeSize; ++y)
        for (int x = 0; x < planeSize; ++x)
        {
            const float u = (x - half) / half, v = (y - half) / half;
            const float r = std::sqrt (u * u + v * v);
            const float edge = juce::jlimit (0.0f, 1.0f, (1.0f - r) / 0.42f);     // bord adouci
            if (edge <= 0) { bd.setPixelColour (x, y, juce::Colours::transparentBlack); continue; }
            const float fade = edge * edge * (3 - 2 * edge);
            if (mode == 0)
            {
                // couleurs ramenées vers le bleu-violet de Spacepad, lumière contenue
                auto c = bd.getPixelColour (x, y);
                const float lum = 0.3f * c.getFloatRed() + 0.59f * c.getFloatGreen() + 0.11f * c.getFloatBlue();
                auto tint = blue.interpolatedWith (violet, juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * std::sin (std::atan2 (v, u) * 2 + r * 6)));
                auto t2 = juce::Colour::fromFloatRGBA (juce::jmin (1.0f, tint.getFloatRed() * lum * 1.35f), juce::jmin (1.0f, tint.getFloatGreen() * lum * 1.35f), juce::jmin (1.0f, tint.getFloatBlue() * lum * 1.35f), 1.0f);
                auto out = c.interpolatedWith (t2, 0.6f);
                bd.setPixelColour (x, y, out.withAlpha (fade * juce::jlimit (0.0f, 1.0f, lum * 2.2f)));
            }
            else
            {
                // tourbillon : deux bras en spirale, nébuleuse, poussière sombre
                const float th = std::atan2 (v, u);
                const float spiral = std::log (juce::jmax (r, 0.02f) / 0.08f) / 0.62f;
                const float arm = std::pow (0.5f + 0.5f * std::cos (2.0f * (th - spiral)), 3.0f);
                const float dust = std::pow (0.5f + 0.5f * std::cos (2.0f * (th - spiral) - 0.7f), 8.0f);
                const float n = fbm (u * 5.0f + 9.0f, v * 5.0f + 3.0f);
                float d = arm * (0.5f + 0.5f * n) * (1.0f - 0.6f * r) * 1.9f + std::exp (-r * r / 0.03f) * 0.6f + 0.06f * n;
                d *= 1.0f - 0.55f * dust * (0.6f + 0.4f * n);
                d = juce::jlimit (0.0f, 1.0f, d);
                auto col = blue.interpolatedWith (violet, n).interpolatedWith (juce::Colour (0xffe6efff), juce::jlimit (0.0f, 1.0f, std::exp (-r * 6.0f)));
                bd.setPixelColour (x, y, col.withAlpha (d * fade));
            }
        }
    // étoiles en orbite dans le plan de la galaxie (plus vite près du centre)
    orbit.clear();
    const int n = mode == 0 ? 650 : 1300;
    for (int i = 0; i < n; ++i)
    {
        Star s;
        s.r = 1.05f + std::pow (rng.nextFloat(), 0.8f) * 2.6f;   // en rayons d'orbe
        s.th = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        s.b = 0.2f + 0.8f * std::pow (rng.nextFloat(), 2.0f);
        s.s = 0.8f + rng.nextFloat() * 1.3f;
        orbit.push_back (s);
    }
}

void SpaceView::tick (float dt)
{
    const auto st = uiSettings();
    if (st.background != planeMode) { buildPlane (st.background); needsRender = true; }
    if (! st.animations) return;
    // la galaxie tourne lentement : la recalculer une image sur deux suffit (même rendu à l'œil, deux fois moins de calcul)
    if (++frames % 2 == 0) needsRender = true;
    const float spin = (0.25f + 1.0f * anim.env + 2.2f * anim.surge) * (0.3f + 1.4f * st.speed);
    for (auto& s : orbit)
    {
        s.th -= dt * 0.5f * spin * std::pow (1.0f / s.r, 0.8f);
        if (planeMode == 1)
        {
            s.r -= dt * 0.025f * spin / s.r;                       // le tourbillon aspire doucement vers l'orbe
            if (s.r < 1.03f) { s.r = 2.8f + rng.nextFloat() * 0.85f; s.th = rng.nextFloat() * juce::MathConstants<float>::twoPi; }
        }
    }
}

void SpaceView::paint (juce::Graphics& g)
{
    const auto st = uiSettings();
    if (planeMode != st.background) buildPlane (st.background);
    auto b = getLocalBounds().toFloat();
    g.setColour (theme::bg.darker (0.35f));
    g.fillRect (b);

    // étoiles lointaines, immobiles (préparées une fois)
    const float px = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! farStars.isValid() || farStars.getWidth() != juce::roundToInt (b.getWidth() * px))
    {
        farStars = makeCacheImage (juce::roundToInt (b.getWidth() * px), juce::roundToInt (b.getHeight() * px));
        juce::Graphics fg (farStars);
        fg.addTransform (juce::AffineTransform::scale (px));
        juce::Random r (5);
        for (int i = 0; i < 420; ++i)
        {
            const float s = 0.6f + r.nextFloat() * 1.2f;
            fg.setColour (juce::Colour (0xffe1ebff).withAlpha (0.08f + 0.45f * std::pow (r.nextFloat(), 2.0f)));
            fg.fillEllipse (r.nextFloat() * b.getWidth(), r.nextFloat() * b.getHeight(), s, s);
        }
        // fondu doux vers les bords de la zone
        const auto c = b.getCentre();
        juce::ColourGradient vg (theme::bg.withAlpha (0.0f), c.x, c.y, theme::bg.withAlpha (0.85f), c.x + b.getWidth() * 0.62f, c.y, true);
        vg.addColour (0.6, theme::bg.withAlpha (0.0f));
        fg.setGradientFill (vg);
        fg.fillRect (b);
    }
    g.setOpacity (1.0f);
    g.drawImage (farStars, b);
    if (planeMode == 2 || ! plane.isValid()) return;
    if (needsRender || ! layer.isValid() || layer.getWidth() != juce::roundToInt (b.getWidth() * px)) renderLayer (px);
    g.setOpacity (1.0f);
    g.drawImage (layer, b);
}

void SpaceView::renderLayer (float px)
{
    needsRender = false;
    const auto st = uiSettings();
    auto b = getLocalBounds().toFloat();
    if (! layer.isValid() || layer.getWidth() != juce::roundToInt (b.getWidth() * px))
        layer = makeCacheImage (juce::roundToInt (b.getWidth() * px), juce::roundToInt (b.getHeight() * px));
    else
        layer.clear (layer.getBounds());
    juce::Graphics g (layer);
    g.addTransform (juce::AffineTransform::scale (px));

    const float bright = 0.5f + st.brightness;                     // réglage « luminosité de l'animation »
    const float tilt = planeMode == 0 ? photoTilt : 0.0f, ratio = planeMode == 0 ? photoRatio : 0.92f;
    const float radius = coreRadius * (planeMode == 0 ? 3.55f : 3.2f);
    const float sc = radius / (planeSize * 0.5f);
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.setOpacity (juce::jlimit (0.0f, 1.0f, (0.30f + 0.28f * anim.env) * bright * (planeMode == 1 ? 1.35f : 1.0f)));
    g.drawImageTransformed (plane, juce::AffineTransform::translation (-planeSize * 0.5f, -planeSize * 0.5f)
                                       .rotated (anim.phi).scaled (sc, sc * ratio).rotated (tilt).translated (centre));
    g.setOpacity (1.0f);

    const float ct = std::cos (tilt), sn = std::sin (tilt);
    const float sb = (0.22f + 0.40f * anim.env) * bright;
    for (auto& s : orbit)
    {
        const float rr = s.r * coreRadius;
        const float u = std::cos (s.th) * rr, v = std::sin (s.th) * rr * ratio;
        const float x = centre.x + u * ct - v * sn, y = centre.y + u * sn + v * ct;
        const float fadeIn = planeMode == 1 ? juce::jlimit (0.0f, 1.0f, (s.r - 1.03f) / 0.5f) : 1.0f;
        g.setColour (juce::Colour (0xffdceaff).withAlpha (juce::jlimit (0.0f, 1.0f, s.b * sb * fadeIn)));
        g.fillRect (x, y, s.s, s.s);
    }
}

// ============================================================================================
// KeyRing
// ============================================================================================
int KeyRing::keyAt (juce::Point<float> p) const
{
    auto b = getLocalBounds().toFloat();
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    if (p.getDistanceFrom (c) < D * 0.234f) return -1;
    const float R = D * 0.425f, btn = D * 0.068f;
    for (int i = 0; i < 12; ++i)
    {
        const float a = (i * 30.0f - 90.0f) * juce::MathConstants<float>::pi / 180.0f;
        if (p.getDistanceFrom ({ c.x + R * std::cos (a), c.y + R * std::sin (a) }) < btn * 1.25f) return i;
    }
    return -2;
}

void KeyRing::mouseDown (const juce::MouseEvent& e)
{
    const int h = keyAt (e.position);
    if (h >= 0) proc.pressKey (h);
    else if (h == -1) { if (proc.padActive()) proc.stopPad(); else proc.playPad(); }
    repaint();
}

void KeyRing::mouseMove (const juce::MouseEvent& e)
{
    const int h = keyAt (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= -1 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

// une touche : en mineur, chaque note porte un petit « m »
void KeyRing::paintKey (juce::Graphics& g, int i, bool sel, bool active, bool hov, int mode, float D, juce::Point<float> c) const
{
    const float s = D / 470.0f, R = D * 0.425f, btn = D * 0.0617f;
    const float a = (i * 30.0f - 90.0f) * juce::MathConstants<float>::pi / 180.0f;
    auto kc = juce::Point<float> (c.x + R * std::cos (a), c.y + R * std::sin (a));
    auto kr = juce::Rectangle<float> (btn * 2, btn * 2).withCentre (kc);
    juce::Colour txt;
    if (sel && active)
    {
        softGlow (g, kc, btn * 0.9f, btn * 2.0f, theme::accent.withAlpha (0.5f));
        juce::ColourGradient kg (juce::Colours::white, kc.x - btn * 0.3f, kc.y - btn * 0.5f, theme::accent.darker (0.9f), kc.x + btn, kc.y + btn, true);
        kg.addColour (0.5, theme::accent);
        g.setGradientFill (kg);
        g.fillEllipse (kr);
        g.setColour (juce::Colours::white);
        g.drawEllipse (kr.reduced (1), 2.0f);
        txt = juce::Colour (0xff04121c);
    }
    else
    {
        softGlow (g, kc.translated (0, 6 * s), btn * 0.8f, btn * 1.45f, juce::Colours::black.withAlpha (0.5f));
        juce::ColourGradient kg (juce::Colour (hov ? 0xf04a5878 : 0xf03a4560), kc.x - btn * 0.3f, kc.y - btn * 0.5f, juce::Colour (0xf0151b2b), kc.x + btn, kc.y + btn, true);
        g.setGradientFill (kg);
        g.fillEllipse (kr);
        g.setColour (sel ? theme::accent : juce::Colours::white.withAlpha (0.16f));
        g.drawEllipse (kr.reduced (0.5f), sel ? 2.0f : 1.0f);
        txt = sel ? theme::accentLight : juce::Colour (0xffdfe6f5);
    }
    const auto name = keyText (i);
    auto big = theme::ui ((name.length() > 3 ? 14.0f : 17.0f) * s, 2);
    auto small = theme::ui (11.0f * s, 2);
    g.setColour (txt);
    if (mode == 1)
    {
        const float w1 = juce::GlyphArrangement::getStringWidth (big, name), w2 = juce::GlyphArrangement::getStringWidth (small, "m");
        const float x0 = kc.x - (w1 + w2 + 1) / 2;
        g.setFont (big);
        g.drawText (name, juce::Rectangle<float> (x0, kr.getY(), w1 + 2, kr.getHeight()), juce::Justification::centredLeft, false);
        g.setFont (small);
        g.drawText ("m", juce::Rectangle<float> (x0 + w1 + 1, kr.getY() + 3 * s, w2 + 2, kr.getHeight()), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setFont (big);
        g.drawText (name, kr, juce::Justification::centred);
    }
}

void KeyRing::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float s = D / 470.0f;
    const bool active = proc.padActive();
    const int key = proc.currentKey(), mode = proc.currentMode();
    const auto st = uiSettings();

    // 1. décor fixe : anneau en verre (on voit l'espace à travers) et les 12 touches au repos
    const float px = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int want = getWidth() * 7919 + getHeight() * 31 + mode * 7 + settingsVersion() * 1009;
    if (! base.isValid() || baseKey != want || std::abs (baseScale - px) > 0.01f)
    {
        baseKey = want;
        baseScale = px;
        base = makeCacheImage (juce::roundToInt (b.getWidth() * px), juce::roundToInt (b.getHeight() * px));
        juce::Graphics bg (base);
        bg.addTransform (juce::AffineTransform::scale (px));
        auto disc = juce::Rectangle<float> (D, D).withCentre (c);
        bg.setColour (juce::Colours::black.withAlpha (0.18f));
        bg.fillEllipse (disc);
        bg.setColour (juce::Colours::white.withAlpha (0.10f));
        bg.drawEllipse (disc.reduced (1), 1.0f);
        juce::Path circ; circ.addEllipse (disc.reduced (34 * s));
        juce::Path dash; const float dl[] = { 4.0f * s, 5.0f * s };
        juce::PathStrokeType (1.0f).createDashedStroke (dash, circ, dl, 2);
        bg.setColour (theme::accent.withAlpha (0.20f));
        bg.fillPath (dash);
        for (int i = 0; i < 12; ++i) paintKey (bg, i, false, false, false, mode, D, c);
    }
    g.setOpacity (1.0f);
    g.drawImage (base, b);

    // 2. ce qui bouge : touche survolée, touche jouée
    if (hover >= 0 && hover != key) paintKey (g, hover, false, false, true, mode, D, c);
    paintKey (g, key, true, active, hover == key, mode, D, c);

    // 3. l'orbe : respire avec le son, lumière qui tourne à l'intérieur
    const float coreR = D * 0.234f * (1.0f + 0.022f * anim.env * std::sin (anim.t * 1.1f));
    auto core = juce::Rectangle<float> (coreR * 2, coreR * 2).withCentre (c);
    const float p = 0.35f + 0.65f * anim.env;
    softGlow (g, c, coreR * 0.95f, coreR * 1.6f, theme::violet.withAlpha (0.16f * p));
    softGlow (g, c, coreR * 0.95f, coreR * 1.32f, theme::accent.withAlpha (0.34f * p));
    juce::ColourGradient cg (juce::Colour (0xffd8f6ff).withMultipliedBrightness (0.4f + 0.6f * p), c.x - coreR * 0.2f, c.y - coreR * 0.36f, juce::Colour (0xff070b16), c.x + coreR, c.y + coreR, true);
    cg.addColour (0.15, theme::accent.withMultipliedBrightness (0.45f + 0.5f * p));
    cg.addColour (0.42, theme::accent.darker (1.2f).withMultipliedBrightness (0.6f + 0.4f * p));
    cg.addColour (0.72, juce::Colour (0xff0e1b38));
    g.setGradientFill (cg);
    g.fillEllipse (core);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addEllipse (core);
        g.reduceClipRegion (clip);
        for (int k = 0; k < 2; ++k)
        {
            const float a = anim.orb + k * juce::MathConstants<float>::pi;
            softGlow (g, { c.x + std::cos (a) * coreR * 0.45f, c.y + std::sin (a) * coreR * 0.45f }, 0, coreR * 0.62f, theme::violet.withAlpha (0.20f * p));
        }
        for (int k = 0; k < 3; ++k)
        {
            juce::Path arm;
            for (int i = 0; i <= 28; ++i)
            {
                const float t = i / 28.0f, rr = coreR * (0.12f + 0.86f * t), a = anim.orb + k * 2.094f + t * 2.4f;
                const juce::Point<float> q (c.x + std::cos (a) * rr, c.y + std::sin (a) * rr);
                if (i == 0) arm.startNewSubPath (q); else arm.lineTo (q);
            }
            g.setColour (juce::Colour (0xffc8ebff).withAlpha (0.09f * p));
            g.strokePath (arm, juce::PathStrokeType (2.2f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    g.setColour (theme::accentLight.withAlpha (0.18f + 0.15f * p));
    g.drawEllipse (core.reduced (0.5f), 1.2f);
    if (hover == -1) { g.setColour (juce::Colours::white.withAlpha (0.15f)); g.drawEllipse (core.reduced (2), 2.0f); }

    // nom de la tonalité avec sa lueur : préparé une fois par tonalité
    const auto label = keyText (key) + (mode == 1 ? "m" : "");
    auto labelArea = core.withSizeKeepingCentre (D * 0.46f, 70 * s).translated (0, -14 * s);
    const juce::String lk = label + juce::String (settingsVersion());
    if (lk != labelText || ! labelImg.isValid() || std::abs (labelScale - px) > 0.01f || labelW != juce::roundToInt (labelArea.getWidth()))
    {
        labelText = lk; labelScale = px; labelW = juce::roundToInt (labelArea.getWidth());
        labelImg = makeCacheImage (juce::roundToInt (labelArea.getWidth() * px), juce::roundToInt (labelArea.getHeight() * px));
        juce::Graphics lg (labelImg);
        lg.addTransform (juce::AffineTransform::scale (px));
        const float fs = (label.length() > 3 ? 44.0f : 58.0f) * s;
        glowText (lg, label, theme::title (fs), labelArea.withPosition (0, 0), juce::Justification::centred, juce::Colours::white, theme::accentLight.withAlpha (0.8f), (int) (22 * s * px));
    }
    g.setOpacity (1.0f);
    g.drawImage (labelImg, labelArea);

    auto f = theme::ui (12.0f * s, 1); f.setExtraKerningFactor (0.3f);
    g.setFont (f);
    g.setColour (juce::Colour (0xffd9f4ff));
    const auto noteWord = st.solfegeNames ? solfege (key) : noteName (key);
    g.drawText ((noteWord + " " + modeWord (mode)).toUpperCase(), core.withSizeKeepingCentre (core.getWidth(), 18 * s).translated (0, 30 * s), juce::Justification::centred);
    g.setFont (theme::ui (11.0f * s));
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawText (active ? L ("appuyer pour arr\xc3\xaater", "tap to stop", "toca para detener") : L ("appuyer pour jouer", "tap to play", "toca para tocar"), core.withSizeKeepingCentre (core.getWidth(), 16 * s).translated (0, 52 * s), juce::Justification::centred);
}

// ============================================================================================
// Le crochet majeur ⇄ mineur
// ============================================================================================
void HookButton::tick (float dt)
{
    const float target = proc.currentMode() == 1 ? 0.0f : juce::MathConstants<float>::pi;
    const float before = angle;
    angle += (target - angle) * (1.0f - std::exp (-dt / 0.12f));
    if (std::abs (angle - before) > 0.0005f) repaint();
}

void HookButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const auto c = r.getCentre();
    const float R = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 6;
    const bool minor = proc.currentMode() == 1;
    const auto col = minor ? theme::lilac : theme::gold;
    softGlow (g, c, R * 0.7f, R * 1.25f, col.withAlpha (over ? 0.45f : 0.3f));
    const float rr = down ? R - 1 : R;
    g.setColour (juce::Colour (0xe0141c30));
    g.fillEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2);
    g.setColour (juce::Colours::white.withAlpha (over ? 0.35f : 0.22f));
    g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 1.2f);

    // deux flèches courbes qui se suivent (aller-retour)
    juce::Path arrows;
    const float a = rr * 0.5f;
    for (int k = 0; k < 2; ++k)
    {
        const float a0 = k == 0 ? juce::MathConstants<float>::pi * 1.12f : juce::MathConstants<float>::pi * 0.12f;
        const float a1 = a0 + juce::MathConstants<float>::pi * 0.76f;
        juce::Path arc;
        arc.addCentredArc (0, 0, a, a, 0, a0 + juce::MathConstants<float>::halfPi, a1 + juce::MathConstants<float>::halfPi, true);
        juce::Path stroke;
        juce::PathStrokeType (rr * 0.13f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroke, arc);
        arrows.addPath (stroke);
        const juce::Point<float> e (std::cos (a1) * a, std::sin (a1) * a);
        const juce::Point<float> tangent (-std::sin (a1), std::cos (a1));
        const juce::Point<float> normal (std::cos (a1), std::sin (a1));
        juce::Path head;
        head.addTriangle (e + tangent * (rr * 0.26f), e + normal * (rr * 0.2f) - tangent * (rr * 0.04f), e - normal * (rr * 0.2f) - tangent * (rr * 0.04f));
        arrows.addPath (head);
    }
    arrows.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    g.setColour (col);
    g.fillPath (arrows);
}

// ============================================================================================
// Visualiseur
// ============================================================================================
Visualizer::Visualizer (SpacepadProcessor& p, AnimState& a) : proc (p), anim (a)
{
    setInterceptsMouseClicks (false, false);
}

void Visualizer::tick (float dt)
{
    const auto st = uiSettings();
    proc.copyScope (wave.data(), N);
    if (st.viz <= 1)
    {
        std::copy (wave.begin(), wave.end(), fftData.begin());
        std::fill (fftData.begin() + N, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        for (int i = 0; i < bands; ++i)
        {
            // bandes de fréquences de 60 Hz à 9 kHz, le grave au centre (forme symétrique)
            const int k = std::abs (i - bands / 2);
            const double f0 = 60.0 * std::pow (150.0, (double) k / (bands / 2 + 1)), f1 = f0 * 1.22;
            const int b0 = juce::jlimit (1, N / 2 - 1, (int) (f0 / sr * N)), b1 = juce::jlimit (b0 + 1, N / 2, (int) (f1 / sr * N));
            float m = 0;
            for (int b = b0; b < b1; ++b) m = juce::jmax (m, fftData[(size_t) b]);
            const float db = juce::Decibels::gainToDecibels (m / (float) N * 4.0f, -90.0f);
            float target = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 58.0f);
            if (! st.animations) target = 0.08f;
            target = juce::jmax (0.06f, target * (1.0f + anim.surge * 0.5f));
            level[(size_t) i] += (target - level[(size_t) i]) * juce::jmin (1.0f, dt * (target > level[(size_t) i] ? 18.0f : 6.0f));
            // des bulles montent des barres quand le son bouillonne
            if (st.viz == 0 && st.animations && level[(size_t) i] > 0.25f && rng.nextFloat() < dt * 3.0f * level[(size_t) i])
                bubbles.push_back ({ (i + 0.5f) / bands, 1.0f - level[(size_t) i], 1.2f + rng.nextFloat() * 2.2f, 0.4f + rng.nextFloat() * 0.6f, 1.0f });
        }
    }
    for (auto& b : bubbles) { b.y -= b.v * dt; b.a -= dt * 1.1f; }
    bubbles.erase (std::remove_if (bubbles.begin(), bubbles.end(), [] (const Bubble& b) { return b.a <= 0 || b.y < -0.4f; }), bubbles.end());
    if (bubbles.size() > 120) bubbles.erase (bubbles.begin(), bubbles.begin() + (long) (bubbles.size() - 120));
    repaint();
}

void Visualizer::paint (juce::Graphics& g)
{
    const auto st = uiSettings();
    if (st.viz == 3) return;
    auto r = getLocalBounds().toFloat();
    const float mid = r.getHeight() * 0.64f;
    if (st.viz == 2)
    {
        // onde : le signal réel, lissé
        juce::Path p;
        const int n = 512;
        float peak = 1e-4f;
        for (int i = N - n; i < N; ++i) peak = juce::jmax (peak, std::abs (wave[(size_t) i]));
        const float gain = juce::jmin (6.0f, 0.42f / peak) * r.getHeight() * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + r.getWidth() * i / (n - 1);
            const float w = std::sin (juce::MathConstants<float>::pi * i / (n - 1));    // effilé aux deux bouts
            const float y = r.getCentreY() - wave[(size_t) (N - n + i)] * gain * w;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (theme::accent.withAlpha (0.22f));
        g.strokePath (p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill (juce::ColourGradient (theme::accent, r.getX(), 0, theme::violet, r.getRight(), 0, false));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }
    const float bw = r.getWidth() / bands, ww = bw * 0.62f;
    for (int i = 0; i < bands; ++i)
    {
        const float l = level[(size_t) i];
        const float h = juce::jmax (4.0f, l * mid);
        const float x = r.getX() + i * bw + (bw - ww) * 0.5f;
        g.setGradientFill (juce::ColourGradient (theme::lilac.withAlpha (0.55f + 0.45f * l), x, mid - h, theme::accent.withAlpha (0.6f + 0.4f * l), x, mid, false));
        g.fillRoundedRectangle (x, mid - h, ww, h, ww * 0.5f);
        g.setColour (theme::accent.withAlpha (0.10f + 0.12f * l));                         // reflet
        g.fillRoundedRectangle (x, mid + 3, ww, h * 0.32f, ww * 0.5f);
    }
    for (auto& b : bubbles)
    {
        const float x = r.getX() + b.x * r.getWidth(), y = r.getY() + b.y * mid;
        g.setColour (juce::Colour (0xffdcf0ff).withAlpha (b.a * 0.7f));
        g.fillEllipse (x - b.r, y - b.r, b.r * 2, b.r * 2);
    }
}
}
