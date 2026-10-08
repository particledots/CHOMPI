#include "PluginEditor.h"

namespace
{
const juce::Colour kBg      (0xff17181b);
const juce::Colour kPanel   (0xff212327);
const juce::Colour kEdge    (0xff34373d);
const juce::Colour kAccent  (0xffe8a33d);
const juce::Colour kText    (0xffd3d6db);
const juce::Colour kDim     (0xff7f858f);
const juce::Colour kTrack   (0xff3a3d44);

juce::Font makeFont(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace

// ---------------------------------------------------------------------------------------------
IpmohcLookAndFeel::IpmohcLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, kText);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, kAccent.withAlpha(0.35f));
    setColour(juce::Label::textColourId, kText);
    setColour(juce::TextEditor::backgroundColourId, kBg);
    setColour(juce::TextEditor::textColourId, kText);
    setColour(juce::TextEditor::outlineColourId, kAccent);
    setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
    setColour(juce::CaretComponent::caretColourId, kAccent);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2f35));
    setColour(juce::TextButton::buttonOnColourId, kAccent);
    setColour(juce::TextButton::textColourOffId, kText);
    setColour(juce::TextButton::textColourOnId, kBg);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2d2f35));
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::outlineColourId, kEdge);
    setColour(juce::ComboBox::arrowColourId, kDim);
    setColour(juce::PopupMenu::backgroundColourId, kPanel);
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour(juce::PopupMenu::highlightedTextColourId, kBg);
}

juce::Font IpmohcLookAndFeel::getTextButtonFont(juce::TextButton&, int h) { return makeFont(juce::jmax(9.0f, h * 0.42f), true); }
juce::Font IpmohcLookAndFeel::getComboBoxFont(juce::ComboBox& c) { return makeFont(juce::jmax(9.0f, c.getHeight() * 0.46f)); }

juce::Font IpmohcLookAndFeel::getLabelFont(juce::Label& l)
{
    return makeFont(l.getProperties().getWithDefault("fontHeight", 12.0f));
}

void IpmohcLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
    const float dia = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float radius = dia * 0.5f;
    const float track = juce::jmax(2.5f, radius * 0.11f);
    const float arcR = radius - track * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) s.getProperties().getWithDefault("bipolar", false);
    const bool enabled = s.isEnabled();
    const auto accent = enabled ? kAccent : kDim;

    // knob body
    const float bodyR = radius - track * 1.9f;
    g.setColour(juce::Colour(0xff2d2f35));
    g.fillEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour(juce::Colour(0xff0f1012));
    g.drawEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.2f);

    // track
    juce::Path trackPath;
    trackPath.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour(kTrack);
    g.strokePath(trackPath, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (from the left end, or from centre for bipolar controls)
    const float from = bipolar ? (startAngle + 0.5f * (endAngle - startAngle)) : startAngle;
    if (std::abs(angle - from) > 0.002f)
    {
        juce::Path v;
        v.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(accent);
        g.strokePath(v, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // pointer
    juce::Path p;
    const float pl = bodyR * 0.72f;
    p.addRoundedRectangle(-1.4f, -bodyR + 3.0f, 2.8f, pl * 0.62f, 1.2f);
    g.setColour(enabled ? kText : kDim);
    g.fillPath(p, juce::AffineTransform::rotation(angle).translated(c.x, c.y));
}


// ---------------------------------------------------------------------------------------------
static juce::String noteName(int n)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[n % 12]) + juce::String(n / 12 - 2); // Logic's naming: MIDI 60 = C3
}

void StepGrid::refresh()
{
    SeqStep fresh[StepSequencer::kMaxSteps];
    int len = 0, playing = -1;
    seq_.snapshot(fresh, len, playing);
    bool changed = (len != len_) || (playing != playing_);
    for (int i = 0; i < StepSequencer::kMaxSteps && !changed; ++i)
        changed = fresh[i].note != steps_[i].note || fresh[i].rest != steps_[i].rest || fresh[i].prob != steps_[i].prob
                  || fresh[i].ratchet != steps_[i].ratchet || fresh[i].gate != steps_[i].gate || fresh[i].accent != steps_[i].accent
                  || fresh[i].octChance != steps_[i].octChance || fresh[i].condA != steps_[i].condA || fresh[i].condB != steps_[i].condB;
    if (!changed) return;
    for (int i = 0; i < StepSequencer::kMaxSteps; ++i) steps_[i] = fresh[i];
    len_ = len; playing_ = playing;
    repaint();
}

StepGrid::StepGrid(StepSequencer& s) : seq_(s)
{
    for (auto* b : {&page1Btn_, &page2Btn_})
    {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1003);
        addChildComponent(*b); // only shown in the ring view
    }
    page1Btn_.setToggleState(true, juce::dontSendNotification);
    page1Btn_.onClick = [this] { if (page1Btn_.getToggleState()) setPage(0); };
    page2Btn_.onClick = [this] { if (page2Btn_.getToggleState()) setPage(1); };
}

void StepGrid::setView(View v)
{
    if (v == view_) return;
    view_ = v;
    hover_ = -1;
    page1Btn_.setVisible(v == View::Ring);
    page2Btn_.setVisible(v == View::Ring);
    repaint();
}

void StepGrid::setPage(int p)
{
    p = juce::jlimit(0, 1, p);
    if (p == page_) return;
    page_ = p;
    hover_ = -1;
    (p == 0 ? page1Btn_ : page2Btn_).setToggleState(true, juce::dontSendNotification);
    repaint();
}

void StepGrid::setHover(int step)
{
    if (step == hover_) return;
    hover_ = step;
    if (view_ != View::Grid) repaint();
}

const char* StepGrid::laneName(Lane l)
{
    switch (l)
    {
        case Lane::Pitch: return "PITCH";   case Lane::Prob: return "PROB";   case Lane::Ratchet: return "RATCH";
        case Lane::Gate: return "GATE";     case Lane::Accent: return "ACCENT"; case Lane::Oct: return "OCT";
        case Lane::Cond: return "COND";
    }
    return "";
}

// ---- ring geometry: rings of 16 buttons, slot 0 at 12 o'clock, clockwise; as big as the component allows ----
StepGrid::RingGeometry StepGrid::makeGeometry(juce::Point<float> centre, float outerR) const
{
    RingGeometry g;
    g.centre = centre;
    g.outerR = juce::jmax(10.0f, outerR);
    g.buttonR = g.outerR * 0.140f;
    g.trackR = g.outerR - g.buttonR - g.outerR * 0.04f;
    return g;
}

StepGrid::RingGeometry StepGrid::singleGeometry() const
{
    // the margin leaves room for the playing step's glow
    return makeGeometry(getLocalBounds().toFloat().getCentre(), (float) juce::jmin(getWidth(), getHeight()) * 0.5f - 8.0f);
}

