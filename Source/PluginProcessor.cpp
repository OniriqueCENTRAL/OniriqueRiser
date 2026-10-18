#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace onirique;

//==============================================================================
// Rendu de la montée sur un thread à part : toujours la dernière prise avec les derniers réglages.
class OniriqueRiserAudioProcessor::RenderThread : public juce::Thread
{
public:
    explicit RenderThread (OniriqueRiserAudioProcessor& p) : juce::Thread ("ONIRIQUE RISER render"), owner (p) { startThread(); }
    ~RenderThread() override { stopThread (4000); }

    void request (std::shared_ptr<const RiserCapture> c, const RiserSettings& s)
    {
        {
            const juce::ScopedLock sl (jobLock);
            job = std::move (c);
            settings = s;
            pending = true;
        }
        notify();
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            std::shared_ptr<const RiserCapture> c;
            RiserSettings s;
            {
                const juce::ScopedLock sl (jobLock);
                if (pending) { c = job; s = settings; pending = false; }
            }
            if (c == nullptr) { owner.renderPending = false; wait (-1); continue; }
            owner.renderBusy = true;
            owner.renderPending = false;
            auto r = std::make_shared<const RiserResult> (RiserEngine::render (*c, s));
            std::shared_ptr<const RiserResult> victim;
            {
                const juce::SpinLock::ScopedLockType sl (owner.lock);
                // l'ancien rendu est gardé quelques rendus de plus : le thread audio, qui en tient peut-être une copie le temps
                // d'un bloc, n'a jamais la dernière (aucune libération de mémoire sur le thread audio)
                victim = std::move (owner.retired[(size_t) owner.retiredPos]);
                owner.retired[(size_t) owner.retiredPos] = owner.current;
                owner.retiredPos = (owner.retiredPos + 1) & 3;
                owner.current = std::move (r);
            }
            victim.reset();   // libéré ici, hors du verrou
            owner.renderBusy = false;
        }
    }

    OniriqueRiserAudioProcessor& owner;
    juce::CriticalSection jobLock;
    std::shared_ptr<const RiserCapture> job;
    RiserSettings settings;
    bool pending = false;
};

//==============================================================================
OniriqueRiserAudioProcessor::OniriqueRiserAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    renderThread = std::make_unique<RenderThread> (*this);
    startTimerHz (20);
}

OniriqueRiserAudioProcessor::~OniriqueRiserAudioProcessor()
{
    stopTimer();
    renderThread.reset();
}

bool OniriqueRiserAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void OniriqueRiserAudioProcessor::prepareToPlay (double sr, int)
{
    rate = sr;
    preroll.setSize (2, (int) (kPreroll * sr) + 1);
    rec.setSize (2, (int) ((kPreroll + kMaxTake) * sr));
    preroll.clear();
    preWrite = preFill = 0;
    recLen = 0;
    if (capPhase.load() != 3) capPhase = 0;
    wasPlaying = false;
    bypassMix.reset (sr, 0.02);
    listenIdx = 0;
}

OniriqueRiserAudioProcessor::Capture OniriqueRiserAudioProcessor::captureState() const noexcept
{
    const int ph = capPhase.load();
    return ph == 2 ? Capture::recording : (ph == 1 ? Capture::listening : Capture::waiting);
}

bool OniriqueRiserAudioProcessor::hasTake() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return take != nullptr;
}

std::shared_ptr<const RiserResult> OniriqueRiserAudioProcessor::result() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return current;
}

void OniriqueRiserAudioProcessor::setListen (bool on)
{
    listenIdx = 0;
    listenPos = 0.0;
    listenOn = on;
}

// fin de prise (thread audio) : la prise attend que l'interface la reprenne ; rien de nouveau n'est capturé d'ici là
void OniriqueRiserAudioProcessor::finishCapture()
{
    capPhase = recLen.load() > 0 ? 3 : 0;
}

void OniriqueRiserAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int c = inCh; c < outCh; ++c) buffer.clear (c, 0, n);
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    if (n <= 0 || numCh <= 0) return;

    // ---- position du DAW
    bool playing = false, hasPpq = false;
    double timeSec = 0.0, ppq = 0.0, bpm = 120.0;
    int sigNum = 4, sigDen = 4;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying() || pos->getIsRecording();
            if (auto t = pos->getTimeInSeconds()) timeSec = *t;
            else if (auto ts = pos->getTimeInSamples()) timeSec = (double) *ts / rate;
            if (auto p = pos->getPpqPosition()) { ppq = *p; hasPpq = true; }
            if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0) bpm = *b;
            if (auto s = pos->getTimeSignature()) { sigNum = s->numerator; sigDen = s->denominator; }
        }
    bpmNow.store (bpm, std::memory_order_relaxed);

    // ---- capture automatique
    const int phase = capPhase.load();
    if (playing && ! wasPlaying && phase != 3) { capPhase = 1; preWrite = preFill = 0; recLen = 0; }
    if (playing && capPhase.load() == 1)
    {
        // pré-écoute : les 2 dernières secondes, pour garder l'attaque quand la voix arrive
        float pk = 0.0f;
        const int P = preroll.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float x = buffer.getSample (juce::jmin (c, numCh - 1), i);
                preroll.setSample (c, preWrite, x);
                pk = std::max (pk, std::abs (x));
            }
            preWrite = (preWrite + 1) % P;
            preFill = juce::jmin (P, preFill + 1);
        }
        if (pk > 0.0056f)   // -45 dBFS : la voix arrive -> la prise commence, avec la pré-écoute devant
        {
            for (int i = 0; i < preFill; ++i)
            {
                const int s = (preWrite - preFill + i + P) % P;
                for (int c = 0; c < 2; ++c) rec.setSample (c, i, preroll.getSample (c, s));
            }
            recLen = preFill;
            const double back = (double) (preFill - n) / rate;   // la pré-écoute contient déjà ce bloc
            recStartSec = timeSec - back;
            recStartPpq = ppq - back * bpm / 60.0;
            recHasPpq = hasPpq;
            recBpm = bpm; recSigNum = sigNum; recSigDen = sigDen;
            capPhase = 2;
        }
    }
    else if (playing && capPhase.load() == 2)
    {
        // un saut de lecture (boucle, clic sur la règle) ou une prise pleine termine la prise
        if (std::abs (timeSec - lastTime) > 0.05)
            finishCapture();
        else
        {
            const int at = recLen.load(), fits = juce::jmin (n, rec.getNumSamples() - at);
            for (int c = 0; c < 2; ++c) rec.copyFrom (c, at, buffer, juce::jmin (c, numCh - 1), 0, fits);
            recLen = at + fits;
            if (fits < n) finishCapture();
        }
    }
    if (! playing && wasPlaying)
    {
        if (capPhase.load() == 2) finishCapture();
        else if (capPhase.load() == 1) capPhase = 0;   // lecture sans voix : la prise d'avant reste
    }
    wasPlaying = playing;
    lastTime = timeSec + n / rate;

    // ---- sortie : la voix telle quelle + la montée (IN SYNC pendant la lecture, LISTEN à l'arrêt)
    bypassMix.setTargetValue (apvts.getRawParameterValue (ids::bypass)->load() > 0.5f ? 0.0f : 1.0f);
    std::shared_ptr<const RiserResult> res;
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (sl.isLocked()) res = current;
    }
    const bool usable = res != nullptr && res->riser.getNumSamples() > 0 && std::abs (res->fs - rate) < 0.5;
    if (usable && playing && apvts.getRawParameterValue (ids::inSync)->load() > 0.5f)
    {
        const int64_t idx0 = (int64_t) std::llround ((timeSec - res->fileStartSec) * rate);
        const int len = res->riser.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const int64_t j = idx0 + i;
            const float g = bypassMix.getNextValue();
            if (j < 0 || j >= len) continue;
            for (int c = 0; c < numCh; ++c) buffer.addSample (c, i, g * res->riser.getSample (c, (int) j));
        }
    }
    else if (usable && ! playing && listenOn.load())
    {
        const int total = res->riser.getNumSamples() - res->padSamples;   // la montée seule (sans la voix qui suit)
        for (int i = 0; i < n; ++i)
        {
            if (listenIdx >= total)
            {
                if (! loop.load()) { listenOn = false; break; }
                listenIdx = 0;
            }
            for (int c = 0; c < numCh; ++c)
                buffer.addSample (c, i, res->riser.getSample (c, res->padSamples + listenIdx));
            ++listenIdx;
        }
        listenPos = total > 0 ? (double) listenIdx / total : 0.0;
    }
    else
        bypassMix.skip (n);
    if (! listenOn.load()) listenIdx = 0;

    float pk = 0.0f;
    for (int c = 0; c < numCh; ++c) pk = std::max (pk, buffer.getMagnitude (c, 0, n));
    if (pk > level.load (std::memory_order_relaxed)) level.store (pk, std::memory_order_relaxed);
}

