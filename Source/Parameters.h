#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace onirique
{
namespace ids
{
    inline constexpr const char* length  = "length";     // 1 BEAT .. 4 BARS
    inline constexpr const char* space   = "space";      // ROOM, PLATE, DARK PLATE, HALL, SHIMMER
    inline constexpr const char* source  = "source";     // début de phrase envoyé dans la reverb (temps)
    inline constexpr const char* size    = "size";       // longueur de la queue (%)
    inline constexpr const char* curve   = "curve";      // forme de la montée (%)
    inline constexpr const char* gain    = "gain";       // niveau du fichier (dB, après normalisation à -1 dBFS)
    inline constexpr const char* sClean  = "sClean";     // sifflantes retirées
    inline constexpr const char* phone   = "phone";
    inline constexpr const char* chorus  = "chorus";
    inline constexpr const char* tremolo = "tremolo";
    inline constexpr const char* sweep   = "sweep";
    inline constexpr const char* wide    = "wide";
    inline constexpr const char* inSync  = "inSync";     // la montée joue d'elle-même pendant la lecture du DAW
    inline constexpr const char* phrase  = "phrase";     // phrase de la prise (session : ni presets, ni automation)
    inline constexpr const char* bypass  = "bypass";
}

inline const juce::StringArray& spaceNames()
{
    static const juce::StringArray n { "ROOM", "PLATE", "DARK PLATE", "HALL", "SHIMMER" };
    return n;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::length, 1 }, "Length",
                                                         StringArray { "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::space, 1 }, "Space",
                                                         StringArray { "Room", "Plate", "Dark Plate", "Hall", "Shimmer" }, 1));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::source, 1 }, "Source",
        NormalisableRange<float> (0.25f, 4.0f, 0.25f), 1.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return String (v, v == std::floor (v) ? 0 : 2) + (v == 1.0f ? " beat" : " beats"); })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::size, 1 }, "Size",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 60.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::curve, 1 }, "Curve",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::gain, 1 }, "Gain",
        NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { v = std::round (v * 10.0f) / 10.0f + 0.0f; return String (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::sClean, 1 }, "S-Clean", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::phone, 1 }, "Phone", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::chorus, 1 }, "Chorus", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::tremolo, 1 }, "Tremolo", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::sweep, 1 }, "Sweep", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::wide, 1 }, "Wide", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::inSync, 1 }, "In Sync", false));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::phrase, 1 }, "Phrase", 0, 63, 0,
                                                      AudioParameterIntAttributes().withAutomatable (false)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    return { p.begin(), p.end() };
}
} // namespace onirique
