#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>

using namespace onirique;

namespace
{
constexpr float kBaseW = ui::kEditorBaseW, kBaseH = ui::kEditorBaseH;   // carré
constexpr int kMinW = 440, kMaxW = 1100;
constexpr float kPi = juce::MathConstants<float>::pi;

void iconPower (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float w)
{
    juce::Path p;
    const auto ctr = r.getCentre();
    const float rad = r.getWidth() * 0.36f;
    p.addCentredArc (ctr.x, ctr.y, rad, rad, 0.0f, 0.75f, 2.0f * kPi - 0.75f, true);
    p.startNewSubPath (ctr.x, ctr.y - rad * 1.25f);
    p.lineTo (ctr.x, ctr.y - rad * 0.15f);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace

//==============================================================================
RiserDisplay::RiserDisplay (OniriqueRiserAudioProcessor& p) : processor (p)
{
    setTooltip ("Play your DAW over the vocal: it is captured automatically, the riser is ready when you stop. "
                "Then grab the waveform and drop it in your DAW, at the bar shown.");
}

juce::Rectangle<float> RiserDisplay::waveArea() const
{
    const auto b = getLocalBounds().toFloat();
    return { b.getX() + b.getWidth() * 0.12f, b.getY() + b.getHeight() * 0.22f, b.getWidth() * 0.76f, b.getHeight() * 0.46f };
}

juce::Rectangle<float> RiserDisplay::listenZone() const
{
    const float u = ui::editorScale (*this);
    return { 16.0f * u, (float) getHeight() - 34.0f * u, 76.0f * u, 24.0f * u };
}
juce::Rectangle<float> RiserDisplay::loopZone() const { return listenZone().translated (listenZone().getWidth() + 6.0f * ui::editorScale (*this), 0.0f).withWidth (62.0f * ui::editorScale (*this)); }
juce::Rectangle<float> RiserDisplay::editZone() const
{
    const float u = ui::editorScale (*this);
    return { (float) getWidth() - 16.0f * u - 54.0f * u, (float) getHeight() - 34.0f * u, 54.0f * u, 24.0f * u };
}
// RAW : juste à gauche d'EDIT
juce::Rectangle<float> RiserDisplay::rawZone() const
{
    const float u = ui::editorScale (*this);
    return editZone().translated (-50.0f * u, 0.0f).withWidth (42.0f * u);
}
juce::Rectangle<float> RiserDisplay::prevZone() const
{
    const float u = ui::editorScale (*this);
    return { (float) getWidth() - 132.0f * u, 10.0f * u, 24.0f * u, 24.0f * u };
}
juce::Rectangle<float> RiserDisplay::nextZone() const
{
    const float u = ui::editorScale (*this);
    return { (float) getWidth() - 40.0f * u, 10.0f * u, 24.0f * u, 24.0f * u };
}

RiserDisplay::Zone RiserDisplay::zoneAt (juce::Point<float> p) const
{
    const bool many = shown != nullptr && shown->phraseCount > 1;
    if (listenZone().contains (p)) return zListen;
    if (loopZone().contains (p)) return zLoop;
    if (editZone().contains (p)) return zEdit;
    if (rawZone().contains (p)) return zRaw;
    if (many && prevZone().contains (p)) return zPrev;
    if (many && nextZone().contains (p)) return zNext;
    if (waveArea().expanded (8.0f).contains (p)) return zWave;
    return none;
}

void RiserDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
    const bool ready = shown != nullptr && shown->riserSamples > 0;
    setMouseCursor (z == zWave && ready ? juce::MouseCursor::DraggingHandCursor
                                       : (z != none && z != zWave ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
}

void RiserDisplay::mouseExit (const juce::MouseEvent&)
{
    if (! dragging) { hover = none; repaint(); }
}

void RiserDisplay::mouseDown (const juce::MouseEvent& e)
{
    auto setPhrase = [this] (int delta)
    {
        if (shown == nullptr) return;
        const int to = juce::jlimit (0, shown->phraseCount - 1, shown->phrase + delta);
        if (auto* p = processor.apvts.getParameter (ids::phrase))
        { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) to)); p->endChangeGesture(); }
    };
    switch (zoneAt (e.position))
    {
        case zListen: processor.setListen (! processor.isListening()); break;
        case zLoop:  looping = ! looping; processor.loop = looping; repaint(); break;
        case zEdit:   editing = ! editing; if (onEdit) onEdit (editing); repaint(); break;
        case zRaw:
            if (auto* p = processor.apvts.getParameter (ids::raw))
            { p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); repaint(); }
            break;
        case zPrev:   setPhrase (-1); break;
        case zNext:   setPhrase (1); break;
        case zWave: case none: default: break;
    }
}

// la forme d'onde se glisse directement dans le DAW
void RiserDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || zoneAt (e.mouseDownPosition) != zWave || e.getDistanceFromDragStart() < 5) return;
    juce::String err;
    const auto file = processor.exportRiser (err);
    if (! file.existsAsFile()) { note = err.isNotEmpty() ? "PLAY YOUR DAW OVER THE VOCAL FIRST" : "NOTHING TO DRAG YET"; noteFrames = 180; repaint(); return; }
    dragging = true;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [this] { dragging = false; });
}