StepGrid::RingGeometry StepGrid::ringGeometry(int ring) const
{
    if (view_ != View::TwoRings) return singleGeometry();
    // two rings side by side, as big as the height allows (and the width: two diameters and a small gap must fit)
    const float W = (float) getWidth(), H = (float) getHeight();
    const float gap = juce::jmax(6.0f, W * 0.02f);
    const float R = juce::jmax(10.0f, juce::jmin(H * 0.5f - 8.0f, (W - gap) * 0.25f));
    const float cx = W * 0.5f + (ring == 0 ? -1.0f : 1.0f) * (R + gap * 0.5f);
    return makeGeometry({cx, H * 0.5f}, R);
}

juce::Point<float> StepGrid::ringSlotCentre(int ring, int slot) const
{
    const auto g = ringGeometry(ring);
    const float a = -juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi * (float) slot / (float) kRingSlots;
    return {g.centre.x + g.trackR * std::cos(a), g.centre.y + g.trackR * std::sin(a)};
}

int StepGrid::slotToStep(int ring, int slot) const
{
    return view_ == View::TwoRings ? RingLayout::twoRingsStep(ring, slot) : RingLayout::singleRingStep(slot, page_);
}

bool StepGrid::stepToSlot(int step, int& ring, int& slot) const
{
    if (step < 0 || step >= StepSequencer::kMaxSteps) return false;
    if (view_ == View::TwoRings) { RingLayout::twoRingsSlot(step, ring, slot); return true; }
    if (step < page_ * kRingSlots || step >= (page_ + 1) * kRingSlots) return false;
    ring = 0; slot = step - page_ * kRingSlots;
    return true;
}

void StepGrid::resized()
{
    // the two page buttons (RING view) sit to the right of the ring, one above the other
    const auto g = singleGeometry();
    const float sc = getWidth() / 612.0f;
    const int bw = juce::roundToInt(64.0f * sc), bh = juce::roundToInt(26.0f * sc), gap = juce::roundToInt(6.0f * sc);
    const int x = juce::roundToInt(g.centre.x + g.outerR + 24.0f * sc);
    const int top = juce::roundToInt(g.centre.y) - bh - gap / 2;
    page1Btn_.setBounds(x, top, bw, bh);
    page2Btn_.setBounds(x, top + bh + gap, bw, bh);
}

juce::Rectangle<float> StepGrid::cellBounds(int i) const
{
    const float w = getWidth() / 16.0f, h = getHeight() / 2.0f;
    return juce::Rectangle<float>((i % 16) * w, (i / 16) * h, w, h).reduced(2.0f);
}

int StepGrid::cellAt(juce::Point<int> p) const
{
    if (!getLocalBounds().contains(p)) return -1;
    if (view_ != View::Grid) // the button whose centre is nearest, if the point is within (a little more than) its radius
    {
        const float maxD = singleGeometry().buttonR * 1.2f; // both views use the same button size
        int best = -1;
        float bestD = maxD;
        const auto pf = p.toFloat();
        for (int ring = 0; ring < numRings(); ++ring)
            for (int slot = 0; slot < kRingSlots; ++slot)
            {
                const float d = pf.getDistanceFrom(ringSlotCentre(ring, slot));
                if (d <= bestD) { bestD = d; best = slotToStep(ring, slot); }
            }
        return best;
    }
    const int col = juce::jlimit(0, 15, (int) (p.x / (getWidth() / 16.0f)));
    const int row = juce::jlimit(0, 1, (int) (p.y / (getHeight() / 2.0f)));
    return row * 16 + col;
}

int StepGrid::effectiveGate(const SeqStep& st) const
{
    return st.gate > 0 ? (int) st.gate : juce::jlimit(5, 100, juce::roundToInt(globalGate_ * 100.0f));
}

int StepGrid::laneValue(const SeqStep& st) const
{
    switch (lane_)
    {
        case Lane::Pitch:   return st.note;
        case Lane::Prob:    return st.prob;
        case Lane::Ratchet: return st.ratchet;
        case Lane::Gate:    return effectiveGate(st);
        case Lane::Oct:     return st.octChance;
        case Lane::Cond:    return StepSequencer::conditionToIndex(st.condA, st.condB);
        case Lane::Accent:  return st.accent ? 1 : 0;
    }
    return 0;
}

void StepGrid::setLaneValue(int i, int v)
{
    switch (lane_)
    {
        case Lane::Prob:    seq_.setProb(i, juce::jlimit(0, 100, v)); break;
        case Lane::Ratchet: seq_.setRatchet(i, juce::jlimit(1, StepSequencer::kMaxRatchet, v)); break;
        case Lane::Gate:    seq_.setStepGate(i, juce::jlimit(5, 100, v)); break;
        case Lane::Oct:     seq_.setOctChance(i, juce::jlimit(0, 100, v)); break;
        case Lane::Cond:
        {
            int a = 1, b = 1;
            StepSequencer::conditionFromIndex(juce::jlimit(0, StepSequencer::kNumConditions - 1, v), a, b);
            seq_.setCondition(i, a, b);
            break;
        }
        case Lane::Pitch: case Lane::Accent: break;
    }
}

juce::String StepGrid::describe(const SeqStep& st, float& fill, bool& bright, bool& dimBar) const
{
    fill = 0.0f; bright = true; dimBar = false;
    juce::String text;
    switch (lane_)
    {
        case Lane::Pitch:   text = noteName(st.note); break;
        case Lane::Prob:    fill = st.prob / 100.0f; text = juce::String(st.prob) + "%"; break;
        case Lane::Ratchet: fill = (st.ratchet - 1) / 7.0f; bright = st.ratchet > 1; text = "x" + juce::String(st.ratchet); break;
        case Lane::Gate:    fill = effectiveGate(st) / 100.0f; bright = st.gate > 0; dimBar = st.gate == 0; text = juce::String(effectiveGate(st)) + "%"; break;
        case Lane::Accent:  fill = st.accent ? 1.0f : 0.0f; bright = st.accent; text = st.accent ? "ACC" : "-"; break;
        case Lane::Oct:     fill = st.octChance / 100.0f; bright = st.octChance > 0; text = juce::String(st.octChance) + "%"; break;
        case Lane::Cond:    bright = st.condB > 1; text = st.condB > 1 ? juce::String(st.condA) + ":" + juce::String(st.condB) : juce::String("ALL"); break;
    }
    return text;
}

void StepGrid::paint(juce::Graphics& g)
{
    if (view_ != View::Grid) paintRing(g); else paintGrid(g);
}

