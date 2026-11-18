// Spacepad by JB Praise — petits outils de dessin partagés par les fichiers de l'interface.
#pragma once
#include "UI.h"

namespace spacepad
{
inline juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

// texte avec une lueur (à préparer une fois dans une image, jamais à chaque image : le flou coûte cher)
inline void glowText (juce::Graphics& g, const juce::String& t, juce::Font f, juce::Rectangle<float> r, juce::Justification j, juce::Colour c, juce::Colour glow, int radius, float kerning = 0)
{
    if (kerning != 0) f.setExtraKerningFactor (kerning);
    juce::GlyphArrangement ga;
    ga.addFittedText (f, t, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1);
    juce::Path p;
    ga.createPath (p);
    juce::DropShadow (glow, radius, {}).drawForPath (g, p);
    g.setColour (c);
    g.fillPath (p);
}

// lueur douce : dégradé radial (aussi beau qu'un flou, mais presque gratuit à dessiner)
inline void softGlow (juce::Graphics& g, juce::Point<float> c, float r0, float r1, juce::Colour col)
{
    if (r1 <= r0 || col.getAlpha() == 0) return;
    juce::ColourGradient gr (col, c.x, c.y, col.withAlpha (0.0f), c.x + r1, c.y, true);
    gr.addColour (juce::jlimit (0.0, 0.99, (double) (r0 / r1)), col);
    gr.addColour (juce::jlimit (0.0, 0.995, (double) ((r0 + (r1 - r0) * 0.45f) / r1)), col.withMultipliedAlpha (0.35f));
    g.setGradientFill (gr);
    g.fillEllipse (c.x - r1, c.y - r1, r1 * 2, r1 * 2);
}

inline void insetBox (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c)
{
    g.setColour (c);
    g.fillRoundedRectangle (r, radius);
    juce::ColourGradient sh (juce::Colours::black.withAlpha (0.55f), r.getX(), r.getY(), juce::Colours::transparentBlack, r.getX(), r.getY() + 10, false);
    g.setGradientFill (sh);
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (r.translated (0, 1), radius, 1.0f);
}

// verre dépoli façon iPhone : pastille ou rond, sans cadre sombre
inline void glassShape (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool over, juce::Colour ink = juce::Colours::white)
{
    const bool lightInk = ink.getBrightness() > 0.5f;
    g.setColour (juce::Colours::black.withAlpha (lightInk ? 0.22f : 0.06f));
    g.fillRoundedRectangle (r.translated (0, 3).expanded (1), radius + 1);
    g.setColour (lightInk ? ink.withAlpha (over ? 0.15f : 0.10f) : juce::Colours::white.withAlpha (over ? 0.95f : 0.85f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (ink.withAlpha (lightInk ? 0.15f : 0.12f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    if (lightInk) { g.setColour (ink.withAlpha (0.10f)); g.drawLine (r.getX() + radius, r.getY() + 1.5f, r.getRight() - radius, r.getY() + 1.5f, 1.0f); }
}

// étoile des favoris (dessinée : la police n'a pas toujours ce signe)
inline void drawStar (juce::Graphics& g, juce::Point<float> c, float r, bool filled, juce::Colour col)
{
    juce::Path p;
    p.addStar (c, 5, r * 0.45f, r, 0.0f);
    g.setColour (col);
    if (filled) g.fillPath (p);
    else g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

inline juce::Image makeCacheImage (int w, int h) { return juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true); }
}