void RiserDisplay::mouseUp (const juce::MouseEvent&) { dragging = false; }

void RiserDisplay::tick()
{
    auto r = processor.result();
    const int st = (int) processor.captureState();
    const bool rend = processor.isRendering();
    const double lp = processor.isListening() ? processor.listenPosition() : -1.0;
    const double rs = st == (int) OniriqueRiserAudioProcessor::Capture::recording ? processor.recordingSeconds() : 0.0;
    bool changed = r != shown || st != shownState || rend != rendering || std::abs (lp - listen) > 1.0e-4 || std::abs (rs - recSec) > 0.05;
    if (r != shown) appear = 0.0f;
    shown = r; shownState = st; rendering = rend; listen = lp; recSec = rs;
    changed |= ui::approach (appear, 1.0f, 0.10f);
    changed |= ui::approach (grab, hover == zWave || dragging ? 1.0f : 0.0f, 0.2f);
    if (noteFrames > 0 && --noteFrames == 0) { note.clear(); changed = true; }
    if (st != 0 || rend || shown == nullptr || shown->riserSamples == 0) { pulse += 1.0f / 60.0f; changed = true; }
    else if ((++frames & 1) == 0) { pulse += 1.0f / 30.0f; changed = true; }   // lueur animée de la montée : 30 i/s
    if (changed) repaint();
}

