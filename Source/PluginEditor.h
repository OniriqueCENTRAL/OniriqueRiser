#pragma once

#include "UIKit.h"
#include "PluginProcessor.h"

// Le panneau de la montée : titre, la montée (braise) qui monte jusqu'à la voix, l'état de la capture, où poser le
// fichier. On attrape la forme d'onde à la souris et on la glisse dans le DAW. En bas : LISTEN, LOOP, EDIT.
class RiserDisplay : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit RiserDisplay (OniriqueRiserAudioProcessor&);
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void (bool)> onEdit;   // EDIT : les réglages secondaires à la place du grand bouton
    bool isEditing() const noexcept { return editing; }
    void setEditing (bool e) { editing = e; repaint(); }

private:
    enum Zone { none = -1, zListen, zLoop, zEdit, zRaw, zPrev, zNext, zWave };
    Zone zoneAt (juce::Point<float>) const;
    juce::Rectangle<float> waveArea() const, listenZone() const, loopZone() const, editZone() const, rawZone() const, prevZone() const, nextZone() const;

    OniriqueRiserAudioProcessor& processor;
    std::shared_ptr<const onirique::RiserResult> shown;
    int shownState = -1;
    Zone hover = none;
    float pulse = 0.0f, appear = 0.0f, grab = 0.0f;
    double listen = -1.0, recSec = 0.0;
    bool rendering = false, dragging = false, editing = false, looping = false;
    juce::String note;   // message bref (rien à glisser...)
    int noteFrames = 0;
    int frames = 0;
public:
    bool frozen = false;   // essais locaux (visuel de démo) : aucune animation, chaque image est exacte
private:
};

// Effet en un clic (PHONE, CHORUS...) : le pictogramme marche / arrêt allume ou coupe l'effet ; un clic sur le NOM ouvre
// son menu de réglages (onText). Braise quand il est allumé.
class FxToggle : public juce::ToggleButton, public Animated
{
public:
    explicit FxToggle (const juce::String& name) : juce::ToggleButton (name) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onText;
    void setMenuOpen (bool o) { if (o != menuOpen) { menuOpen = o; repaint(); } }

private:
    float on = 0.0f, hov = 0.0f, iconRight = 0.0f;
    bool textClick = false, menuOpen = false;
};

// Curseur du menu d'un effet : NOM  valeur, au-dessus d'une piste braise et d'un bouton sombre (glisser, cliquer sur la
// piste, molette, double-clic = défaut).
class FxSlider : public juce::Component, public Animated
{
public:
    FxSlider (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& name);
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> track() const;
    void setFromX (float x);
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String name, shownText;
    float shown = -1.0f, glow = 0.0f;
    bool dragging = false;
};

// Menu d'un effet : une petite carte au-dessus de son nom, avec ses deux réglages (MIX + un réglage propre).
class FxMenu : public juce::Component, public Animated
{
public:
    explicit FxMenu (juce::AudioProcessorValueTreeState&);
    void showFor (int effect, juce::Rectangle<int> anchor);   // effect : 0 PHONE, 1 CHORUS, 2 TREMOLO, 3 SWEEP, 4 WIDE ; -1 = fermer
    int current() const noexcept { return effect; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;

private:
    std::array<std::unique_ptr<FxSlider>, 10> sliders;   // deux par effet
    int effect = -1;
    float shown = 0.0f;
};

class OniriqueRiserAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          public juce::DragAndDropContainer,
                                          private juce::Timer
{
public:
    explicit OniriqueRiserAudioProcessorEditor (OniriqueRiserAudioProcessor&);
    ~OniriqueRiserAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using Layout = KnobComponent::Layout;
    void timerCallback() override;
    float scale() const noexcept { return (float) getWidth() / onirique::ui::kEditorBaseW; }
    void showEdit (bool);
    void mouseDown (const juce::MouseEvent&) override;   // un clic ailleurs ferme le menu d'un effet

    OniriqueRiserAudioProcessor& processor;
    OniriqueLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    PresetBar presetBar { processor.presets };
    RiserDisplay display { processor };

    // le grand bouton : la durée de la montée
    juce::Slider lengthKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAtt;

    // une rangée d'effets en un clic
    FxToggle phone { "PHONE" }, chorus { "CHORUS" }, tremolo { "TREMOLO" }, sweep { "SWEEP" }, wide { "WIDE" };

    // EDIT : les réglages secondaires
    juce::Component editPanel;
    SegmentSelector spaceSel { processor.apvts, onirique::ids::space, { "ROOM", "PLATE", "DARK", "HALL", "SHIMMER" },
                               { "Short, tight room.", "Bright, dense plate: the classic vocal riser.", "Darker, wider plate.",
                                 "Big hall, long and soft.", "Hall with an octave above: an airy, shining rise." } };
    KnobComponent sourceKnob { processor.apvts, onirique::ids::source, "SOURCE", "1/4", "4", Layout::textBelow,
                               "How much of the phrase's start goes into the reverb (in beats).", lnf };
    KnobComponent sizeKnob   { processor.apvts, onirique::ids::size, "SIZE", "0", "100", Layout::textBelow,
                               "Length of the reverb tail compared to the riser: more = a longer, smoother swell.", lnf };
    KnobComponent curveKnob  { processor.apvts, onirique::ids::curve, "CURVE", "0", "100", Layout::textBelow,
                               "Shape of the rise: higher, it arrives later and hits harder.", lnf };
    KnobComponent gainKnob   { processor.apvts, onirique::ids::gain, "GAIN", "-24", "+6", Layout::textBelow,
                               "Level of the riser file (normalised to -1 dBFS first).", lnf, true };
    FxToggle sClean { "S-CLEAN" }, inSync { "IN SYNC" };
    FxMenu fxMenu { processor.apvts };   // réglages d'un effet (clic sur son nom)
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> attachments;
    float editAmt = 0.0f;

    std::vector<Animated*> animated;
    juce::Image logo, logoScaled;
    juce::Image backdropLayer;
    float layersScale = 0.0f;
    juce::Rectangle<float> logoRect, cardRect, knobLabel;
    juce::String shownLength;
    bool restoringSize = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueRiserAudioProcessorEditor)
};