void StepGrid::paintGrid(juce::Graphics& g)
{
    const float scale = getWidth() / 700.0f;
    const bool barLane = lane_ != Lane::Pitch;
    for (int i = 0; i < StepSequencer::kMaxSteps; ++i)
    {
        const auto r = cellBounds(i);
        const bool inUse = i < len_;
        const bool isPlaying = i == playing_ && inUse;
        if (!inUse)
        {
            g.setColour(kEdge.withAlpha(0.6f));
            g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
        }
        else
        {
            const SeqStep& st = steps_[i];
            const bool rest = st.rest;

            // what this lane shows for the step: a bar (fill 0..1), a text, and whether the value is a non-default one
            float fill = 0.0f; bool bright = true, dimBar = false;
            const juce::String text = describe(st, fill, bright, dimBar);

            // in the PITCH lane the playing step is filled solid; in the other lanes the fill is the value bar
            const bool solid = isPlaying && !barLane;
            g.setColour(solid ? kAccent : (rest ? juce::Colour(0xff1b1c20) : juce::Colour(0xff2d2f35)));
            g.fillRoundedRectangle(r, 5.0f);
            if (barLane && !rest && fill > 0.0f)
            {
                juce::Path clip; clip.addRoundedRectangle(r, 5.0f);
                g.saveState();
                g.reduceClipRegion(clip);
                const float h = r.getHeight() * fill;
                g.setColour(kAccent.withAlpha(isPlaying ? 0.85f : (dimBar ? 0.22f : 0.42f)));
                g.fillRect(r.getX(), r.getBottom() - h, r.getWidth(), h);
                g.restoreState();
            }
            g.setColour(isPlaying ? kAccent : kEdge);
            g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, isPlaying && barLane ? 2.0f : 1.0f);
            g.setColour(solid ? kBg : (rest ? kDim : (bright ? kText : kDim)));
            g.setFont(makeFont(juce::jmax(9.0f, 12.5f * scale), true));
            g.drawText(rest ? juce::String("-") : text, r, juce::Justification::centred);
        }
        g.setColour(isPlaying && lane_ == Lane::Pitch ? kBg : kDim.withAlpha(0.8f));
        g.setFont(makeFont(juce::jmax(7.0f, 8.5f * scale)));
        g.drawText(juce::String(i + 1), r.reduced(4.0f, 2.0f), juce::Justification::topLeft);
    }
}

// ---- ring views (v0.7). Same look as the grid: the lane value is the fill level of the button, the playing step has the amber
// outline and a brighter fill (plus a soft glow); steps beyond the LOOP length are dimmed; the exact value is in the middle. ----
void StepGrid::paintRing(juce::Graphics& g)
{
    const bool barLane = lane_ != Lane::Pitch;
    const int effLoop = loopLen_ <= 0 ? len_ : juce::jmin(loopLen_, len_);
    const int rings = numRings();
    const int pointed = dragIdx_ >= 0 ? dragIdx_ : hover_;

    for (int ring = 0; ring < rings; ++ring) // the tracks
    {
        const auto geo = ringGeometry(ring);
        g.setColour(kEdge.withAlpha(0.7f));
        g.drawEllipse(geo.centre.x - geo.trackR, geo.centre.y - geo.trackR, geo.trackR * 2.0f, geo.trackR * 2.0f, 1.2f);
    }

    if (view_ == View::TwoRings) // faint arrows where the path hops between the rings: step 9 -> 10 and step 25 -> 26
    {
        for (int from : {8, 24})
        {
            int r0 = 0, s0 = 0, r1 = 0, s1 = 0;
            stepToSlot(from, r0, s0); stepToSlot(from + 1, r1, s1);
            const float rb = ringGeometry(0).buttonR;
            const juce::Line<float> line(ringSlotCentre(r0, s0), ringSlotCentre(r1, s1));
            g.setColour(kDim.withAlpha(0.55f));
            g.drawArrow(line.withShortenedStart(rb + 3.0f).withShortenedEnd(rb + 3.0f), 1.2f, 7.0f, 6.0f);
        }
    }

    {   // soft glow behind the playing step
        int r = 0, sl = 0;
        if (playing_ >= 0 && playing_ < len_ && stepToSlot(playing_, r, sl))
        {
            const auto c = ringSlotCentre(r, sl);
            const float gr = ringGeometry(r).buttonR * 2.0f;
            g.setGradientFill(juce::ColourGradient(kAccent.withAlpha(0.35f), c, kAccent.withAlpha(0.0f), c.translated(gr, 0.0f), true));
            g.fillEllipse(c.x - gr, c.y - gr, gr * 2.0f, gr * 2.0f);
        }
    }

    for (int ring = 0; ring < rings; ++ring)
        for (int slot = 0; slot < kRingSlots; ++slot)
        {
            const int i = slotToStep(ring, slot);
            const auto c = ringSlotCentre(ring, slot);
            const float r = ringGeometry(ring).buttonR;
            const juce::Rectangle<float> box(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
            const bool inUse = i < len_;
            const bool outsideLoop = inUse && i >= effLoop;
            const bool isPlaying = i == playing_ && inUse;

            if (outsideLoop) g.beginTransparencyLayer(0.4f);
            bool rest = true;
            if (!inUse)
            {
                g.setColour(kEdge.withAlpha(0.6f));
                g.drawEllipse(box.reduced(0.5f), 1.0f);
            }
            else
            {
                const SeqStep& st = steps_[i];
                rest = st.rest;
                float fill = 0.0f; bool bright = true, dimBar = false;
                describe(st, fill, bright, dimBar);

                const bool solid = isPlaying && !barLane;
                g.setColour(solid ? kAccent : (rest ? juce::Colour(0xff1b1c20) : juce::Colour(0xff2d2f35)));
                g.fillEllipse(box);
                if (barLane && !rest && fill > 0.0f)
                {
                    juce::Path clip; clip.addEllipse(box);
                    g.saveState();
                    g.reduceClipRegion(clip);
                    const float h = box.getHeight() * fill;
                    g.setColour(kAccent.withAlpha(isPlaying ? 0.85f : (dimBar ? 0.22f : 0.42f)));
                    g.fillRect(box.getX(), box.getBottom() - h, box.getWidth(), h);
                    g.restoreState();
                }
                g.setColour(isPlaying ? kAccent : kEdge);
                g.drawEllipse(box.reduced(0.5f), isPlaying && barLane ? 2.0f : 1.0f);
            }
            if (i == pointed) // the step the readout describes
            {
                g.setColour(kText.withAlpha(0.95f));
                g.drawEllipse(box.reduced(0.8f), 1.6f);
            }
            // same rule as the grid: dark text only on the solid amber button (PITCH lane, playing step), otherwise light, or dim for rests / empty slots
            g.setColour(isPlaying && !barLane ? kBg : (!inUse || rest ? kDim : kText.withAlpha(0.9f)));
            g.setFont(makeFont(juce::jmax(7.0f, r * 0.82f), true));
            g.drawText(juce::String(i + 1), box, juce::Justification::centred);
            if (outsideLoop) g.endTransparencyLayer();
        }

    for (int ring = 0; ring < rings; ++ring) paintRingReadout(g, ring);
}

void StepGrid::paintRingReadout(juce::Graphics& g, int ring)
{
    const auto geo = ringGeometry(ring);
    const float R = geo.outerR;
    const auto c = geo.centre;

    // Which step this ring's middle describes. The step under the mouse (or being dragged) goes to the ring that holds it; in the
    // 2 RINGS view the other ring then shows the playing step if that is on it, else just the lane name.
    const int pointed = dragIdx_ >= 0 ? dragIdx_ : hover_;
    auto onThisRing = [&](int step) { int r = 0, sl = 0; return stepToSlot(step, r, sl) && r == ring; };
    int shown = -1;
    if (view_ == View::TwoRings)
    {
        if (pointed >= 0 && onThisRing(pointed)) shown = pointed;
        else if (playing_ >= 0 && playing_ < len_ && onThisRing(playing_)) shown = playing_;
    }
    else
        shown = pointed >= 0 ? pointed : (playing_ >= 0 && playing_ < len_ ? playing_ : -1); // the single ring also describes a playing step on the other page

    auto line = [&](const juce::String& text, float cy, float height, juce::Colour col)
    {
        juce::Font f = makeFont(height, true);
        juce::GlyphArrangement ga;
        ga.addLineOfText(f, text, 0.0f, 0.0f);
        const float w = ga.getBoundingBox(0, -1, true).getWidth(), maxW = R * 1.25f;
        if (w > maxW) f = makeFont(height * maxW / w, true); // long texts shrink to fit inside the ring
        g.setColour(col);
        g.setFont(f);
        g.drawText(text, juce::Rectangle<float>(c.x - R * 0.7f, cy - height, R * 1.4f, height * 2.0f), juce::Justification::centred);
    };

    if (shown < 0) { line(laneName(lane_), c.y, R * 0.16f, kAccent); return; } // nothing to describe: just the lane name

    line("STEP " + juce::String(shown + 1), c.y - R * 0.30f, R * 0.125f, kDim);
    line(laneName(lane_), c.y - R * 0.13f, R * 0.125f, kAccent);
    if (shown >= len_) { line("EMPTY", c.y + R * 0.17f, R * 0.22f, kDim); return; }
    const SeqStep& st = steps_[shown];
    if (st.rest) { line("REST", c.y + R * 0.17f, R * 0.26f, kDim); return; }
    float fill = 0.0f; bool bright = true, dimBar = false;
    line(describe(st, fill, bright, dimBar), c.y + R * 0.17f, R * 0.30f, bright ? kText : kDim);
    if (lane_ == Lane::Gate && st.gate == 0) line("FOLLOWS KNOB", c.y + R * 0.42f, R * 0.10f, kDim);
}

void StepGrid::paintOverChildren(juce::Graphics& g)
{
    // ring view: a small amber dot on the page button of the other page when the playing step is over there
    if (view_ != View::Ring || playing_ < 0 || playing_ >= len_) return;
    const int playPage = playing_ / kRingSlots;
    if (playPage == page_) return;
    const auto& b = playPage == 0 ? page1Btn_ : page2Btn_;
    g.setColour(kAccent);
    g.fillEllipse((float) b.getRight() - 11.0f, (float) b.getY() + 4.0f, 7.0f, 7.0f);
}

void StepGrid::mouseDown(const juce::MouseEvent& e)
{
    dragIdx_ = cellAt(e.getPosition());
    dragged_ = false;
    dragStartY_ = e.y;
    dragStartValue_ = (dragIdx_ >= 0 && dragIdx_ < len_) ? laneValue(steps_[dragIdx_]) : (lane_ == Lane::Pitch ? 60 : 0);
    if (view_ != View::Grid) { setHover(dragIdx_); repaint(); } // the readout follows the step being pressed (vertical drag, as in the grid)
}

void StepGrid::mouseMove(const juce::MouseEvent& e)
{
    if (view_ != View::Grid) setHover(cellAt(e.getPosition()));
}

void StepGrid::mouseExit(const juce::MouseEvent&)
{
    if (view_ != View::Grid && dragIdx_ < 0) setHover(-1);
}

void StepGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (dragIdx_ < 0 || dragIdx_ >= len_ || steps_[dragIdx_].rest || lane_ == Lane::Accent) return;
    const int dy = dragStartY_ - e.y;
    if (std::abs(dy) > 4) dragged_ = true;
    if (!dragged_) return;
    switch (lane_)
    {
        case Lane::Pitch: // with a scale set the stored note snaps to a scale tone while dragging
            seq_.setNote(dragIdx_, StepSequencer::quantizeNote(juce::jlimit(0, 127, dragStartValue_ + dy / 6), root_, scale_));
            break;
        case Lane::Prob: case Lane::Gate: case Lane::Oct: setLaneValue(dragIdx_, dragStartValue_ + dy); break; // 1 pixel = 1 %
        case Lane::Ratchet: setLaneValue(dragIdx_, dragStartValue_ + dy / 12); break;                          // 12 pixels per repeat
        case Lane::Cond:    setLaneValue(dragIdx_, dragStartValue_ + dy / 6); break;                           // 6 pixels per condition
        case Lane::Accent:  break;
    }
    refresh();
}

