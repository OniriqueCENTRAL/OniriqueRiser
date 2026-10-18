#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include "Biquad.h"
#include "Common.h"
#include "DeEsser.h"
#include "VocalFX.h"

namespace onirique
{
/*  ONIRIQUE RISER : le moteur (hors temps réel, sur un thread à part).

    La voix capturée en jouant le DAW -> une montée de reverb inversée ("reverse reverb") qui finit pile là où la voix
    commence, de la durée choisie en temps et en mesures au tempo du morceau.

    1. Les phrases : enveloppe RMS (10 ms) ; une phrase commence là où le son revient après au moins 250 ms de silence
       (seuil : 35 dB sous le plus fort passage de la prise, jamais sous -50 dBFS).
    2. La SOURCE : le début de la phrase choisie (SOURCE temps), adouci à la fin. Avec S-CLEAN, ses S sont retirés deux
       fois : le de-esser de la VOCALBOX à fond, puis, dans l'envoi vers la reverb seulement, un détecteur de sifflantes
       (part de l'énergie au-dessus de 5 kHz) qui fait plonger l'envoi jusqu'à -24 dB sur chaque S : aucune sifflante
       ne ressort à l'envers dans la montée.
    3. La source est retournée, envoyée à 100 % dans la reverb de la VOCALBOX (réseau de 16 lignes ; ROOM, PLATE,
       DARK PLATE, HALL, SHIMMER) réglée pour durer au moins la montée, puis le retour est retourné à nouveau :
       y(t) = somme de h(tau) x(t + tau), la reverb "avant" la voix. On garde la partie qui précède le premier mot.
    4. Couleur : SWEEP (filtre qui s'ouvre en montant), PHONE, CHORUS, TREMOLO (doubles-croches au tempo), WIDE ;
       forme (CURVE), fondu d'entrée, normalisation à -1 dBFS puis GAIN.
    5. Le fichier commence sur un temps de la grille du DAW (silence ajouté devant) : on le pose sur ce temps, la montée
       finit exactement sur la voix.
*/
struct RiserCapture
{
    juce::AudioBuffer<float> audio;   // stéréo
    double fs = 48000.0;
    double startSec = 0.0;            // position sur la ligne de temps du DAW (secondes) du 1er échantillon
    double startPpq = 0.0;            // ... en noires
    bool hasPpq = false;
    double bpm = 120.0;
    int sigNum = 4, sigDen = 4;
};

struct RiserSettings
{
    int lengthIdx = 2;                // 1 TEMPS, 2 TEMPS, 1 MESURE, 2 MESURES, 4 MESURES
    int space = 1;                    // ROOM, PLATE, DARK PLATE, HALL, SHIMMER
    float sourceBeats = 1.0f;         // durée du début de phrase envoyée dans la reverb
    float size = 0.6f;                // 0..1 : longueur de la queue par rapport à la montée
    float curve = 0.0f;               // 0..1 : la montée se creuse (arrive plus tard, plus fort)
    float gainDb = 0.0f;
    bool sClean = true, phone = false, chorus = false, tremolo = false, sweep = false, wide = false;
    // réglages des effets (0..1, sauf la cadence du trémolo : 0 1/4, 1 1/8, 2 1/16, 3 1/32 ; le départ du SWEEP en Hz)
    float phoneMix = 1.0f, phoneDrive = 0.5f, chorusMix = 1.0f, chorusDepth = 0.65f, tremoloMix = 1.0f, sweepMix = 1.0f, sweepStart = 300.0f,
          wideMix = 1.0f, wideWidth = 0.9f;
    int tremoloRate = 2;
    bool raw = false;   // RAW : la reverse reverb seule (ni effets, ni CURVE)
    int phrase = 0;
};

struct RiserResult
{
    juce::AudioBuffer<float> riser;   // stéréo : silence de calage puis la montée ; finit sur la voix
    double fs = 48000.0;
    int padSamples = 0;               // silence de calage au début
    int riserSamples = 0;
    double fileStartSec = 0.0;        // position du DAW où poser le fichier (secondes)
    double fileStartPpq = 0.0;
    bool hasGrid = false;
    int dropBar = 1, dropBeat = 1;    // mesure et temps où poser le fichier
    double onsetSec = 0.0;            // début de la voix (ligne de temps)
    int phrase = 0, phraseCount = 0;
    juce::String lengthLabel;
    std::vector<float> riserEnv;      // affichage : crête par tranche
    std::vector<float> voiceEnv;      // affichage : la voix juste après (même durée que la montée)
    juce::AudioBuffer<float> voiceAfter;   // écoute : la voix qui suit la montée (2 s)
};

class RiserEngine
{
public:
    static const juce::StringArray& lengthNames()
    {
        static const juce::StringArray n { "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS" };
        return n;
    }

