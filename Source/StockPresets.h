#pragma once

#include "Parameters.h"
#include "ui/OniriquePresets.h"

namespace onirique
{
/*  Presets d'usine de RISER, rangés par usage pour le navigateur de la barre (les tags). Valeurs en unités réelles ; un
    réglage non cité reprend sa valeur par défaut.
    length : 0 1 BEAT, 1 2 BEATS, 2 1 BAR, 3 2 BARS, 4 4 BARS ; space : 0 ROOM, 1 PLATE, 2 DARK PLATE, 3 HALL, 4 SHIMMER ;
    source en temps ; tremoloRate : 0 1/4, 1 1/8, 2 1/16, 3 1/32 ; sweepStart en Hz. */
namespace riser
{
enum Tags { shortRise = 1, longRise = 2, hook = 4, adlibs = 8, dark = 16, bright = 32, effects = 64 };

inline juce::StringArray tagNames() { return { "Short", "Long", "Hook", "Ad-libs", "Dark", "Bright", "Effects" }; }

/** Les réglages de session (la prise, sa vue, la lecture d'elle-même) : un preset les laisse. */
inline juce::StringArray notStored() { return { ids::bypass, ids::phrase, ids::inSync, ids::raw, ids::inputOpen }; }

inline std::vector<PresetBank::Stock> stockPresets()
{
    return {
        { "Init", 0, {} },
        { "Classic Swell", hook | bright, { { ids::length, 2 }, { ids::space, 1 }, { ids::size, 60 } } },
        { "Two-Beat Lift", shortRise | hook, { { ids::length, 1 }, { ids::space, 1 }, { ids::size, 50 }, { ids::curve, 15 } } },
        { "Short Pickup", shortRise | adlibs, { { ids::length, 0 }, { ids::space, 0 }, { ids::source, 0.5f }, { ids::size, 40 } } },
        { "Quick Breath", shortRise | adlibs, { { ids::length, 0 }, { ids::space, 1 }, { ids::source, 0.25f }, { ids::size, 35 },
                                                { ids::curve, 20 } } },
        { "Room Snap", shortRise | dark, { { ids::length, 0 }, { ids::space, 0 }, { ids::size, 30 }, { ids::curve, 50 } } },
        { "Hall Bloom", longRise | hook, { { ids::length, 3 }, { ids::space, 3 }, { ids::size, 80 }, { ids::curve, 10 }, { ids::wide, 1 } } },
        { "Ghost Hall", longRise | dark, { { ids::length, 3 }, { ids::space, 3 }, { ids::size, 85 }, { ids::sweep, 1 }, { ids::curve, 30 } } },
        { "Shimmer Rise", longRise | bright | hook, { { ids::length, 3 }, { ids::space, 4 }, { ids::size, 75 }, { ids::wide, 1 } } },
        { "Long Build", longRise, { { ids::length, 4 }, { ids::space, 3 }, { ids::size, 90 }, { ids::source, 2 }, { ids::curve, 60 },
                                    { ids::sweep, 1 } } },
        { "Four-Bar Epic", longRise | hook | bright, { { ids::length, 4 }, { ids::space, 4 }, { ids::size, 95 }, { ids::source, 2 },
                                                      { ids::curve, 50 }, { ids::wide, 1 } } },
        { "Dark Tunnel", dark, { { ids::length, 2 }, { ids::space, 2 }, { ids::size, 70 }, { ids::sweep, 1 } } },
        { "Dream Sweep", longRise | bright | effects, { { ids::length, 3 }, { ids::space, 4 }, { ids::size, 80 }, { ids::sweep, 1 },
                                                        { ids::sweepStart, 200 }, { ids::chorus, 1 }, { ids::chorusDepth, 50 },
                                                        { ids::wide, 1 } } },
        { "Wide Chorus", effects | bright, { { ids::length, 2 }, { ids::space, 3 }, { ids::chorus, 1 }, { ids::wide, 1 } } },
        { "Phone Call", effects | adlibs, { { ids::length, 2 }, { ids::space, 0 }, { ids::phone, 1 }, { ids::size, 45 } } },
        { "Telephone Fade-in", effects | dark, { { ids::length, 2 }, { ids::space, 0 }, { ids::phone, 1 }, { ids::phoneDrive, 70 },
                                                 { ids::sweep, 1 }, { ids::sweepStart, 600 } } },
        { "Stutter Rise", effects | hook, { { ids::length, 2 }, { ids::space, 1 }, { ids::tremolo, 1 }, { ids::curve, 40 } } },
        { "Gated Pulse", effects, { { ids::length, 2 }, { ids::space, 1 }, { ids::tremolo, 1 }, { ids::tremoloRate, 3 },
                                    { ids::tremoloMix, 90 }, { ids::curve, 30 } } },
        { "Drill Pickup", shortRise | effects, { { ids::length, 1 }, { ids::space, 1 }, { ids::size, 50 }, { ids::tremolo, 1 },
                                                 { ids::phone, 1 } } },
    };
}

/** Les presets enregistrés avec l'ancienne barre (fichiers .orsrpreset du même dossier) passent dans le navigateur,
    une fois : chacun est recopié au format de la nouvelle barre, l'original reste en place. */
inline void importOldUserPresets (const PresetBank& bank)
{
    for (const auto& file : bank.getFolder().findChildFiles (juce::File::findFiles, false, "*.orsrpreset"))
    {
        const auto target = file.withFileExtension (bank.getExtension());
        if (target.existsAsFile())
            continue;

        if (const auto xml = juce::XmlDocument::parse (file))
        {
            juce::ValueTree snapshot ("Snapshot");
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                if (const auto id = p->getStringAttribute ("id"); ! notStored().contains (id))
                    snapshot.setProperty (id, (float) p->getDoubleAttribute ("value"), nullptr);

            if (const auto out = snapshot.createXml())
                out->writeTo (target);
        }
    }
}
} // namespace riser
} // namespace onirique
