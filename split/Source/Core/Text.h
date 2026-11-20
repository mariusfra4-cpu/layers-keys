// Layers Split — les textes du code source sont en UTF-8 (accents français).
#pragma once

#include <juce_core/juce_core.h>

namespace lsplit
{
inline juce::String U (const char* utf8) { return juce::String::fromUTF8 (utf8); }
}