void RiserDisplay::paint (juce::Graphics& g)
{
    const float u = ui::editorScale (*this);
    const auto area = getLocalBounds().toFloat();
    const float rad = 14.0f * u;
    g.setGradientFill (juce::ColourGradient (ui::deep2.withAlpha (0.92f), 0.0f, 0.0f, ui::night, 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, rad);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), rad, std::max (1.0f, u));

    using Cap = OniriqueRiserAudioProcessor::Capture;
    const bool has = shown != nullptr && shown->riserSamples > 0;
    const juce::String sep (juce::CharPointer_UTF8 ("  \xc2\xb7  "));

    // ---- titre (le sigil étoilé + ONIRIQUE RISER)
    {
        const float s = 6.0f * u;
        const juce::Point<float> c (22.0f * u, 22.0f * u);
        juce::Path star;
        const float k = 0.18f * s;
        star.startNewSubPath (c.x, c.y - s); star.lineTo (c.x + k, c.y - k); star.lineTo (c.x + s, c.y); star.lineTo (c.x + k, c.y + k);
        star.lineTo (c.x, c.y + s); star.lineTo (c.x - k, c.y + k); star.lineTo (c.x - s, c.y); star.lineTo (c.x - k, c.y - k); star.closeSubPath();
        g.setColour (ui::ember);
        g.fillPath (star);
        auto f1 = ui::font (ui::Face::display, 13.0f * u, false, 0.22f), f2 = ui::font (ui::Face::display, 13.0f * u, true, 0.22f);
        const float w1 = ui::textWidth (f1, "ONIRIQUE") + 4.0f * u;
        g.setColour (ui::haze);
        g.setFont (f1);
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (34.0f * u, 10.0f * u, w1, 24.0f * u), juce::Justification::centredLeft, false);
        g.setColour (ui::bone);
        g.setFont (f2);
        g.drawText ("RISER", juce::Rectangle<float> (34.0f * u + w1, 10.0f * u, 80.0f * u, 24.0f * u), juce::Justification::centredLeft, false);
    }

    // ---- phrases (en haut à droite, s'il y en a plusieurs)
    if (has && shown->phraseCount > 1)
    {
        auto tri = [&] (juce::Rectangle<float> r, bool left, bool hl)
        {
            const auto q = r.withSizeKeepingCentre (r.getHeight() * 0.28f, r.getHeight() * 0.4f);
            juce::Path p;
            if (left) p.addTriangle (q.getRight(), q.getY(), q.getRight(), q.getBottom(), q.getX(), q.getCentreY());
            else      p.addTriangle (q.getX(), q.getY(), q.getX(), q.getBottom(), q.getRight(), q.getCentreY());
            g.setColour (hl ? ui::bone : ui::haze);
            g.fillPath (p);
        };
        tri (prevZone(), true, hover == zPrev);
        tri (nextZone(), false, hover == zNext);
        g.setColour (ui::mist);
        g.setFont (ui::font (ui::Face::mono, 11.0f * u, true, 0.06f));
        g.drawText ("PHRASE " + juce::String (shown->phrase + 1) + "/" + juce::String (shown->phraseCount),
                    juce::Rectangle<float> (prevZone().getRight(), prevZone().getY(), nextZone().getX() - prevZone().getRight(), prevZone().getHeight()),
                    juce::Justification::centred, false);
    }

    // ---- la montée : des traits verticaux qui grandissent jusqu'à la voix
    const auto wa = waveArea();
    const float mid = wa.getCentreY(), half = wa.getHeight() * 0.5f;
    const int bars = 72;
    const float step = wa.getWidth() / (float) bars, bw = std::max (1.0f, step * 0.38f);
    if (has)
    {
        float pk = 1.0e-6f;
        for (float v : shown->riserEnv) pk = std::max (pk, v);
        const int N = (int) shown->riserEnv.size();
        for (int b = 0; b < bars; ++b)
        {
            const int i0 = b * N / bars, i1 = juce::jmax (i0 + 1, (b + 1) * N / bars);
            float m = 0.0f;
            for (int i = i0; i < i1 && i < N; ++i) m = std::max (m, shown->riserEnv[(size_t) i]);
            const float t = (float) b / (float) (bars - 1);
            const float h = std::max (1.5f * u, juce::jmin (1.0f, m / pk) * half * appear);
            const float x = wa.getX() + step * ((float) b + 0.5f);
            const bool played = listen >= 0.0 && listen >= t;
            auto col = ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, 0.25f + 0.75f * t).interpolatedWith (ui::bone, 0.25f * grab * t);
            if (played) col = ui::bone;
            // lueur animée : une vague de lumière qui remonte vers la voix (plus vive quand on attrape la montée)
            const float wave = 0.5f + 0.5f * std::sin (t * 9.0f - pulse * 2.2f);
            const float glowA = (0.10f + 0.22f * grab) * t * (0.35f + 0.65f * wave * wave);
            if (glowA > 0.01f)
            {
                g.setColour (ui::ember.withAlpha (glowA));
                g.fillRoundedRectangle (juce::Rectangle<float> (x - bw * 1.6f, mid - h - 2.0f * u, bw * 3.2f, 2.0f * h + 4.0f * u), bw * 1.6f);
            }
            g.setColour (col);
            g.fillRoundedRectangle (juce::Rectangle<float> (x - bw * 0.5f, mid - h, bw, 2.0f * h), bw * 0.5f);
        }
        // la voix commence ici
        g.setColour (ui::bone.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (wa.getRight() + 3.0f * u, wa.getY() + wa.getHeight() * 0.1f, 1.2f * u, wa.getHeight() * 0.8f));
        g.setFont (ui::font (ui::Face::mono, 8.5f * u, true, 0.12f));
        g.drawText ("VOICE", juce::Rectangle<float> (wa.getRight() + 7.0f * u, wa.getY() + wa.getHeight() * 0.1f, 40.0f * u, 10.0f * u), juce::Justification::centredLeft, false);
    }
    else
    {
        // en attente : un fil qui respire (plus vif pendant la capture)
        const bool rec = shownState == (int) Cap::recording;
        juce::Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float t = (float) i / 120.0f, x = wa.getX() + t * wa.getWidth();
            const float a = (0.03f + 0.10f * std::pow (t, 2.5f)) * half * (rec ? 2.5f : 1.0f);
            const float y = mid + a * std::sin (t * 46.0f - pulse * 4.0f);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        ui::strokeGlow (g, p, ui::ember, 1.3f * u, 0.5f);
        g.setColour (ui::ember.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.3f * u));
    }

    // ---- ligne d'état : capture, calcul, où poser le fichier
    {
        juce::String t;
        juce::Colour c = ui::haze;
        const float beat = 0.5f + 0.5f * std::sin (pulse * 6.0f);
        if (note.isNotEmpty()) { t = note; c = ui::ember; }
        else if (shownState == (int) Cap::recording) { t = "CAPTURING THE VOICE" + sep + juce::String (recSec, 1) + " s"; c = ui::ember.withAlpha (0.55f + 0.45f * beat); }
        else if (shownState == (int) Cap::listening) t = "LISTENING" + sep + "WAITING FOR THE VOICE";
        else if (rendering) t = "BUILDING THE RISER";
        else if (has) { t = shown->hasGrid ? "DRAG IT TO BAR " + juce::String (shown->dropBar) + sep + "BEAT " + juce::String (shown->dropBeat) : "DRAG IT: IT ENDS ON THE VOICE"; c = grab > 0.5f ? ui::bone : ui::mist; }
        else t = "PRESS PLAY IN YOUR DAW";
        g.setColour (c);
        g.setFont (ui::fitFont (ui::Face::mono, 10.5f * u, area.getWidth() * 0.6f, t, true, 0.1f));
        g.drawText (t, juce::Rectangle<float> (area.getX(), wa.getBottom() + 6.0f * u, area.getWidth(), 16.0f * u), juce::Justification::centred, false);
    }

    // ---- LISTEN, LOOP (à gauche), EDIT (à droite)
    auto textButton = [&] (juce::Rectangle<float> r, const juce::String& txt, bool on, bool hl, int icon)
    {
        const auto col = on ? ui::ember : (hl ? ui::bone : ui::haze);
        const float ic = r.getHeight() * 0.42f;
        const auto ir = juce::Rectangle<float> (ic, ic).withCentre ({ r.getX() + ic * 0.7f, r.getCentreY() });
        juce::Path p;
        if (icon == 0) p.addTriangle (ir.getX(), ir.getY(), ir.getX(), ir.getBottom(), ir.getRight(), ir.getCentreY());   // lecture
        else if (icon == 1)
        {
            p.addCentredArc (ir.getCentreX(), ir.getCentreY(), ic * 0.5f, ic * 0.42f, 0.0f, 0.6f, 2.0f * kPi - 0.3f, true);   // boucle
            g.setColour (col);
            g.strokePath (p, juce::PathStrokeType (1.3f * u));
            p.clear();
        }
        if (! p.isEmpty()) { g.setColour (col); g.fillPath (p); }
        g.setColour (col);
        g.setFont (ui::font (ui::Face::mono, 10.0f * u, true, 0.14f));
        g.drawText (txt, r.withTrimmedLeft (icon >= 0 ? ic * 1.6f : 0.0f), icon >= 0 ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    };
    textButton (listenZone(), "LISTEN", listen >= 0.0, hover == zListen, 0);
    textButton (loopZone(), "LOOP", looping, hover == zLoop, 1);
    textButton (editZone(), "EDIT", editing, hover == zEdit, -1);
    textButton (rawZone(), "RAW", processor.apvts.getRawParameterValue (ids::raw)->load() > 0.5f, hover == zRaw, -1);
}

