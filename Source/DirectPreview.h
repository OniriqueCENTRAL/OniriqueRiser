#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

/*  Écoute directe (Mac) : une file de sortie Audio Queue vers la sortie audio du Mac, hors du DAW.

    Sur une piste audio sans le bouton I, Logic ne fait plus tourner les plugins à l'arrêt : processBlock n'est plus
    appelé et LISTEN restait muet. Le RISER joue alors l'écoute lui-même par cette file. L'Audio Queue convertit la
    fréquence vers celle de la sortie et ne touche jamais aux réglages de la carte son (fréquence, taille de tampon)
    que le DAW utilise. Ailleurs qu'au Mac, start() ne fait rien et rend false.

    fill est appelé sur le thread de la file : il ajoute numCh canaux de n échantillons (tampons mis à zéro avant). */
class DirectPreview
{
public:
    using Fill = std::function<void (float* const* out, int numCh, int n)>;

    explicit DirectPreview (Fill f);
    ~DirectPreview();

    bool start (double sampleRate);   // thread de l'interface
    void stop();
    bool isRunning() const noexcept { return running; }
    double sampleRate() const noexcept { return fs; }

    struct Impl;

private:
    Fill fill;
    std::unique_ptr<Impl> impl;
    bool running = false;
    double fs = 0.0;

    JUCE_DECLARE_NON_COPYABLE (DirectPreview)
};