void StepGrid::mouseUp(const juce::MouseEvent& e)
{
    if (!dragged_ && dragIdx_ >= 0)
    {
        if (lane_ == Lane::Pitch) { seq_.toggleRest(dragIdx_); refresh(); }
        else if (lane_ == Lane::Accent) { seq_.toggleAccent(dragIdx_); refresh(); } // in the other lanes a plain click does nothing
    }
    dragIdx_ = -1;
    if (view_ != View::Grid) { setHover(cellAt(e.getPosition())); repaint(); }
}

void StepGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int i = cellAt(e.getPosition());
    if (i < 0 || i >= len_) return;
    switch (lane_)
    {
        case Lane::Prob:    seq_.setProb(i, 100); break;
        case Lane::Ratchet: seq_.setRatchet(i, 1); break;
        case Lane::Gate:    seq_.setStepGate(i, 0); break;      // back to following the GATE knob
        case Lane::Oct:     seq_.setOctChance(i, 0); break;
        case Lane::Cond:    seq_.setCondition(i, 1, 1); break;  // ALL
        case Lane::Pitch: case Lane::Accent: return;
    }
    refresh();
}

void StepGrid::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = cellAt(e.getPosition());
    if (i < 0 || i >= len_ || steps_[i].rest || w.deltaY == 0.0f) return;
    const int up = w.deltaY > 0 ? 1 : -1;
    switch (lane_)
    {
        case Lane::Pitch:   seq_.setNote(i, StepSequencer::scaleStep(steps_[i].note, up, root_, scale_)); break; // one scale step per notch
        case Lane::Prob:    setLaneValue(i, laneValue(steps_[i]) + 5 * up); break;
        case Lane::Gate:    setLaneValue(i, laneValue(steps_[i]) + 5 * up); break;
        case Lane::Oct:     setLaneValue(i, laneValue(steps_[i]) + 5 * up); break;
        case Lane::Ratchet: setLaneValue(i, laneValue(steps_[i]) + up); break;
        case Lane::Cond:    setLaneValue(i, laneValue(steps_[i]) + up); break;
        case Lane::Accent:  return;
    }
    refresh();
}