    static double lengthBeats (int idx, int sigNum, int sigDen)
    {
        const double bar = sigNum * 4.0 / juce::jmax (1, sigDen);   // une mesure, en noires
        switch (juce::jlimit (0, 4, idx))
        {
            case 0: return 1.0;
            case 1: return 2.0;
            case 2: return bar;
            case 3: return 2.0 * bar;
            default: return 4.0 * bar;
        }
    }

    // débuts de phrases (échantillons)
    static std::vector<int> findPhrases (const juce::AudioBuffer<float>& a, double fs)
    {
        const int hop = juce::jmax (1, (int) (0.010 * fs)), n = a.getNumSamples();
        std::vector<float> rms;
        float peak = 0.0f;
        for (int s = 0; s + hop <= n; s += hop)
        {
            double e = 0.0;
            for (int c = 0; c < a.getNumChannels(); ++c)
            {
                const float* x = a.getReadPointer (c, s);
                for (int i = 0; i < hop; ++i) e += (double) x[i] * x[i];
            }
            const float r = (float) std::sqrt (e / (hop * juce::jmax (1, a.getNumChannels())));
            rms.push_back (r);
            peak = std::max (peak, r);
        }
        std::vector<int> onsets;
        if (peak <= 0.0f) return onsets;
        const float thr = std::max (dbToGain (-50.0f), peak * dbToGain (-35.0f));
        const int minGap = (int) std::ceil (0.25 * fs / hop);
        int quiet = minGap;   // le début de la prise compte comme un silence
        for (int f = 0; f < (int) rms.size(); ++f)
        {
            if (rms[(size_t) f] >= thr)
            {
                if (quiet >= minGap)
                {
                    // affinage : premier échantillon au-dessus d'un seuil plus bas, dans la trame d'avant
                    int s = juce::jmax (0, (f - 1) * hop);
                    const float fine = thr * 0.5f;
                    for (int i = s; i < (f + 1) * hop && i < n; ++i)
                    {
                        float v = 0.0f;
                        for (int c = 0; c < a.getNumChannels(); ++c) v = std::max (v, std::abs (a.getSample (c, i)));
                        if (v >= fine) { s = i; break; }
                    }
                    onsets.push_back (s);
                }
                quiet = 0;
            }
            else
                ++quiet;
        }
        return onsets;
    }