//==============================================================================
void FxToggle::tick()
{
    bool changed = ui::approach (on, getToggleState() ? 1.0f : 0.0f, 0.2f);
    changed |= ui::approach (hov, isOver() ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void FxToggle::paintButton (juce::Graphics& g, bool, bool)
{
    const auto b = getLocalBounds().toFloat();
    const float h = b.getHeight(), u = h / 26.0f;
    const auto col = ui::haze.interpolatedWith (ui::bone, 0.6f * hov).interpolatedWith (ui::ember, on);
    const float ic = h * 0.55f;
    const auto f = ui::font (ui::Face::mono, h * 0.42f, true, 0.12f);
    const float tw = ui::textWidth (f, getButtonText());
    const float total = ic + 5.0f * u + tw;
    const float x0 = b.getCentreX() - total * 0.5f;
    // allumé : une lueur douce sur le pictogramme, dans les limites du bouton
    const auto icR = juce::Rectangle<float> (ic, ic).withCentre ({ x0 + ic * 0.5f, b.getCentreY() });
    if (on > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.30f * on), icR.getCentreX(), icR.getCentreY(),
                                                 ui::ember.withAlpha (0.0f), icR.getCentreX() + h * 0.5f, icR.getCentreY(), true));
        g.fillEllipse (icR.withSizeKeepingCentre (h * 0.98f, h * 0.98f));
    }
    iconRight = icR.getRight() + 3.0f * u;   // à droite : le nom (ouvre le menu)
    iconPower (g, icR, col, std::max (1.2f, 1.6f * u));
    g.setColour (col);
    g.setFont (f);
    g.drawText (getButtonText(), juce::Rectangle<float> (x0 + ic + 5.0f * u, b.getY(), tw + 4.0f, h), juce::Justification::centredLeft, false);
    if (menuOpen)   // menu ouvert : le nom est souligné
        g.fillRoundedRectangle (juce::Rectangle<float> (x0 + ic + 5.0f * u, b.getCentreY() + h * 0.30f, tw, std::max (1.0f, 1.4f * u)), 0.7f * u);
}

// clic sur le nom : le menu de l'effet (le pictogramme, lui, allume / coupe)
void FxToggle::mouseDown (const juce::MouseEvent& e)
{
    textClick = onText != nullptr && e.position.x > iconRight;
    if (! textClick) juce::ToggleButton::mouseDown (e);
}

void FxToggle::mouseDrag (const juce::MouseEvent& e)
{
    if (! textClick) juce::ToggleButton::mouseDrag (e);
}

void FxToggle::mouseUp (const juce::MouseEvent& e)
{
    if (textClick)
    {
        textClick = false;
        if (onText != nullptr && getLocalBounds().contains (e.getPosition())) onText();
        return;
    }
    juce::ToggleButton::mouseUp (e);
}

//==============================================================================
FxSlider::FxSlider (juce::AudioProcessorValueTreeState& s, const char* paramId, const juce::String& n)
    : param (s.getParameter (paramId)), name (n)
{
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); }, nullptr);
        attachment->sendInitialUpdate();
    }
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

juce::Rectangle<float> FxSlider::track() const
{
    const auto b = getLocalBounds().toFloat();
    const float h = b.getHeight();
    return { b.getX() + h * 0.22f, b.getY() + h * 0.68f, b.getWidth() - h * 0.44f, 0.0f };
}

void FxSlider::setFromX (float x)
{
    const auto t = track();
    const float norm = juce::jlimit (0.0f, 1.0f, (x - t.getX()) / std::max (1.0f, t.getWidth()));
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (norm));
}

void FxSlider::mouseDown (const juce::MouseEvent& e)
{
    if (param == nullptr) return;
    dragging = true;
    attachment->beginGesture();
    setFromX (e.position.x);
}

void FxSlider::mouseDrag (const juce::MouseEvent& e) { if (param != nullptr && dragging) setFromX (e.position.x); }