// ---------------------------------------------------------------------------------------------
WT8Editor::Knob& WT8Editor::addKnob(const juce::String& id, const juce::String& label, Kind kind, bool bipolar)
{
    auto k = std::make_unique<Knob>();
    auto& s = k->slider;

    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 18);
    s.setMouseDragSensitivity(kind == Kind::Pitch ? 800 : 250); // Pitch spans a lot of range: slower drag
    s.setScrollWheelEnabled(true);
    s.getProperties().set("bipolar", bipolar);
    s.setLookAndFeel(&laf_);

    // Attach first: the attachment installs its own text functions, which we then replace.
    k->attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc_.apvts, id, s);

    switch (kind)
    {
        case Kind::Int:
            s.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)); };
            s.valueFromTextFunction = [](const juce::String& t) { return (double) t.getIntValue(); };
            break;
        case Kind::Octave:
            s.textFromValueFunction = [](double v) {
                const int n = juce::roundToInt(v);
                return n > 0 ? "+" + juce::String(n) : juce::String(n);
            };
            s.valueFromTextFunction = [](const juce::String& t) { return (double) t.getIntValue(); };
            break;
        case Kind::Pitch:
            s.textFromValueFunction = [](double v) {
                return juce::String(v >= 0.005 ? "+" : "") + juce::String(v, 2) + " st";
            };
            s.valueFromTextFunction = [](const juce::String& t) { return (double) t.getFloatValue(); };
            break;
        case Kind::Percent:
            s.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)); };
            s.valueFromTextFunction = [](const juce::String& t) { return (double) t.getFloatValue() / 100.0; };
            break;
        case Kind::Pan:
            s.textFromValueFunction = [](double v) {
                const int n = juce::roundToInt(std::abs(v - 0.5) * 200.0);
                return n == 0 ? juce::String("C") : juce::String(v < 0.5 ? "L" : "R") + juce::String(n);
            };
            s.valueFromTextFunction = [](const juce::String& t) {
                const auto u = t.trim().toUpperCase();
                const double n = u.substring(1).getDoubleValue() / 200.0;
                if (u.startsWithChar('L')) return 0.5 - n;
                if (u.startsWithChar('R')) return 0.5 + n;
                return 0.5;
            };
            break;
        case Kind::Decibels:
            s.textFromValueFunction = [](double v) {
                return juce::String(v >= 0.05 ? "+" : "") + juce::String(v, 1) + " dB";
            };
            s.valueFromTextFunction = [](const juce::String& t) { return (double) t.getFloatValue(); };
            break;
        case Kind::LoopLength: // 0 = the whole pattern
            s.textFromValueFunction = [](double v) { const int n = juce::roundToInt(v); return n == 0 ? juce::String("ALL") : juce::String(n); };
            s.valueFromTextFunction = [](const juce::String& t) { return t.trim().equalsIgnoreCase("all") ? 0.0 : (double) t.getIntValue(); };
            break;
        case Kind::Swing: // 50..75 %, shown without a trailing ".0"
            s.textFromValueFunction = [](double v) { return juce::String(v, 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + "%"; };
            s.valueFromTextFunction = [](const juce::String& t) { return (double) t.retainCharacters("0123456789.").getFloatValue(); };
            break;
        case Kind::Seed: // 0 = different every time
            s.textFromValueFunction = [](double v) { const int n = juce::roundToInt(v); return n == 0 ? juce::String("RANDOM") : juce::String(n); };
            s.valueFromTextFunction = [](const juce::String& t) { return t.trim().startsWithIgnoreCase("r") ? 0.0 : (double) t.getIntValue(); };
            break;
    }

    s.updateText();

    if (auto* param = proc_.apvts.getParameter(id))
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            s.setDoubleClickReturnValue(true, ranged->convertFrom0to1(ranged->getDefaultValue()));


    auto& n = k->name;
    n.setText(label, juce::dontSendNotification);
    n.setJustificationType(juce::Justification::centred);
    n.setColour(juce::Label::textColourId, kDim);
    n.getProperties().set("fontHeight", 11.0f);
    n.setLookAndFeel(&laf_);
    n.setInterceptsMouseClicks(false, false);

    addAndMakeVisible(s);
    addAndMakeVisible(n);

    knobs_.push_back(std::move(k));
    return *knobs_.back();
}