    static RiserResult render (const RiserCapture& cap, const RiserSettings& st)
    {
        RiserResult res;
        res.fs = cap.fs;
        const double fs = cap.fs;
        const int n = cap.audio.getNumSamples();
        const auto phrases = findPhrases (cap.audio, fs);
        res.phraseCount = (int) phrases.size();
        if (phrases.empty() || n < 16) return res;
        res.phrase = juce::jlimit (0, res.phraseCount - 1, st.phrase);
        const int onset = phrases[(size_t) res.phrase];

        const double bpm = juce::jlimit (20.0, 400.0, cap.bpm);
        const double beatSec = 60.0 / bpm;
        const double beats = lengthBeats (st.lengthIdx, cap.sigNum, cap.sigDen);
        res.lengthLabel = lengthNames()[juce::jlimit (0, 4, st.lengthIdx)];
        const int R = juce::jmax (16, (int) std::lround (beats * beatSec * fs));
        res.riserSamples = R;

        // ---- 2. la source : le début de la phrase (5 ms avant l'attaque), fondu de sortie
        const int pre = (int) (0.005 * fs);
        const int s0 = juce::jmax (0, onset - pre);
        const int S = juce::jlimit ((int) (0.12 * fs), juce::jmax ((int) (0.12 * fs), n - s0),
                                    (int) std::lround (juce::jlimit (0.1, 4.0, (double) st.sourceBeats * beatSec) * fs));
        const int Sa = juce::jmin (S, n - s0);
        juce::AudioBuffer<float> src (2, S);
        src.clear();
        for (int c = 0; c < 2; ++c)
            src.copyFrom (c, 0, cap.audio, juce::jmin (c, cap.audio.getNumChannels() - 1), s0, Sa);
        {
            const int fo = juce::jmin (Sa / 2, (int) (0.030 * fs));
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < fo; ++i)
                    src.setSample (c, Sa - fo + i, src.getSample (c, Sa - fo + i) * 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (float) i / (float) fo)));
        }

        juce::AudioBuffer<float> send (src);
        if (st.sClean) cleanSibilance (src, send, fs, S);

        // ---- 3. retournée -> reverb à 100 % -> retournée
        const int L = S + R + (int) (0.1 * fs);
        juce::AudioBuffer<float> in (2, L), wet (2, L);
        in.clear(); wet.clear();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < S; ++i) in.setSample (c, i, send.getSample (c, S - 1 - i));
        {
            fx::VocalReverb rv;
            fx::VocalReverb::Params p;
            static constexpr int modes[] { fx::VocalReverb::room, fx::VocalReverb::plate1, fx::VocalReverb::plate2, fx::VocalReverb::hall, fx::VocalReverb::shimmer };
            p.on = true;
            p.mode = modes[juce::jlimit (0, 4, st.space)];
            p.preDelayMs = 0.0f;
            p.decay = (float) juce::jlimit (0.8, 20.0, (R / fs) * (0.8 + 1.8 * (double) st.size));
            p.size = 0.55f + 0.4f * st.size;
            p.diffusion = 0.85f;
            p.modRate = 0.6f; p.modDepth = 0.3f;
            p.mix = 1.0f;
            p.lowCut = 160.0f; p.highCut = 9500.0f; p.ducking = 0.0f;
            rv.set (p);
            rv.prepare (fs);
            rv.set (p);
            for (int pos = 0; pos < L; pos += 512)
            {
                const int m = juce::jmin (512, L - pos);
                const float* ip[2] { in.getReadPointer (0, pos), in.getReadPointer (1, pos) };
                float* wp[2] { wet.getWritePointer (0, pos), wet.getWritePointer (1, pos) };
                rv.processSend (ip, wp, 2, m);
            }
        }
        // retour retourné : l'attaque de la voix (src[0]) tombe en L - S ; la montée est juste avant
        const int m0 = L - S;
        juce::AudioBuffer<float> riser (2, R);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < R; ++i)
            {
                const int m = m0 - R + i;               // indice dans le retour retourné
                riser.setSample (c, i, m >= 0 ? wet.getSample (c, L - 1 - m) : 0.0f);
            }

        // ---- 4. couleur et forme (RAW : la reverse reverb telle quelle)
        if (! st.raw) colour (riser, fs, beatSec, st);
        else colourRaw (riser, fs);

        float pk = 0.0f;
        for (int c = 0; c < 2; ++c) pk = std::max (pk, riser.getMagnitude (c, 0, R));
        const float norm = pk > 1.0e-6f ? dbToGain (-1.0f) / pk : 1.0f;
        riser.applyGain (norm * dbToGain (juce::jlimit (-24.0f, 12.0f, st.gainDb)));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < R; ++i) riser.setSample (c, i, sanitize (riser.getSample (c, i), 1.0f));

        // ---- 5. calage sur la grille du DAW
        res.onsetSec = cap.startSec + onset / fs;
        double fileStartSec = res.onsetSec - R / fs;
        int pad = 0;
        if (cap.hasPpq)
        {
            const double onsetPpq = cap.startPpq + (onset / fs) / beatSec;
            const double riserPpq = onsetPpq - (double) R / fs / beatSec;
            const double grid = std::floor (riserPpq + 1.0e-6);
            pad = juce::jmax (0, (int) std::lround ((riserPpq - grid) * beatSec * fs));
            res.fileStartPpq = grid;
            res.hasGrid = grid >= 0.0;
            const double qpb = cap.sigNum * 4.0 / juce::jmax (1, cap.sigDen);   // noires par mesure
            const double barIdx = std::floor (grid / qpb + 1.0e-9);
            res.dropBar = (int) barIdx + 1;
            res.dropBeat = (int) std::lround ((grid - barIdx * qpb) * juce::jmax (1, cap.sigDen) / 4.0) + 1;
            fileStartSec -= pad / fs;
        }
        res.padSamples = pad;
        res.fileStartSec = fileStartSec;
        res.riser.setSize (2, pad + R);
        res.riser.clear();
        for (int c = 0; c < 2; ++c) res.riser.copyFrom (c, pad, riser, c, 0, R);

        // ---- affichage et écoute
        const int bins = 240;
        res.riserEnv = envelope (riser, 0, R, bins);
        res.voiceEnv = envelope (cap.audio, onset, juce::jmin (R, n - onset), bins);
        const int va = juce::jmin ((int) (2.0 * fs), n - onset);
        res.voiceAfter.setSize (2, va);
        for (int c = 0; c < 2; ++c) res.voiceAfter.copyFrom (c, 0, cap.audio, juce::jmin (c, cap.audio.getNumChannels() - 1), onset, va);
        return res;
    }