RiserSettings OniriqueRiserAudioProcessor::readSettings() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    RiserSettings s;
    s.lengthIdx = juce::roundToInt (v (ids::length));
    s.space = juce::roundToInt (v (ids::space));
    s.sourceBeats = v (ids::source);
    s.size = v (ids::size) * 0.01f;
    s.curve = v (ids::curve) * 0.01f;
    s.gainDb = v (ids::gain);
    s.sClean = v (ids::sClean) > 0.5f;
    s.phone = v (ids::phone) > 0.5f;
    s.chorus = v (ids::chorus) > 0.5f;
    s.tremolo = v (ids::tremolo) > 0.5f;
    s.sweep = v (ids::sweep) > 0.5f;
    s.wide = v (ids::wide) > 0.5f;
    s.phrase = juce::roundToInt (v (ids::phrase));
    s.phoneMix = v (ids::phoneMix) * 0.01f;   s.phoneDrive = v (ids::phoneDrive) * 0.01f;
    s.chorusMix = v (ids::chorusMix) * 0.01f; s.chorusDepth = v (ids::chorusDepth) * 0.01f;
    s.tremoloMix = v (ids::tremoloMix) * 0.01f; s.tremoloRate = juce::roundToInt (v (ids::tremoloRate));
    s.sweepMix = v (ids::sweepMix) * 0.01f;   s.sweepStart = v (ids::sweepStart);
    s.wideMix = v (ids::wideMix) * 0.01f;     s.wideWidth = v (ids::wideWidth) * 0.01f;
    s.raw = v (ids::raw) > 0.5f;
    return s;
}

static bool sameSettings (const RiserSettings& a, const RiserSettings& b)
{
    return a.lengthIdx == b.lengthIdx && a.space == b.space && a.sourceBeats == b.sourceBeats && a.size == b.size && a.curve == b.curve
        && a.gainDb == b.gainDb && a.sClean == b.sClean && a.phone == b.phone && a.chorus == b.chorus && a.tremolo == b.tremolo
        && a.sweep == b.sweep && a.wide == b.wide && a.phrase == b.phrase
        && a.phoneMix == b.phoneMix && a.phoneDrive == b.phoneDrive && a.chorusMix == b.chorusMix && a.chorusDepth == b.chorusDepth
        && a.tremoloMix == b.tremoloMix && a.tremoloRate == b.tremoloRate && a.sweepMix == b.sweepMix && a.sweepStart == b.sweepStart
        && a.wideMix == b.wideMix && a.wideWidth == b.wideWidth && a.raw == b.raw;
}