WT8Editor::WT8Editor(WT8AudioProcessor& p) : juce::AudioProcessorEditor(&p), proc_(p), grid_(p.sequencer())
{
    setLookAndFeel(&laf_);
    auto& table   = addKnob("table",  "WAVETABLE", Kind::Int);
    auto& frame   = addKnob("cycle",  "FRAME",     Kind::Int);
    auto& octave  = addKnob("octave", "OCTAVE",    Kind::Octave, true);
    auto& pitch   = addKnob("pitch",  "PITCH",     Kind::Pitch,  true);

    auto& attack  = addKnob("attack",  "ATTACK",  Kind::Percent);
    auto& release = addKnob("release", "RELEASE", Kind::Percent);

    auto& cutoff  = addKnob("cutoff",    "CUTOFF",    Kind::Percent);
    auto& reso    = addKnob("resonance", "RESONANCE", Kind::Percent);

    auto& plfoD   = addKnob("pitchlfodepth",  "PITCH DEPTH",  Kind::Percent);
    auto& plfoR   = addKnob("pitchlforate",   "PITCH RATE",   Kind::Percent);
    auto& flfoD   = addKnob("filterlfodepth", "FILTER DEPTH", Kind::Percent);
    auto& flfoR   = addKnob("filterlforate",  "FILTER RATE",  Kind::Percent);

    auto& fx      = addKnob("fx",     "DELAY / REVERB", Kind::Percent);
    auto& fxTime  = addKnob("fxtime", "TIME",           Kind::Percent);
    auto& comp    = addKnob("comp",   "COMP / SAT",     Kind::Percent);

    auto& gain    = addKnob("gain",   "GAIN",  Kind::Percent);
    auto& pan     = addKnob("pan",    "PAN",   Kind::Pan, true);
    auto& boost   = addKnob("output", "BOOST", Kind::Decibels, true);

    row1_.push_back({"OSCILLATOR", {&table, &frame, &octave, &pitch}, {}});
    row1_.push_back({"ENVELOPE",   {&attack, &release}, {}});
    row1_.push_back({"FILTER",     {&cutoff, &reso}, {}});
    row2_.push_back({"LFO",        {&plfoD, &plfoR, &flfoD, &flfoR}, {}});
    row2_.push_back({"EFFECTS",    {&fx, &fxTime, &comp}, {}});
    row2_.push_back({"OUTPUT",     {&gain, &pan, &boost}, {}});

    // ---- sequencer strip ----
    seqKnobs_ = {&addKnob("seq_gate",  "GATE",   Kind::Percent),    &addKnob("seq_prob",   "PROB",   Kind::Percent),
                 &addKnob("seq_loop",  "LOOP",   Kind::LoopLength), &addKnob("seq_seed",   "SEED",   Kind::Seed),
                 &addKnob("seq_swing", "SWING",  Kind::Swing),      &addKnob("seq_accent", "ACCENT", Kind::Percent)};

    for (auto* b : {&recBtn_, &restBtn_, &delBtn_, &clearBtn_, &playBtn_, &muteBtn_}) addAndMakeVisible(*b);
    recBtn_.setClickingTogglesState(true);
    recBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd9534f));
    recBtn_.onClick = [this] { proc_.setSeqRecording(recBtn_.getToggleState()); };
    restBtn_.onClick = [this] { proc_.sequencer().addRest(); grid_.refresh(); };
    delBtn_.onClick = [this] { proc_.sequencer().deleteLast(); grid_.refresh(); };
    clearBtn_.onClick = [this] { proc_.sequencer().clear(); grid_.refresh(); };
    playBtn_.setClickingTogglesState(true);
    muteBtn_.setClickingTogglesState(true);
    playAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc_.apvts, "seq_play", playBtn_);
    muteAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc_.apvts, "seq_mute", muteBtn_);

    auto fillCombo = [this](juce::ComboBox& box, const char* id) {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(proc_.apvts.getParameter(id)))
            box.addItemList(choice->choices, 1);
        addAndMakeVisible(box);
    };
    fillCombo(syncBox_, "seq_sync");
    fillCombo(divBox_, "seq_div");
    fillCombo(dirBox_, "seq_dir");
    fillCombo(scaleBox_, "seq_scale");
    fillCombo(rootBox_, "seq_root");
    fillCombo(octBox_, "seq_octmode");
    octAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "seq_octmode", octBox_);
    scaleAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "seq_scale", scaleBox_);
    rootAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "seq_root", rootBox_);
    syncAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "seq_sync", syncBox_);
    divAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "seq_div", divBox_);
    dirAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "seq_dir", dirBox_);

    // lane switch (which per-step value the grid shows and edits) - editor state only, not saved
    auto addLane = [this](juce::TextButton& btn, StepGrid::Lane lane) {
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(1001);
        addAndMakeVisible(btn);
        btn.onClick = [this, &btn, lane] { if (btn.getToggleState()) grid_.setLane(lane); };
    };
    addLane(pitchLaneBtn_, StepGrid::Lane::Pitch);   addLane(probLaneBtn_, StepGrid::Lane::Prob);
    addLane(ratchLaneBtn_, StepGrid::Lane::Ratchet); addLane(gateLaneBtn_, StepGrid::Lane::Gate);
    addLane(accentLaneBtn_, StepGrid::Lane::Accent); addLane(octLaneBtn_, StepGrid::Lane::Oct);
    addLane(condLaneBtn_, StepGrid::Lane::Cond);
    pitchLaneBtn_.setToggleState(true, juce::dontSendNotification);

    // v0.7 view switch: the same steps as the 2 x 16 grid or as one ring (editor state only, like the lane; a reopened editor starts in GRID)
    for (auto* b : {&gridViewBtn_, &ringViewBtn_, &twoRingsViewBtn_})
    {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1002);
        addAndMakeVisible(*b);
    }
    gridViewBtn_.onClick = [this] { if (gridViewBtn_.getToggleState()) grid_.setView(StepGrid::View::Grid); };
    ringViewBtn_.onClick = [this] { if (ringViewBtn_.getToggleState()) grid_.setView(StepGrid::View::Ring); };
    twoRingsViewBtn_.onClick = [this] { if (twoRingsViewBtn_.getToggleState()) grid_.setView(StepGrid::View::TwoRings); };
    gridViewBtn_.setToggleState(true, juce::dontSendNotification);

    pendBtn_.setClickingTogglesState(true);
    addAndMakeVisible(pendBtn_);
    pendAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc_.apvts, "seq_pendrep", pendBtn_);

    // MIDI XPOSE: while on, the last key played sets the pattern transpose (C3 = none); RESET puts it back to 0
    addAndMakeVisible(xposeBtn_);
    xposeBtn_.setClickingTogglesState(true);
    xposeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc_.apvts, "seq_xpose", xposeBtn_);
    addAndMakeVisible(xposeResetBtn_);
    xposeResetBtn_.onClick = [this] { proc_.setSeqTranspose(0); };
    addAndMakeVisible(xposeReadout_);
    xposeReadout_.setJustificationType(juce::Justification::centred);
    xposeReadout_.getProperties().set("fontHeight", 13.0f);
    xposeReadout_.setInterceptsMouseClicks(false, false);

    for (auto* l : {&syncLabel_, &divLabel_, &laneLabel_, &dirLabel_, &scaleLabel_, &rootLabel_, &octLabel_})
    {
        l->setJustificationType(juce::Justification::centredRight);
        l->setColour(juce::Label::textColourId, kDim);
        l->getProperties().set("fontHeight", 11.0f);
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
    }
    syncLabel_.setText("SYNC", juce::dontSendNotification);
    divLabel_.setText("STEP", juce::dontSendNotification);
    laneLabel_.setText("EDIT", juce::dontSendNotification);
    dirLabel_.setText("DIRECTION", juce::dontSendNotification);
    scaleLabel_.setText("SCALE", juce::dontSendNotification);
    rootLabel_.setText("ROOT", juce::dontSendNotification);
    octLabel_.setText("OCT JUMP", juce::dontSendNotification);

    // v0.8 pattern slots (row C): 16 patterns kept inside the project; choosing one plays it from now on
    addAndMakeVisible(patLabel_);
    patLabel_.setText("PATTERN", juce::dontSendNotification);
    patLabel_.setJustificationType(juce::Justification::centredRight);
    patLabel_.setColour(juce::Label::textColourId, kDim);
    patLabel_.getProperties().set("fontHeight", 11.0f);
    patLabel_.setInterceptsMouseClicks(false, false);
    for (int i = 0; i < WT8AudioProcessor::kPatternSlots; ++i) { patBox_.addItem(juce::String(i + 1), i + 1); patTexts_.add(juce::String(i + 1)); }
    addAndMakeVisible(patBox_);
    addAndMakeVisible(patPrevBtn_);
    addAndMakeVisible(patNextBtn_);
    patBox_.onChange = [this] { proc_.selectPatternSlot(patBox_.getSelectedId() - 1); grid_.refresh(); };
    patPrevBtn_.onClick = [this] { stepPatternSlot(-1); };
    patNextBtn_.onClick = [this] { stepPatternSlot(+1); };
    refreshPatternBox();

    // v0.8 presets (header): the sound settings as files in a folder, so every project can use them
    for (auto* b : {&presetPrevBtn_, &presetNextBtn_, &presetSaveBtn_, &presetFolderBtn_}) addAndMakeVisible(*b);
    addAndMakeVisible(presetBox_);
    presetBox_.setTextWhenNothingSelected("PRESET");
    presetBox_.onChange = [this] {
        const int id = presetBox_.getSelectedId();
        if (id == 1) proc_.loadInitPreset();
        else if (id >= 2 && id - 2 < presetNames_.size() && !proc_.loadPreset(presetNames_[id - 2]))
            presetBox_.setText("(could not load)", juce::dontSendNotification);
    };
    presetPrevBtn_.onClick = [this] { stepPreset(-1); };
    presetNextBtn_.onClick = [this] { stepPreset(+1); };
    presetSaveBtn_.onClick = [this] { promptSavePreset(); };
    presetFolderBtn_.onClick = [this] { auto f = proc_.getPresetFolder(); f.createDirectory(); f.revealToUser(); };
    refreshPresetList({});

    addAndMakeVisible(grid_);

    setResizable(true, true);
    setResizeLimits(630, 647, 1260, 1293); // v0.7: 840 x 862 (was 840 x 762) to give the ring room
    getConstrainer()->setFixedAspectRatio(840.0 / 862.0);
    setSize(840, 862);
    grid_.refresh();
    timerCallback(); // fill in the transpose readout and scale state straight away
    startTimerHz(15);
}