private:
    static std::vector<float> envelope (const juce::AudioBuffer<float>& a, int start, int len, int bins)
    {
        std::vector<float> e ((size_t) bins, 0.0f);
        if (len <= 0) return e;
        for (int b = 0; b < bins; ++b)
        {
            const int s = start + (int) ((int64_t) len * b / bins), z = start + (int) ((int64_t) len * (b + 1) / bins);
            float m = 0.0f;
            for (int c = 0; c < a.getNumChannels(); ++c) if (z > s) m = std::max (m, a.getMagnitude (c, s, z - s));
            e[(size_t) b] = m;
        }
        return e;
    }

    // S-CLEAN : de-esser à fond sur la source, puis l'envoi plonge sur chaque sifflante
    static void cleanSibilance (juce::AudioBuffer<float>& src, juce::AudioBuffer<float>& send, double fs, int S)
    {
        {
            DeEsser de;
            de.prepare (fs, true);   // hors temps réel : sans anticipation (aucun retard à compenser)
            de.setParams (6500.0f, 1.0f, 0.0f, true);
            for (int pos = 0; pos < S; pos += 512)
            {
                const int m = juce::jmin (512, S - pos);
                float* p[2] { src.getWritePointer (0, pos), src.getWritePointer (1, pos) };
                de.process (p, 2, m);
            }
        }
        send.makeCopyOf (src);
        Biquad hp, lp;
        hp.setCoeffs (BiquadCoeffs::highpass (fs, 5000.0, 0.707));
        lp.setCoeffs (BiquadCoeffs::lowpass (fs, 9000.0, 0.707));
        const float atk = (float) (1.0 - std::exp (-1.0 / (0.002 * fs))), rel = (float) (1.0 - std::exp (-1.0 / (0.040 * fs)));
        const float gs = (float) (1.0 - std::exp (-1.0 / (0.004 * fs)));
        float eh = 0.0f, ef = 0.0f, g = 1.0f;
        for (int i = 0; i < S; ++i)
        {
            const float x = 0.5f * (src.getSample (0, i) + src.getSample (1, i));
            const float h = std::abs (hp.process (x, 0));
            const float f = std::abs (x);
            eh += (h > eh ? atk : rel) * (h - eh);
            ef += (f > ef ? atk : rel) * (f - ef);
            const float ratio = ef > 1.0e-6f ? eh / ef : 0.0f;        // part des aigus : forte sur un S
            const float t = juce::jlimit (0.0f, 1.0f, (ratio - 0.30f) / 0.30f);
            const float target = dbToGain (-24.0f * t);
            g += gs * (target - g);
            for (int c = 0; c < 2; ++c) send.setSample (c, i, lp.process (src.getSample (c, i), c) * g);
        }
    }

    // un effet sur une copie, puis mélangé au signal selon son MIX
    template <typename Fn>
    static void withMix (juce::AudioBuffer<float>& r, float mix, Fn&& effect)
    {
        mix = juce::jlimit (0.0f, 1.0f, mix);
        if (mix <= 0.0f) return;
        juce::AudioBuffer<float> wet (r);
        effect (wet);
        const int R = r.getNumSamples();
        for (int c = 0; c < 2; ++c)
        {
            float* d = r.getWritePointer (c);
            const float* w = wet.getReadPointer (c);
            for (int i = 0; i < R; ++i) d[i] += mix * (w[i] - d[i]);
        }
    }

    static void colour (juce::AudioBuffer<float>& r, double fs, double beatSec, const RiserSettings& st)
    {
        const int R = r.getNumSamples();

        if (st.sweep)   // filtre passe-bas qui s'ouvre de START jusqu'à 18 kHz au fil de la montée
            withMix (r, st.sweepMix, [&] (juce::AudioBuffer<float>& w)
            {
                float* ch[2] { w.getWritePointer (0), w.getWritePointer (1) };
                const double f0 = juce::jlimit (40.0, 4000.0, (double) st.sweepStart), f1 = std::min (18000.0, 0.45 * fs);
                Biquad lp;
                for (int i = 0; i < R; ++i)
                {
                    if ((i & 31) == 0) lp.setCoeffs (BiquadCoeffs::lowpass (fs, f0 * std::pow (f1 / f0, (double) i / R), 0.9));
                    for (int c = 0; c < 2; ++c) ch[c][i] = lp.process (ch[c][i], c);
                }
            });
        if (st.phone)   // téléphone : 450 Hz - 3.2 kHz, du grain selon DRIVE
            withMix (r, st.phoneMix, [&] (juce::AudioBuffer<float>& w)
            {
                float* ch[2] { w.getWritePointer (0), w.getWritePointer (1) };
                Biquad a, b, c2, d;
                a.setCoeffs (BiquadCoeffs::highpass (fs, 450.0, 0.707)); b.setCoeffs (BiquadCoeffs::highpass (fs, 450.0, 0.707));
                c2.setCoeffs (BiquadCoeffs::lowpass (fs, 3200.0, 0.707)); d.setCoeffs (BiquadCoeffs::lowpass (fs, 3200.0, 0.707));
                const float drive = 1.0f + 19.0f * juce::jlimit (0.0f, 1.0f, st.phoneDrive);
                for (int i = 0; i < R; ++i)
                    for (int c = 0; c < 2; ++c)
                        ch[c][i] = std::tanh (drive * d.process (c2.process (b.process (a.process (ch[c][i], c), c), c), c)) / std::sqrt (drive);
            });
        if (st.chorus)
            withMix (r, st.chorusMix, [&] (juce::AudioBuffer<float>& w)
            {
                fx::Chorus co;
                co.prepare (fs);
                co.set (true, 0.7f, juce::jlimit (0.0f, 1.0f, st.chorusDepth), 1.0f);
                for (int pos = 0; pos < R; pos += 512)
                {
                    const int m = juce::jmin (512, R - pos);
                    float* p[2] { w.getWritePointer (0, pos), w.getWritePointer (1, pos) };
                    co.process (p, 2, m);
                }
            });
        if (st.tremolo)   // au tempo (TIMING), calé sur la fin (la voix) ; MIX = profondeur
            withMix (r, st.tremoloMix, [&] (juce::AudioBuffer<float>& w)
            {
                float* ch[2] { w.getWritePointer (0), w.getWritePointer (1) };
                const double period = beatSec / (double) (1 << juce::jlimit (0, 3, st.tremoloRate));
                for (int i = 0; i < R; ++i)
                {
                    const double ph = std::fmod ((double) (R - i) / fs, period) / period;
                    const float g = 1.0f - (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * ph));
                    for (int c = 0; c < 2; ++c) ch[c][i] *= g;
                }
            });
        if (st.wide)   // élargissement : côtés renforcés selon WIDTH
            withMix (r, st.wideMix, [&] (juce::AudioBuffer<float>& w)
            {
                float* ch[2] { w.getWritePointer (0), w.getWritePointer (1) };
                const float k = 1.0f + juce::jlimit (0.0f, 1.0f, st.wideWidth);
                for (int i = 0; i < R; ++i)
                {
                    const float m = 0.5f * (ch[0][i] + ch[1][i]), s = 0.5f * (ch[0][i] - ch[1][i]) * k;
                    ch[0][i] = m + s; ch[1][i] = m - s;
                }
            });

        // forme : CURVE creuse le début ; fondu d'entrée de 30 ms (rien ne claque au début du fichier)
        shape (r, fs, 1.0f + 3.0f * juce::jlimit (0.0f, 1.0f, st.curve));
    }

    // RAW : la reverse reverb seule, avec seulement le fondu d'entrée
    static void colourRaw (juce::AudioBuffer<float>& r, double fs) { shape (r, fs, 1.0f); }

    static void shape (juce::AudioBuffer<float>& r, double fs, float p)
    {
        const int R = r.getNumSamples();
        const int fi = juce::jmin (R / 4, (int) (0.030 * fs));
        for (int i = 0; i < R; ++i)
        {
            float g = p > 1.001f ? std::pow ((float) (i + 1) / (float) R, p - 1.0f) : 1.0f;
            if (i < fi) g *= (float) i / (float) fi;
            for (int c = 0; c < 2; ++c) r.setSample (c, i, r.getSample (c, i) * g);
        }
    }
};
} // namespace onirique