// thread de l'interface : reprend une prise terminée, relance le calcul quand un réglage change
void OniriqueRiserAudioProcessor::timerCallback()
{
    bool newTake = false;
    if (capPhase.load() == 3)
    {
        auto c = std::make_shared<RiserCapture>();
        const int len = recLen.load();
        c->audio.setSize (2, len);
        for (int ch = 0; ch < 2; ++ch) c->audio.copyFrom (ch, 0, rec, ch, 0, len);
        c->fs = rate; c->startSec = recStartSec; c->startPpq = recStartPpq; c->hasPpq = recHasPpq;
        c->bpm = recBpm; c->sigNum = recSigNum; c->sigDen = recSigDen;
        capPhase = 0;
        if (! RiserEngine::findPhrases (c->audio, c->fs).empty())   // une prise sans voix ne remplace pas la précédente
        {
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                take = c;
            }
            // nouvelle prise : première phrase
            if (auto* p = apvts.getParameter (ids::phrase)) p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
            newTake = true;
        }
    }

    const auto s = readSettings();
    if (newTake || ! haveSettings || ! sameSettings (s, lastSettings))
    {
        lastSettings = s;
        haveSettings = true;
        std::shared_ptr<const RiserCapture> t;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            t = take;
        }
        if (t != nullptr)
        {
            renderPending = true;
            renderThread->request (t, readSettings());
        }
    }
}

juce::File OniriqueRiserAudioProcessor::exportRiser (juce::String& error)
{
    const auto r = result();
    if (r == nullptr || r->riser.getNumSamples() == 0) { error = "Play your DAW over the vocal first: it is captured automatically."; return {}; }
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ONIRIQUE RISER");
    dir.createDirectory();
    juce::String name = "ONIRIQUE RISER - " + r->lengthLabel;
    if (r->hasGrid) name << " - bar " << r->dropBar << " beat " << r->dropBeat;
    auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr) { error = "Could not write the audio file."; return {}; }
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), r->fs, 2, 24, {}, 0));
    if (w == nullptr) { error = "Could not write the audio file."; return {}; }
    os.release();
    w->writeFromAudioSampleBuffer (r->riser, 0, r->riser.getNumSamples());
    return file;
}

juce::AudioProcessorEditor* OniriqueRiserAudioProcessor::createEditor()
{
    return new OniriqueRiserAudioProcessorEditor (*this);
}

void OniriqueRiserAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void OniriqueRiserAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.takeSnapshot();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueRiserAudioProcessor();
}

#if ONIRIQUE_UI_TEST
// essais locaux uniquement (jamais dans les versions publiées) : une prise de synthèse (deux phrases) posée à la mesure 9
void OniriqueRiserAudioProcessor::loadTestTake()
{
    auto c = std::make_shared<RiserCapture>();
    c->fs = 48000.0; c->bpm = 140.0; c->sigNum = 4; c->sigDen = 4;
    c->startSec = 32.0 * 60.0 / 140.0; c->startPpq = 32.0; c->hasPpq = true;
    const int n = (int) (c->fs * 6.0);
    c->audio.setSize (2, n);
    c->audio.clear();
    juce::Random rnd (3);
    auto phrase = [&] (double at, double dur, double f0)
    {
        const int a = (int) (at * c->fs), z = (int) ((at + dur) * c->fs);
        for (int i = a; i < z; ++i)
        {
            const double t = (i - a) / c->fs;
            double v = 0.0;
            for (int h = 1; h < 30; ++h) v += 0.08 / h * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * h * t * (1.0 + 0.01 * std::sin (5.0 * t)));
            const double env = std::min (1.0, std::min (t / 0.02, (dur - t) / 0.05)) * (0.6 + 0.4 * std::sin (t * 7.0));
            for (int ch = 0; ch < 2; ++ch) c->audio.setSample (ch, i, (float) (v * env));
        }
    };
    phrase (1.2, 1.6, 196.0);
    phrase (3.4, 1.8, 220.0);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        take = c;
    }
    haveSettings = false;   // relance le calcul au prochain tour du minuteur
}
#endif
