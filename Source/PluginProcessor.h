#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "Parameters.h"
#include "PresetManager.h"
#include "DSP/RiserEngine.h"

/*  ONIRIQUE RISER : posé sur la piste de voix, il laisse passer le son tel quel.

    Capture automatique : dès que le DAW joue, le plugin écoute ; quand la voix arrive, il l'enregistre (avec les 2 s
    d'avant, pour ne jamais rater l'attaque) et note sa place exacte sur la ligne de temps (secondes, noires, tempo,
    mesure). À l'arrêt de la lecture (ou à un saut de lecture, une boucle), la prise est prête et la montée calculée
    sur un thread à part. Une lecture sans voix ne remplace pas la prise d'avant.

    Ensuite : glisser le fichier dans le DAW (exportRiser), l'écouter dans le plugin DAW à l'arrêt (LISTEN / LOOP ; dans
    la vue RAW, LISTEN joue la partie de la prise choisie entre les poignées), ou IN SYNC : la montée est jouée par le
    plugin lui-même, au bon moment, pendant la lecture.

    INPUT CLOSED : plus rien n'est capturé, la prise reste telle quelle (une relecture ne remplace pas la lead, et la
    montée posée sur la même piste ne se capture pas elle-même). Une capture en cours se termine à la fermeture.
*/
class OniriqueRiserAudioProcessor : public juce::AudioProcessor,
                                    private juce::Timer
{
public:
    OniriqueRiserAudioProcessor();
    ~OniriqueRiserAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (onirique::ids::bypass); }

    const juce::String getName() const override { return "ONIRIQUE RISER"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    onirique::PresetManager presets { apvts };

    //==============================================================================
    // Pour l'interface (thread de l'interface)
    enum class Capture { waiting, listening, recording };   // waiting : DAW à l'arrêt ; listening : lecture, pas encore de voix
    Capture captureState() const noexcept;
    double recordingSeconds() const noexcept { return (double) recLen.load() / juce::jmax (1.0, rate); }
    bool hasTake() const;
    bool isRendering() const noexcept { return renderPending.load() || renderBusy.load(); }
    std::shared_ptr<const onirique::RiserResult> result() const;
    juce::File exportRiser (juce::String& error);
    void setListen (bool on);
    bool isListening() const noexcept { return listenOn.load(); }
    double listenPosition() const noexcept { return listenPos.load(); }   // 0..1 : position dans la montée
    std::atomic<bool> loop { false };
    // la source choisie à la main dans la vue RAW (secondes depuis le début de la prise) ; resetTrim : automatique
    // (début de la phrase, durée SOURCE). Une nouvelle prise, une autre phrase ou un autre SOURCE reviennent à l'automatique.
    void setTrim (double startSec, double endSec) { trimA = startSec; trimB = endSec; }
    void resetTrim() { trimA = -1.0; trimB = -1.0; }
    bool isInputOpen() const { return apvts.getRawParameterValue (onirique::ids::inputOpen)->load() > 0.5f; }
    float consumeLevel() noexcept { return level.exchange (0.0f); }
    double hostBpm() const noexcept { return bpmNow.load(); }
   #if ONIRIQUE_UI_TEST
    void loadTestTake();   // essais locaux : une voix de synthèse, comme si le DAW venait de la jouer
    void testTick() { timerCallback(); }   // essais : un tour du minuteur de l'interface
   #endif

private:
    void timerCallback() override;
    onirique::RiserSettings readSettings() const;
    void finishCapture();

    class RenderThread;
    std::unique_ptr<RenderThread> renderThread;
    friend class RenderThread;

    double rate = 48000.0;

    // capture (thread audio ; tampons alloués dans prepareToPlay)
    static constexpr double kPreroll = 2.0, kMaxTake = 32.0;
    juce::AudioBuffer<float> preroll, rec;
    int preWrite = 0, preFill = 0;
    std::atomic<int> recLen { 0 };
    std::atomic<int> capPhase { 0 };            // 0 inactive, 1 à l'écoute, 2 enregistre, 3 prise prête (attend l'interface)
    double recStartSec = 0.0, recStartPpq = 0.0, recBpm = 120.0, lastTime = 0.0;
    bool recHasPpq = false, wasPlaying = false;
    int recSigNum = 4, recSigDen = 4;
    std::atomic<double> bpmNow { 120.0 };

    // prise et rendu (thread de l'interface / thread de rendu) ; le thread audio n'attend jamais (try-lock)
    mutable juce::SpinLock lock;
    std::shared_ptr<onirique::RiserCapture> take;
    std::shared_ptr<const onirique::RiserResult> current;
    std::shared_ptr<const onirique::RiserResult> retired[4];   // anciens rendus, libérés par le thread de rendu (jamais par l'audio)
    int retiredPos = 0;
    onirique::RiserSettings lastSettings;
    bool haveSettings = false;
    std::atomic<bool> renderPending { false }, renderBusy { false };
    std::atomic<double> trimA { -1.0 }, trimB { -1.0 };

    // écoute dans le plugin
    std::atomic<bool> listenOn { false };
    std::atomic<double> listenPos { 0.0 };
    int listenIdx = 0;

    std::atomic<float> level { 0.0f };
    juce::SmoothedValue<float> bypassMix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueRiserAudioProcessor)
};