void FxSlider::mouseUp (const juce::MouseEvent&)
{
    if (param == nullptr || ! dragging) return;
    dragging = false;
    attachment->endGesture();
}

void FxSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr) attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void FxSlider::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const int steps = param->getNumSteps();
    const float stepN = steps > 1 && steps < 64 ? 1.0f / (float) (steps - 1) : 0.03f;
    const float dir = w.deltaY > 0.0f ? 1.0f : (w.deltaY < 0.0f ? -1.0f : 0.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, param->getValue() + dir * stepN);
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (norm));
}

void FxSlider::tick()
{
    if (param == nullptr) return;
    bool changed = ui::approach (shown, param->getValue(), 0.35f);
    changed |= ui::approach (glow, dragging || isMouseOver() ? 1.0f : 0.0f, 0.2f);
    const auto t = param->getCurrentValueAsText();
    if (t != shownText) { shownText = t; changed = true; }
    if (changed) repaint();
}

void FxSlider::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const auto b = getLocalBounds().toFloat();
    const float h = b.getHeight(), u = h / 56.0f;
    // NOM  valeur
    const auto f1 = ui::font (ui::Face::display, 13.0f * u, true, 0.12f), f2 = ui::font (ui::Face::display, 13.0f * u, false, 0.12f);
    const juce::String v = shownText.toUpperCase();
    const float w1 = ui::textWidth (f1, name) + 8.0f * u, w2 = ui::textWidth (f2, v) + 4.0f;
    const float x0 = b.getCentreX() - (w1 + w2) * 0.5f;
    g.setFont (f1);
    g.setColour (ui::mist);
    g.drawText (name, juce::Rectangle<float> (x0, b.getY(), w1, h * 0.42f), juce::Justification::centredLeft, false);
    g.setFont (f2);
    g.setColour (ui::haze);
    g.drawText (v, juce::Rectangle<float> (x0 + w1, b.getY(), w2, h * 0.42f), juce::Justification::centredLeft, false);

    // piste : fond sombre, remplissage braise jusqu'au bouton
    const auto t = track();
    const float th = 4.0f * u, x = t.getX() + juce::jlimit (0.0f, 1.0f, shown) * t.getWidth();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (juce::Rectangle<float> (t.getX(), t.getY() - th * 0.5f, t.getWidth(), th), th * 0.5f);
    juce::Path fill;
    fill.addRoundedRectangle (juce::Rectangle<float> (t.getX(), t.getY() - th * 0.5f, std::max (th, x - t.getX()), th), th * 0.5f);
    ui::strokeGlow (g, fill, ui::ember, 1.0f * u, 0.5f + 0.4f * glow);
    g.setColour (ui::ember);
    g.fillPath (fill);
    // bouton : disque sombre, liseré clair
    const float r = 10.0f * u;
    const auto knob = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre ({ x, t.getY() });
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (knob.translated (0.0f, 2.0f * u).expanded (1.5f * u));
    g.setGradientFill (juce::ColourGradient (ui::deep2.brighter (0.35f), knob.getX(), knob.getY(), ui::night, knob.getRight(), knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (ui::line.interpolatedWith (ui::bone, 0.25f + 0.35f * glow));
    g.drawEllipse (knob.reduced (0.5f), std::max (1.0f, 1.2f * u));
}

//==============================================================================
FxMenu::FxMenu (juce::AudioProcessorValueTreeState& s)
{
    const char* fxIds[10] { ids::phoneMix, ids::phoneDrive, ids::chorusMix, ids::chorusDepth, ids::tremoloMix, ids::tremoloRate,
                            ids::sweepMix, ids::sweepStart, ids::wideMix, ids::wideWidth };
    const char* names[10] { "MIX", "DRIVE", "MIX", "DEPTH", "MIX", "TIMING", "MIX", "START", "MIX", "WIDTH" };
    for (int i = 0; i < 10; ++i)
    {
        sliders[(size_t) i] = std::make_unique<FxSlider> (s, fxIds[i], names[i]);
        addChildComponent (*sliders[(size_t) i]);
    }
    setAlpha (0.0f);
    setVisible (false);
}

void FxMenu::showFor (int e, juce::Rectangle<int> anchor)
{
    effect = e;
    if (effect < 0) return;   // la fermeture se fait en fondu (tick)
    for (int i = 0; i < 10; ++i) sliders[(size_t) i]->setVisible (i / 2 == effect);
    auto* parent = getParentComponent();
    const float u = ui::editorScale (*this);
    const float w = 230.0f * u, h = 150.0f * u;
    auto r = juce::Rectangle<float> (w, h).withCentre ({ (float) anchor.getCentreX(), (float) anchor.getY() - 8.0f * u - h * 0.5f });
    if (parent != nullptr) r.setX (juce::jlimit (8.0f * u, (float) parent->getWidth() - w - 8.0f * u, r.getX()));
    setBounds (r.toNearestInt());
    setVisible (true);
    toFront (false);
    resized();
}

void FxMenu::resized()
{
    const float u = ui::editorScale (*this);
    auto r = getLocalBounds().toFloat().reduced (16.0f * u, 12.0f * u);
    const float sh = r.getHeight() * 0.5f;
    for (int i = 0; i < 10; ++i)
        sliders[(size_t) i]->setBounds (juce::Rectangle<float> (r.getX(), r.getY() + (float) (i % 2) * sh, r.getWidth(), sh).toNearestInt());
}

void FxMenu::tick()
{
    if (ui::approach (shown, effect >= 0 ? 1.0f : 0.0f, 0.25f))
    {
        setAlpha (shown);
        if (shown <= 0.0f) setVisible (false);
    }
    if (! isVisible()) return;
    for (auto& s : sliders) if (s->isVisible()) s->tick();
}

void FxMenu::paint (juce::Graphics& g)
{
    const float u = ui::editorScale (*this);
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float rad = 12.0f * u;
    g.setColour (ui::night);   // carte opaque : rien ne transparaît dessous
    g.fillRoundedRectangle (b, rad);
    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, b.getY(), ui::night, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, rad);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.3f));
    g.drawRoundedRectangle (b.reduced (0.5f), rad, std::max (1.0f, u));
}