void WT8Editor::timerCallback()
{
    grid_.refresh();
    recBtn_.setToggleState(proc_.isSeqRecording(), juce::dontSendNotification);
    playBtn_.setEnabled(syncBox_.getSelectedItemIndex() == 0); // in "Logic" sync, Logic's transport is the play button
    pendBtn_.setEnabled(dirBox_.getSelectedItemIndex() == 2);  // end-repeat only applies to the pendulum

    refreshPatternBox();
    grid_.setGlobalGate(*proc_.apvts.getRawParameterValue("seq_gate")); // the GATE lane shows this for steps without their own gate
    grid_.setLoopLength(juce::roundToInt(proc_.apvts.getRawParameterValue("seq_loop")->load())); // the ring view dims the steps beyond the loop

    // scale (item 0 of the box = Off) for the grid's pitch editing
    grid_.setScale(scaleBox_.getSelectedItemIndex() - 1, rootBox_.getSelectedItemIndex());

    // transpose readout: lit while MIDI XPOSE is on, dim (the pattern plays as recorded) while it is off
    const int xp = proc_.getSeqTranspose();
    const bool xposeOn = xposeBtn_.getToggleState();
    xposeReadout_.setText((xp > 0 ? "+" : "") + juce::String(xp) + " st", juce::dontSendNotification);
    xposeReadout_.setColour(juce::Label::textColourId, xposeOn ? kAccent : kDim);
}

void WT8Editor::refreshPatternBox()
{
    // "3 *" = the slot holds a pattern. Item texts are only rewritten when something changed.
    bool changed = false;
    for (int i = 0; i < WT8AudioProcessor::kPatternSlots; ++i)
    {
        const auto text = juce::String(i + 1) + (proc_.patternSlotHasSteps(i) ? " *" : "");
        if (text != patTexts_[i]) { patTexts_.set(i, text); patBox_.changeItemText(i + 1, text); changed = true; }
    }
    const int want = proc_.getPatternSlot() + 1;
    if (changed || patBox_.getSelectedId() != want) patBox_.setSelectedId(want, juce::dontSendNotification);
}

void WT8Editor::stepPatternSlot(int dir)
{
    const int n = WT8AudioProcessor::kPatternSlots;
    patBox_.setSelectedId((proc_.getPatternSlot() + dir + n) % n + 1, juce::sendNotificationSync);
}

void WT8Editor::refreshPresetList(const juce::String& select)
{
    presetNames_ = proc_.listPresets();
    presetBox_.clear(juce::dontSendNotification);
    presetBox_.addItem("INIT (defaults)", 1);
    presetBox_.addSeparator();
    for (int i = 0; i < presetNames_.size(); ++i) presetBox_.addItem(presetNames_[i], i + 2);
    if (select.isNotEmpty())
    {
        const int idx = presetNames_.indexOf(select);
        if (idx >= 0) presetBox_.setSelectedId(idx + 2, juce::dontSendNotification);
    }
}

void WT8Editor::stepPreset(int dir)
{
    presetNames_ = proc_.listPresets(); // (new files may have been added in the folder)
    const int count = presetNames_.size() + 1; // + INIT
    const int cur = presetBox_.getSelectedId() > 0 ? presetBox_.getSelectedId() - 1 : (dir > 0 ? -1 : 0);
    const auto keep = presetBox_.getSelectedId() >= 2 && presetBox_.getSelectedId() - 2 < presetNames_.size() ? presetNames_[presetBox_.getSelectedId() - 2] : juce::String();
    refreshPresetList(keep);
    presetBox_.setSelectedId(((cur + dir) % count + count) % count + 1, juce::sendNotificationSync);
}

void WT8Editor::promptSavePreset()
{
    auto* w = new juce::AlertWindow("SAVE PRESET", "Name for this sound (the filter, oscillator, envelope, LFO and effect settings):",
                                    juce::MessageBoxIconType::NoIcon, this);
    const int id = presetBox_.getSelectedId();
    w->addTextEditor("name", id >= 2 && id - 2 < presetNames_.size() ? presetNames_[id - 2] : juce::String());
    w->addButton("SAVE", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("CANCEL", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<WT8Editor> self(this);
    juce::Component::SafePointer<juce::AlertWindow> win(w);
    w->enterModalState(true, juce::ModalCallbackFunction::create([self, win](int result) {
        if (result != 1 || self == nullptr || win == nullptr) return;
        self->savePresetAs(win->getTextEditorContents("name"));
    }), true);
}

void WT8Editor::savePresetAs(const juce::String& rawName)
{
    const auto name = rawName.trim();
    auto say = [this](const juce::String& msg) {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::NoIcon, "PRESET", msg, "OK", this), nullptr);
    };
    if (WT8AudioProcessor::presetFileName(name).isEmpty()) { say("Please type a name."); return; }
    if (name.equalsIgnoreCase("INIT") || name.startsWithIgnoreCase("INIT (")) { say("INIT is the built-in default sound; please pick another name."); return; }
    auto doSave = [this](const juce::String& n) {
        juce::String err;
        if (!proc_.savePreset(n, err))
        {
            juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::NoIcon, "PRESET", err, "OK", this), nullptr);
            return;
        }
        const auto stem = WT8AudioProcessor::presetFileName(n).upToLastOccurrenceOf(".ipmohcpreset", false, false);
        refreshPresetList(stem);
    };
    if (proc_.listPresets().contains(WT8AudioProcessor::presetFileName(name).upToLastOccurrenceOf(".ipmohcpreset", false, false), true))
    {
        juce::Component::SafePointer<WT8Editor> self(this);
        juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::NoIcon, "REPLACE PRESET?",
                                         "A preset called \"" + name + "\" already exists. Replace it?", "REPLACE", "CANCEL", this),
                                     juce::ModalCallbackFunction::create([self, name, doSave](int r) { if (r == 1 && self != nullptr) doSave(name); }));
        return;
    }
    doSave(name);
}

WT8Editor::~WT8Editor()
{
    stopTimer();
    setLookAndFeel(nullptr);
    for (auto& k : knobs_)
    {
        k->slider.setLookAndFeel(nullptr);
        k->name.setLookAndFeel(nullptr);
    }
}

