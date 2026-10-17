    // Presets d'usine (inclus dans PresetManager). Valeurs en unités réelles ; un réglage non cité reprend sa valeur par défaut.
    // length : 0 1 BEAT, 1 2 BEATS, 2 1 BAR, 3 2 BARS, 4 4 BARS ; space : 0 ROOM, 1 PLATE, 2 DARK PLATE, 3 HALL, 4 SHIMMER
    static Factory preset (const char* name, std::initializer_list<std::pair<const char*, float>> v)
    {
        Factory f { name, {} };
        for (const auto& [id, value] : v) f.values[id] = value;
        return f;
    }

    static std::vector<Factory> makeFactory()
    {
        return {
            { "Init", {} },
            preset ("Classic Swell",  { { ids::length, 2.0f }, { ids::space, 1.0f }, { ids::size, 60.0f } }),
            preset ("Short Pickup",   { { ids::length, 0.0f }, { ids::space, 0.0f }, { ids::source, 0.5f }, { ids::size, 40.0f } }),
            preset ("Ghost Hall",     { { ids::length, 3.0f }, { ids::space, 3.0f }, { ids::size, 85.0f }, { ids::sweep, 1.0f }, { ids::curve, 30.0f } }),
            preset ("Shimmer Rise",   { { ids::length, 3.0f }, { ids::space, 4.0f }, { ids::size, 75.0f }, { ids::wide, 1.0f } }),
            preset ("Dark Tunnel",    { { ids::length, 2.0f }, { ids::space, 2.0f }, { ids::size, 70.0f }, { ids::sweep, 1.0f } }),
            preset ("Phone Call",     { { ids::length, 2.0f }, { ids::space, 0.0f }, { ids::phone, 1.0f }, { ids::size, 45.0f } }),
            preset ("Stutter Rise",   { { ids::length, 2.0f }, { ids::space, 1.0f }, { ids::tremolo, 1.0f }, { ids::curve, 40.0f } }),
            preset ("Wide Chorus",    { { ids::length, 2.0f }, { ids::space, 3.0f }, { ids::chorus, 1.0f }, { ids::wide, 1.0f } }),
            preset ("Long Build",     { { ids::length, 4.0f }, { ids::space, 3.0f }, { ids::size, 90.0f }, { ids::source, 2.0f }, { ids::curve, 60.0f }, { ids::sweep, 1.0f } }),
            preset ("Drill Pickup",   { { ids::length, 1.0f }, { ids::space, 1.0f }, { ids::size, 50.0f }, { ids::tremolo, 1.0f }, { ids::phone, 1.0f } }),
        };
    }