//==============================================================================
OniriqueRiserAudioProcessorEditor::OniriqueRiserAudioProcessorEditor (OniriqueRiserAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    tooltipWindow.setLookAndFeel (&lnf);
    setLookAndFeel (&lnf);
    setOpaque (true);
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (display);
    display.onEdit = [this] (bool e) { showEdit (e); };
    animated.push_back (&presetBar);
    animated.push_back (&display);

    // le grand bouton : REVERSE + durée
    lengthKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    lengthKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    lengthKnob.setRotaryParameters (1.25f * kPi, 2.75f * kPi, true);
    lengthKnob.setMouseDragSensitivity (180);
    lengthKnob.setTooltip ("Riser length, at your DAW's tempo: it always ends exactly where the voice starts.");
    lengthKnob.getProperties().set ("shown", 0.0f);   // à l'ouverture, la couronne se déploie jusqu'à la valeur
    lengthKnob.getProperties().set ("glow", 0.0f);
    addAndMakeVisible (lengthKnob);
    lengthAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, ids::length, lengthKnob);

    struct T { FxToggle& b; const char* id; const char* tip; };
    for (auto& t : { T { phone, ids::phone, "PHONE: telephone-band riser, slightly gritty." },
                     T { chorus, ids::chorus, "CHORUS: a wide, shimmering riser." },
                     T { tremolo, ids::tremolo, "TREMOLO: the riser pulses in 16th notes at your DAW's tempo." },
                     T { sweep, ids::sweep, "SWEEP: a filter opens as the riser climbs, from muffled to bright." },
                     T { wide, ids::wide, "WIDE: a wider stereo riser." },
                     T { sClean, ids::sClean, "S-CLEAN: the S and sharp consonants are removed before the reverb, so they never echo backwards." },
                     T { inSync, ids::inSync, "IN SYNC: the plugin plays the riser by itself, at the right moment, while your DAW plays. "
                                              "Turn it off once the file is in your session." } })
    {
        t.b.setTooltip (t.tip);
        attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, t.id, t.b));
        animated.push_back (&t.b);
    }
    for (auto* b : { &phone, &chorus, &tremolo, &sweep, &wide }) addAndMakeVisible (b);

    // clic sur le nom d'un effet : son menu (MIX + un réglage)
    {
        FxToggle* fx[] { &phone, &chorus, &tremolo, &sweep, &wide };
        for (int i = 0; i < 5; ++i)
            fx[i]->onText = [this, i, fx]
            {
                const bool closing = fxMenu.current() == i;
                fxMenu.showFor (closing ? -1 : i, fx[i]->getBounds());
                for (int k = 0; k < 5; ++k) fx[k]->setMenuOpen (! closing && k == i);
            };
    }

    // EDIT : réglages secondaires, à la place du grand bouton
    addChildComponent (editPanel);
    editPanel.setInterceptsMouseClicks (false, true);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &spaceSel, &sourceKnob, &sizeKnob, &curveKnob, &gainKnob, &sClean, &inSync })
        editPanel.addAndMakeVisible (c);
    for (Animated* a : std::initializer_list<Animated*> { &spaceSel, &sourceKnob, &sizeKnob, &curveKnob, &gainKnob })
        animated.push_back (a);
    spaceSel.setTitle ("SPACE");

    const int w = onirique::window::openingSize (processor.apvts.state, "ONIRIQUE RISER", { 560, 560 }, { kMinW, kMinW }, { kMaxW, kMaxW }).x;
    setResizable (true, true);
    setResizeLimits (kMinW, kMinW, kMaxW, kMaxW);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) (kBaseW / kBaseH));
    setSize (w, juce::roundToInt ((float) w * kBaseH / kBaseW));
    restoringSize = false;
    onirique::window::remember (processor.apvts.state, getWidth(), getHeight());

    resized();
   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement (jamais dans les versions publiées)
    {
        const auto t = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {});
        if (t.contains ("demo")) processor.loadTestTake();
        if (t.contains ("edit")) { display.setEditing (true); showEdit (true); }
        if (t.contains ("fxmenu")) juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<OniriqueRiserAudioProcessorEditor> (this)] { if (sp != nullptr && sp->tremolo.onText) sp->tremolo.onText(); });
    }
   #endif
    addChildComponent (fxMenu);   // au-dessus de tout
    animated.push_back (&fxMenu);
    addMouseListener (this, true);   // un clic ailleurs ferme le menu d'un effet
    startTimerHz (60);
}