void WT8Editor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    const float scale = getWidth() / 840.0f;

    // header
    g.setColour(kText);
    g.setFont(makeFont(26.0f * scale, true));
    g.drawText("ipmohc", juce::Rectangle<int>(int(20 * scale), 0, int(300 * scale), int(48 * scale)),
               juce::Justification::centredLeft);
    g.setColour(kDim);
    g.setFont(makeFont(12.0f * scale));
    g.drawText("particledots", juce::Rectangle<int>(getWidth() - int(220 * scale), 0, int(200 * scale), int(48 * scale)),
               juce::Justification::centredRight);

    auto drawGroup = [&](const Group& grp)
    {
        const auto r = grp.bounds.toFloat();
        g.setColour(kPanel);
        g.fillRoundedRectangle(r, 8.0f * scale);
        g.setColour(kEdge);
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f * scale, 1.0f);
        g.setColour(kAccent);
        g.setFont(makeFont(11.0f * scale, true));
        g.drawText(grp.title, grp.bounds.withTrimmedLeft(int(12 * scale)).withHeight(int(24 * scale)),
                   juce::Justification::centredLeft);
    };
    for (auto& grp : row1_) drawGroup(grp);
    for (auto& grp : row2_) drawGroup(grp);
    Group seqGroup; seqGroup.title = "SEQUENCER"; seqGroup.bounds = seqBounds_;
    drawGroup(seqGroup);
}

void WT8Editor::resized()
{
    const float scale = getWidth() / 840.0f;
    const int margin = int(14 * scale);
    const int gap = int(10 * scale);
    const int headerH = int(48 * scale);

    auto full = getLocalBounds().withTrimmedTop(headerH).reduced(margin, 0);
    auto area = full.removeFromTop(int(378 * scale)); // the two knob rows
    const int rowH = (area.getHeight() - gap) / 2;

    auto layoutRow = [&](std::vector<Group>& row, juce::Rectangle<int> r)
    {
        int total = 0;
        for (auto& g : row) total += (int) g.knobs.size();
        const int usable = r.getWidth() - gap * ((int) row.size() - 1);
        int x = r.getX();
        for (size_t i = 0; i < row.size(); ++i)
        {
            auto& grp = row[i];
            const int w = (i + 1 == row.size()) ? r.getRight() - x
                                                : usable * (int) grp.knobs.size() / total;
            grp.bounds = {x, r.getY(), w, r.getHeight()};
            auto inner = grp.bounds.withTrimmedTop(int(24 * scale)).reduced(int(4 * scale), int(4 * scale));
            const int cellW = inner.getWidth() / (int) grp.knobs.size();
            for (size_t j = 0; j < grp.knobs.size(); ++j)
            {
                auto cell = inner.withX(inner.getX() + (int) j * cellW).withWidth(cellW);
                const int labelH = int(16 * scale);
                grp.knobs[j]->name.setBounds(cell.removeFromTop(labelH));
                grp.knobs[j]->name.getProperties().set("fontHeight", 11.0f * scale);
                const int textH = int(18 * scale);
                grp.knobs[j]->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, juce::jmax(48, int(cellW * 0.9f)), textH);
                grp.knobs[j]->slider.setBounds(cell);
            }
            x += w + gap;
        }
    };

    auto row1 = area.removeFromTop(rowH);
    area.removeFromTop(gap);
    layoutRow(row1_, row1);
    layoutRow(row2_, area);

    // ---- sequencer strip ----
    full.removeFromTop(gap);
    seqBounds_ = full.withTrimmedBottom(margin);
    auto inner = seqBounds_.withTrimmedTop(int(24 * scale)).reduced(int(8 * scale), int(4 * scale));

    // right: 2 columns x 3 rows of knobs (GATE PROB / LOOP SEED / SWING ACCENT)
    {
        auto block = inner.removeFromRight(int(176 * scale));
        inner.removeFromRight(int(8 * scale));
        const int cw = block.getWidth() / 2, ch = block.getHeight() / 3;
        for (size_t n = 0; n < seqKnobs_.size(); ++n)
        {
            auto cell = juce::Rectangle<int>(block.getX() + (int) (n % 2) * cw, block.getY() + (int) (n / 2) * ch, cw, ch);
            auto* k = seqKnobs_[n];
            k->name.setBounds(cell.removeFromTop(int(16 * scale)));
            k->name.getProperties().set("fontHeight", 11.0f * scale);
            k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, int(76 * scale), int(18 * scale));
            k->slider.setBounds(cell);
        }
    }

    for (auto* l : {&syncLabel_, &divLabel_, &laneLabel_, &dirLabel_, &scaleLabel_, &rootLabel_, &octLabel_, &patLabel_})
        l->getProperties().set("fontHeight", 11.0f * scale);

    // header: preset bar in the empty space between the name and "particledots"
    {
        const int bh = int(26 * scale), by = (headerH - bh) / 2;
        int x = int(150 * scale);
        auto putH = [&](juce::Component& c, int w) { c.setBounds(x, by, int(w * scale), bh); x += int((w + 6) * scale); };
        putH(presetPrevBtn_, 24); putH(presetBox_, 200); putH(presetNextBtn_, 24);
        x += int(6 * scale);
        putH(presetSaveBtn_, 46); putH(presetFolderBtn_, 58);
    }
    xposeReadout_.getProperties().set("fontHeight", 13.0f * scale);

    // row A: pattern editing, transport, timing
    auto controls = inner.removeFromTop(int(30 * scale));
    auto put = [&](juce::Component& c, int w) { c.setBounds(controls.removeFromLeft(int(w * scale))); controls.removeFromLeft(int(6 * scale)); };
    put(recBtn_, 48); put(restBtn_, 48); put(delBtn_, 42); put(clearBtn_, 52);
    controls.removeFromLeft(int(10 * scale));
    put(playBtn_, 52); put(muteBtn_, 52);
    controls.removeFromLeft(int(10 * scale));
    put(syncLabel_, 34); put(syncBox_, 66); put(divLabel_, 34); put(divBox_, 62);

    // row B: how the pattern is played back (direction) and which scale its notes are snapped to
    inner.removeFromTop(int(6 * scale));
    controls = inner.removeFromTop(int(30 * scale));
    put(dirLabel_, 66); put(dirBox_, 92); put(pendBtn_, 62);
    controls.removeFromLeft(int(14 * scale));
    put(scaleLabel_, 40); put(scaleBox_, 170); put(rootLabel_, 34); put(rootBox_, 52);

    // row C: transpose from MIDI in (v0.5), octave-jump size (v0.6)
    inner.removeFromTop(int(6 * scale));
    controls = inner.removeFromTop(int(30 * scale));
    put(xposeBtn_, 82); put(xposeReadout_, 52); put(xposeResetBtn_, 52);
    controls.removeFromLeft(int(14 * scale));
    put(octLabel_, 62); put(octBox_, 140);
    controls.removeFromLeft(int(2 * scale));
    put(patLabel_, 46); put(patPrevBtn_, 22); put(patBox_, 62); put(patNextBtn_, 22);

    // row D: which per-step value the grid shows and edits
    inner.removeFromTop(int(6 * scale));
    controls = inner.removeFromTop(int(30 * scale));
    put(laneLabel_, 30); put(pitchLaneBtn_, 50); put(probLaneBtn_, 46); put(ratchLaneBtn_, 56); put(gateLaneBtn_, 46);
    put(accentLaneBtn_, 60); put(octLaneBtn_, 40); put(condLaneBtn_, 50);
    controls.removeFromLeft(int(10 * scale));
    put(gridViewBtn_, 46); put(ringViewBtn_, 48); put(twoRingsViewBtn_, 64);

    inner.removeFromTop(int(6 * scale));
    grid_.setBounds(inner);
}
