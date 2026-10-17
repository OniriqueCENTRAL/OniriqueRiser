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
    enum Zone { none = -1, zListen, zLoop, zEdit, zPrev, zNext, zWave };
    Zone zoneAt (juce::Point<float>) const;
    juce::Rectangle<float> waveArea() const, listenZone() const, loopZone() const, editZone() const, prevZone() const, nextZone() const;

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
};

// Effet en un clic (PHONE, CHORUS...) : pictogramme marche / arrêt + nom, braise quand il est allumé.
class FxToggle : public juce::ToggleButton, public Animated
{
public:
    explicit FxToggle (const juce::String& name) : juce::ToggleButton (name) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    float on = 0.0f, hov = 0.0f;
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