OniriqueRiserAudioProcessorEditor::~OniriqueRiserAudioProcessorEditor()
{
    stopTimer();
    removeMouseListener (this);
    onirique::window::rememberOnThisComputer ("ONIRIQUE RISER", getWidth(), getHeight());
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void OniriqueRiserAudioProcessorEditor::showEdit (bool e)
{
    if (e) editPanel.setVisible (true);
    else lengthKnob.setVisible (true);
}

void OniriqueRiserAudioProcessorEditor::timerCallback()
{
    for (auto* a : animated) a->tick();

    // grand bouton : l'aiguille et la couronne suivent la valeur (le dessin du potard lit "shown")
    {
        const float pos = (float) lengthKnob.valueToProportionOfLength (lengthKnob.getValue());
        float shown = (float) lengthKnob.getProperties().getWithDefault ("shown", 0.0f);
        if (ui::approach (shown, pos, 0.3f)) { lengthKnob.getProperties().set ("shown", shown); lengthKnob.repaint(); }
    }

    // EDIT : le grand bouton laisse la place aux réglages secondaires (fondu)
    if (ui::approach (editAmt, display.isEditing() ? 1.0f : 0.0f, 0.18f))
    {
        editPanel.setAlpha (editAmt);
        lengthKnob.setAlpha (1.0f - editAmt);
        if (editAmt <= 0.0f) editPanel.setVisible (false);
        if (editAmt >= 1.0f) lengthKnob.setVisible (false);
        repaint (knobLabel.getSmallestIntegerContainer());
    }
    if (const auto t = lengthKnob.getTextFromValue (lengthKnob.getValue()).toUpperCase(); t != shownLength)
    {
        shownLength = t;
        repaint (knobLabel.getSmallestIntegerContainer());
    }
}

void OniriqueRiserAudioProcessorEditor::resized()
{
    backdropLayer = {};
    const float u = scale();
    const float W = (float) getWidth();

    // ---- en-tête : logo, presets
    logoRect = { 16.0f * u, 12.0f * u, 30.0f * u * 985.0f / 895.0f, 30.0f * u };
    presetBar.setBounds (juce::Rectangle<float> (250.0f * u, 34.0f * u).withCentre ({ W * 0.5f, 27.0f * u }).toNearestInt());

    // ---- la carte : le panneau de la montée, le grand bouton, les effets
    cardRect = { 12.0f * u, 54.0f * u, W - 24.0f * u, (float) getHeight() - 66.0f * u };
    display.setBounds (juce::Rectangle<float> (cardRect.getX() + 10.0f * u, cardRect.getY() + 10.0f * u, cardRect.getWidth() - 20.0f * u, 200.0f * u).toNearestInt());
    const float top = (float) display.getBottom() + 8.0f * u;
    lengthKnob.setBounds (juce::Rectangle<float> (196.0f * u, 196.0f * u).withCentre ({ W * 0.5f, top + 104.0f * u }).toNearestInt());
    knobLabel = { 0.0f, top + 200.0f * u, W, 24.0f * u };

    const float fxY = cardRect.getBottom() - 44.0f * u, fxW = 92.0f * u;
    FxToggle* row[] { &phone, &chorus, &tremolo, &sweep, &wide };
    for (int i = 0; i < 5; ++i)
        row[i]->setBounds (juce::Rectangle<float> (fxW, 26.0f * u).withCentre ({ W * 0.5f + ((float) i - 2.0f) * (fxW + 4.0f * u), fxY + 13.0f * u }).toNearestInt());

    // EDIT : à la place du grand bouton
    editPanel.setBounds (juce::Rectangle<float> (cardRect.getX() + 10.0f * u, top, cardRect.getWidth() - 20.0f * u, fxY - top - 8.0f * u).toNearestInt());
    {
        const float pw = (float) editPanel.getWidth();
        spaceSel.setBounds (juce::Rectangle<float> (pw - 20.0f * u, 32.0f * u).withCentre ({ pw * 0.5f, 26.0f * u }).toNearestInt());
        const float kw = (pw - 20.0f * u) / 4.0f;
        KnobComponent* ks[] { &sourceKnob, &sizeKnob, &curveKnob, &gainKnob };
        for (int i = 0; i < 4; ++i)
        {
            ks[i]->setMaxDiameter (60.0f * u);
            ks[i]->setBounds (juce::Rectangle<float> (10.0f * u + kw * (float) i, 52.0f * u, kw, 118.0f * u).toNearestInt());
        }
        sClean.setBounds (juce::Rectangle<float> (110.0f * u, 26.0f * u).withCentre ({ pw * 0.5f - 66.0f * u, 190.0f * u }).toNearestInt());
        inSync.setBounds (juce::Rectangle<float> (110.0f * u, 26.0f * u).withCentre ({ pw * 0.5f + 66.0f * u, 190.0f * u }).toNearestInt());
    }

    if (logo.isValid())
    {
        const int lh = juce::jlimit (16, 400, juce::roundToInt (logoRect.getHeight() * 2.0f));
        logoScaled = logo.rescaled (juce::roundToInt ((float) lh * 985.0f / 895.0f), lh, juce::Graphics::highResamplingQuality);
    }
    if (! restoringSize)
        onirique::window::remember (processor.apvts.state, getWidth(), getHeight());
}

void OniriqueRiserAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float u = scale();
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! backdropLayer.isValid() || std::abs (ps - layersScale) > 1.0e-3f)
    {
        // fond + carte : une image prête (redessinée seulement à la taille)
        layersScale = ps;
        backdropLayer = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * ps)),
                                     juce::jmax (1, juce::roundToInt ((float) getHeight() * ps)), false);
        juce::Graphics bg (backdropLayer);
        bg.addTransform (juce::AffineTransform::scale (ps));
        const float W = (float) getWidth(), H = (float) getHeight();
        bg.fillAll (ui::night);
        bg.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.75f), 0.0f, 0.0f, ui::night.withAlpha (0.0f), 0.0f, H * 0.55f, false));
        bg.fillRect (getLocalBounds());
        bg.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.16f), W * 0.8f, -30.0f * u, ui::ember.withAlpha (0.0f), W * 1.2f, -30.0f * u, true));
        bg.fillRect (getLocalBounds());
        juce::Path card;
        card.addRoundedRectangle (cardRect, 18.0f * u);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), juce::roundToInt (20.0f * u), { 0, juce::roundToInt (8.0f * u) }).drawForPath (bg, card);
        bg.setGradientFill (juce::ColourGradient (ui::deep2.withAlpha (0.96f), 0.0f, cardRect.getY(), ui::deep.withAlpha (0.98f), 0.0f, cardRect.getBottom(), false));
        bg.fillPath (card);
        bg.setColour (ui::line);
        bg.strokePath (card, juce::PathStrokeType (std::max (1.0f, u)));
        // petits points de grille dans les coins de la carte
        for (int cx = 0; cx < 2; ++cx)
            for (int cy = 0; cy < 2; ++cy)
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3 - i; ++j)
                    {
                        const float x = cx == 0 ? cardRect.getX() + (12.0f + 7.0f * (float) j) * u : cardRect.getRight() - (12.0f + 7.0f * (float) j) * u;
                        const float y = cy == 0 ? (float) display.getBottom() + (14.0f + 7.0f * (float) i) * u : cardRect.getBottom() - (12.0f + 7.0f * (float) i) * u;
                        bg.setColour (ui::line.withMultipliedAlpha (1.4f));
                        bg.fillEllipse (juce::Rectangle<float> (2.4f * u, 2.4f * u).withCentre ({ x, y }));
                    }
        if (logoScaled.isValid()) bg.drawImage (logoScaled, logoRect, juce::RectanglePlacement::stretchToFit);
    }
    g.setOpacity (1.0f);
    g.drawImageTransformed (backdropLayer, juce::AffineTransform::scale (1.0f / layersScale));

    // REVERSE  1 BAR (sous le grand bouton)
    if (editAmt < 1.0f)
    {
        const auto f1 = ui::font (ui::Face::display, 15.0f * u, true, 0.14f), f2 = ui::font (ui::Face::display, 15.0f * u, false, 0.14f);
        const float w1 = ui::textWidth (f1, "REVERSE") + 8.0f * u, w2 = ui::textWidth (f2, shownLength) + 4.0f;
        const float x0 = knobLabel.getCentreX() - (w1 + w2) * 0.5f;
        g.setOpacity (1.0f - editAmt);
        g.setFont (f1);
        g.setColour (ui::mist.withMultipliedAlpha (1.0f - editAmt));
        g.drawText ("REVERSE", juce::Rectangle<float> (x0, knobLabel.getY(), w1, knobLabel.getHeight()), juce::Justification::centredLeft, false);
        g.setFont (f2);
        g.setColour (ui::haze.withMultipliedAlpha (1.0f - editAmt));
        g.drawText (shownLength, juce::Rectangle<float> (x0 + w1, knobLabel.getY(), w2, knobLabel.getHeight()), juce::Justification::centredLeft, false);
    }
}

// un clic hors du menu d'un effet (et hors des noms d'effets, qui le gèrent eux-mêmes) le ferme
void OniriqueRiserAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (fxMenu.current() < 0) return;
    for (auto* c = e.eventComponent; c != nullptr; c = c->getParentComponent())
        if (c == &fxMenu || dynamic_cast<FxToggle*> (c) != nullptr) return;
    fxMenu.showFor (-1, {});
    for (auto* b : { &phone, &chorus, &tremolo, &sweep, &wide }) b->setMenuOpen (false);
}
