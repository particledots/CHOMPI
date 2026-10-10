#include "PluginEditor.h"
#include "WavetableImport.h"

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

// v0.15: six sequencer rows (EDIT, PLAY, DIRECTION, SCALE, LANE, PATTERN) made the window 36 px taller than v0.14 (840 x 898);
// v0.16: a seventh row (CHAIN) makes it another 36 px taller
#if WT8_MIDI_FX
// the MIDI effect build has no sound knobs: the two knob rows (378 px) and the gap below them are gone
constexpr int kWindowW = 840, kWindowH = 970 - 378 - 10;
constexpr int kKnobRowsH = 0;
#else
constexpr int kWindowW = 840, kWindowH = 970;
constexpr int kKnobRowsH = 378;
#endif
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
    setColour(juce::PopupMenu::headerTextColourId, kAccent); // v0.9: the STARTER / USER headings in the preset list
}

juce::Font IpmohcLookAndFeel::getTextButtonFont(juce::TextButton&, int h) { return makeFont(juce::jmax(9.0f, h * 0.42f), true); }
juce::Font IpmohcLookAndFeel::getComboBoxFont(juce::ComboBox& c) { return makeFont(juce::jmax(9.0f, c.getHeight() * 0.46f)); }

// v0.15: same drawing as JUCE's LookAndFeel_V4, but the arrow area is min(30, box height) px instead of a fixed 30 px.
// At 30 px high (the default size) this is identical to before; in smaller boxes the text gets the room back.
void IpmohcLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const float corner = 3.0f;
    const juce::Rectangle<float> boxBounds(0.0f, 0.0f, (float) width, (float) height);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(boxBounds, corner);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(boxBounds.reduced(0.5f), corner, 1.0f);

    const float a = (float) juce::jmin(30, height);
    const float k = a / 30.0f;
    const juce::Rectangle<float> arrowZone((float) width - a, 0.0f, 20.0f * k, (float) height);
    juce::Path path;
    path.startNewSubPath(arrowZone.getX() + 3.0f * k, arrowZone.getCentreY() - 2.0f * k);
    path.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f * k);
    path.lineTo(arrowZone.getRight() - 3.0f * k, arrowZone.getCentreY() - 2.0f * k);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.2f));
    g.strokePath(path, juce::PathStrokeType(juce::jmax(1.4f, 2.0f * k)));
}

void IpmohcLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(1, 1, box.getWidth() - juce::jmin(30, box.getHeight()), box.getHeight() - 2);
    label.setBorderSize(juce::BorderSize<int>(1, 5, 1, 0)); // JUCE's default is 5 px on both sides; the right one only wasted room next to the arrow
    label.setFont(getComboBoxFont(box));
}

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
    tableKnob_ = &table; // (before the first resized(), which places the LOAD / RESET buttons under it)
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

    auto& fkey    = addKnob("filterkey", "KEY", Kind::Percent); // v0.19
    auto& fvel    = addKnob("filtervel", "VEL", Kind::Percent);
    auto& fenv    = addKnob("filterenv", "FILT ENV", Kind::Percent, true); // v0.20
    auto& fdec    = addKnob("filterdecay", "FILT DECAY", Kind::Percent);

    auto& fx      = addKnob("fx",     "DELAY / REVERB", Kind::Percent);
    auto& fxTime  = addKnob("fxtime", "TIME",           Kind::Percent);
    auto& comp    = addKnob("comp",   "COMP / SAT",     Kind::Percent);

    auto& gain    = addKnob("gain",   "GAIN",  Kind::Percent);
    auto& pan     = addKnob("pan",    "PAN",   Kind::Pan, true);
    auto& boost   = addKnob("output", "BOOST", Kind::Decibels, true);

    filterKnobs_[0] = &cutoff; filterKnobs_[1] = &reso;
    lfoKnobs_[0] = &plfoD; lfoKnobs_[1] = &plfoR; lfoKnobs_[2] = &flfoD; lfoKnobs_[3] = &flfoR;
    row1_.push_back({"OSCILLATOR", {&table, &frame, &octave, &pitch}, {}, 2});
    row1_.push_back({"ENVELOPE",   {&attack, &release, &fenv, &fdec}, {}}); // v0.20: the filter envelope's two knobs sit with the amp envelope
    row1_.push_back({"FILTER",     {&cutoff, &reso}, {}});
    row2_.push_back({"LFO",        {&plfoD, &plfoR, &flfoD, &flfoR}, {}});
    row2_.push_back({"FILTER MOD", {&fkey, &fvel}, {}});
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
    // v0.14: fill the pattern with random notes and rests (asks first when the pattern already holds something)
    addAndMakeVisible(randomBtn_);
    randomBtn_.onClick = [this] {
        auto doIt = [this] { proc_.randomizePattern(); grid_.refresh(); refreshSlotButtons(); };
        if (proc_.sequencer().length() == 0) { doIt(); return; }
        juce::Component::SafePointer<WT8Editor> self(this);
        juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOkCancel(
                                         juce::MessageBoxIconType::NoIcon, "REPLACE PATTERN?",
                                         "Pattern " + juce::String(proc_.getPatternSlot() + 1) + " will be replaced with random notes and rests.",
                                         "REPLACE", "CANCEL", this),
                                     juce::ModalCallbackFunction::create([self, doIt](int r) { if (self != nullptr && r == 1) doIt(); }));
    };
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
    fillCombo(filterTypeBox_, "filtertype");
    filterTypeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "filtertype", filterTypeBox_);
    fillCombo(pitchShapeBox_, "pitchlfoshape");
    fillCombo(filterShapeBox_, "filterlfoshape");
    pitchShapeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "pitchlfoshape", pitchShapeBox_);
    filterShapeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "filterlfoshape", filterShapeBox_);
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
    // v0.18: the view is kept in the processor (saved with the project); a click tells it, the editor opens in the saved view
    gridViewBtn_.onClick = [this] { if (gridViewBtn_.getToggleState()) { grid_.setView(StepGrid::View::Grid); proc_.setUiView(0); } };
    ringViewBtn_.onClick = [this] { if (ringViewBtn_.getToggleState()) { grid_.setView(StepGrid::View::Ring); proc_.setUiView(1); } };
    twoRingsViewBtn_.onClick = [this] { if (twoRingsViewBtn_.getToggleState()) { grid_.setView(StepGrid::View::TwoRings); proc_.setUiView(2); } };
    shownView_ = -1;
    syncViewFromProcessor();

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

    for (auto* l : {&syncLabel_, &divLabel_, &laneLabel_, &dirLabel_, &scaleLabel_, &rootLabel_, &octLabel_, &rowEditLabel_, &rowPlayLabel_})
    {
        l->setJustificationType(juce::Justification::centredRight);
        l->setColour(juce::Label::textColourId, kDim);
        l->getProperties().set("fontHeight", 11.0f);
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
    }
    syncLabel_.setText("SYNC", juce::dontSendNotification);
    divLabel_.setText("STEP", juce::dontSendNotification);
    laneLabel_.setText("LANE", juce::dontSendNotification); // v0.15: was "EDIT"; EDIT is now the caption of the REC / REST / DEL / CLEAR / RANDOM row
    rowEditLabel_.setText("EDIT", juce::dontSendNotification);
    rowPlayLabel_.setText("PLAY", juce::dontSendNotification);
    dirLabel_.setText("DIRECTION", juce::dontSendNotification);
    scaleLabel_.setText("SCALE", juce::dontSendNotification);
    rootLabel_.setText("ROOT", juce::dontSendNotification);
    octLabel_.setText("OCT JUMP", juce::dontSendNotification);

    // v0.9 pattern slots (row E): 16 patterns kept inside the project. A click on a slot button plays that slot from now on;
    // COPY, then a click on another slot, copies the current pattern into it (and asks first if that slot already holds one).
    addAndMakeVisible(patLabel_);
    patLabel_.setText("PATTERN", juce::dontSendNotification);
    patLabel_.setJustificationType(juce::Justification::centredRight);
    patLabel_.setColour(juce::Label::textColourId, kDim);
    patLabel_.getProperties().set("fontHeight", 11.0f);
    patLabel_.setInterceptsMouseClicks(false, false);
    for (int i = 0; i < WT8AudioProcessor::kPatternSlots; ++i)
    {
        slotBtn_[i].setButtonText(juce::String(i + 1));
        slotBtn_[i].onClick = [this, i] { onSlotClicked(i); };
        addAndMakeVisible(slotBtn_[i]);
    }
    addAndMakeVisible(copyBtn_);
    copyBtn_.onClick = [this] { setCopyMode(copyBtn_.getToggleState()); };
    copyBtn_.setClickingTogglesState(true);

    // v0.13: RETRIG goes back to step 1 (the step that is sounding plays out first); AT LOOP END makes the slot buttons wait for the end of the loop
    addAndMakeVisible(retrigBtn_);
    retrigBtn_.onClick = [this] { proc_.requestStep1(); };
    addAndMakeVisible(loopEndBtn_);
    loopEndBtn_.setClickingTogglesState(true);
    loopEndBtn_.setToggleState(proc_.getSlotAtLoopEnd(), juce::dontSendNotification);
    loopEndBtn_.onClick = [this] { proc_.setSlotAtLoopEnd(loopEndBtn_.getToggleState()); refreshSlotButtons(); };

    // v0.16 slot chaining: CHAIN on / off, the pass count of the playing slot, and the order the chain follows (display only)
    for (auto* l : {&rowChainLabel_, &repeatLabel_, &chainOrderLabel_})
    {
        l->setColour(juce::Label::textColourId, kDim);
        l->getProperties().set("fontHeight", 11.0f);
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
    }
    rowChainLabel_.setJustificationType(juce::Justification::centredRight);
    repeatLabel_.setJustificationType(juce::Justification::centredRight);
    chainOrderLabel_.setJustificationType(juce::Justification::centredLeft);
    rowChainLabel_.setText("CHAIN", juce::dontSendNotification);
    addAndMakeVisible(chainBtn_);
    chainBtn_.setClickingTogglesState(true);
    chainBtn_.onClick = [this] { proc_.setChain(chainBtn_.getToggleState()); refreshChainControls(); };
    for (int n = 1; n <= StepSequencer::kMaxRepeats; ++n) repeatBox_.addItem(juce::String(n) + "x", n);
    addAndMakeVisible(repeatBox_);
    repeatBox_.onChange = [this] { proc_.setSlotRepeats(proc_.getPatternSlot(), repeatBox_.getSelectedId()); refreshChainControls(); };
    refreshChainControls();
    refreshSlotButtons();

    // presets (header): the sound settings - INIT, the starter presets built into the plugin, and the files in the preset folder,
    // so every project can use them
    for (auto* b : {&presetPrevBtn_, &presetNextBtn_, &presetSaveBtn_, &presetFolderBtn_}) addAndMakeVisible(*b);
    addAndMakeVisible(presetBox_);
    presetBox_.setTextWhenNothingSelected("PRESET");
    presetBox_.onOpen = [this] { presetBox_.setSelectedId(0, juce::dontSendNotification); boxSetId_ = 0; }; // see PresetCombo
    presetBox_.onChange = [this] {
        const int id = presetBox_.getSelectedId();
        boxSetId_ = id; // the pick is being handled now: refreshPresetBox() may update the box again
        bool ok = true;
        juce::String name;
        if (id == kInitId) proc_.loadInitPreset();
        else if (id >= kStarterId0 && id < kStarterId0 + WT8AudioProcessor::numStarterPresets()) ok = proc_.loadStarterPreset(id - kStarterId0);
        else if (id >= kUserId0 && id - kUserId0 < presetNames_.size()) { name = presetNames_[id - kUserId0]; ok = proc_.loadPreset(name); }
        else return;
        if (!ok)
        {
            // nothing changed (the previous preset and the sound are still as they were): say so; the box shows the previous one again below
            juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::NoIcon, "PRESET",
                                         "Could not load the preset \"" + name + "\". The file may have been moved or edited.", "OK", this), nullptr);
        }
        refreshPresetBox();
    };
    presetPrevBtn_.onClick = [this] { stepPreset(-1); };
    presetNextBtn_.onClick = [this] { stepPreset(+1); };
    presetSaveBtn_.onClick = [this] { promptSavePreset(); };
    presetFolderBtn_.onClick = [this] { auto f = proc_.getPresetFolder(); f.createDirectory(); f.revealToUser(); };
    refreshPresetList();

    addAndMakeVisible(grid_);

    setResizable(true, true);
    setResizeLimits(630, int(630.0 * kWindowH / kWindowW), 1260, int(1260.0 * kWindowH / kWindowW)); // 630 x 728 .. 1260 x 1455 (v0.9-v0.14: 840 x 898; v0.15: one more row, 840 x 934; v0.16: one more, 840 x 970)
    getConstrainer()->setFixedAspectRatio((double) kWindowW / kWindowH);
    setSize(kWindowW, kWindowH);
    grid_.refresh();
    timerCallback(); // fill in the transpose readout and scale state straight away
    // v0.10: LOAD / RESET under the WAVETABLE knob
    for (auto* b : {&loadTableBtn_, &resetTableBtn_}) addAndMakeVisible(*b);
    addAndMakeVisible(tableView_);
    loadTableBtn_.onClick = [this] { loadTableFromFile(); };
    resetTableBtn_.onClick = [this] {
        proc_.resetUserTable(juce::jlimit(0, WT8AudioProcessor::kTableSlots - 1, juce::roundToInt(proc_.apvts.getRawParameterValue("table")->load()) - 1));
        refreshTableControls();
    };
    refreshTableControls();
#if WT8_MIDI_FX
    // MIDI effect build: only the sequencer is shown (the sound controls stay as parameters, but have no place here)
    for (auto* grp : {&row1_, &row2_})
        for (auto& g : *grp)
            for (auto* k : g.knobs) { k->slider.setVisible(false); k->name.setVisible(false); }
    for (juce::Component* c : std::initializer_list<juce::Component*>{&tableView_, &loadTableBtn_, &resetTableBtn_, &filterTypeBox_, &pitchShapeBox_, &filterShapeBox_,
                                                                       &presetBox_, &presetPrevBtn_, &presetNextBtn_, &presetSaveBtn_, &presetFolderBtn_})
        c->setVisible(false);
#endif
    startTimerHz(15);
}

void WT8Editor::syncViewFromProcessor()
{
    const int v = proc_.getUiView();
    if (v == shownView_) return;
    shownView_ = v;
    (v == 0 ? gridViewBtn_ : v == 1 ? ringViewBtn_ : twoRingsViewBtn_).setToggleState(true, juce::dontSendNotification);
    grid_.setView(v == 0 ? StepGrid::View::Grid : v == 1 ? StepGrid::View::Ring : StepGrid::View::TwoRings);
}

void WT8Editor::timerCallback()
{
    syncViewFromProcessor(); // a project was opened while the editor is showing
    grid_.refresh();
    refreshTableControls();
    recBtn_.setToggleState(proc_.isSeqRecording(), juce::dontSendNotification);
    playBtn_.setEnabled(syncBox_.getSelectedItemIndex() == 0); // in "Logic" sync, Logic's transport is the play button
    retrigBtn_.setEnabled(syncBox_.getSelectedItemIndex() == 0); // v0.13: like PLAY, a Free-sync control (in Logic sync the bar decides where step 1 is)
    if (loopEndBtn_.getToggleState() != proc_.getSlotAtLoopEnd()) loopEndBtn_.setToggleState(proc_.getSlotAtLoopEnd(), juce::dontSendNotification); // a project was opened
    pendBtn_.setEnabled(dirBox_.getSelectedItemIndex() == 2);  // end-repeat only applies to the pendulum

    refreshSlotButtons();
    refreshChainControls();
    refreshPresetBox(); // the "modified" marker follows the knobs (and the host's automation)
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

// ---- v0.10 user wavetables -------------------------------------------------------------------------------------------
void WT8Editor::refreshTableControls()
{
    const int slot = juce::jlimit(0, WT8AudioProcessor::kTableSlots - 1, juce::roundToInt(proc_.apvts.getRawParameterValue("table")->load()) - 1);
    const bool user = proc_.slotHasUserTable(slot);
    const juce::String label = user ? "USER: " + proc_.userTableName(slot) : juce::String("WAVETABLE");
    if (label != shownTableLabel_ && tableKnob_ != nullptr)
    {
        shownTableLabel_ = label;
        tableKnob_->name.setText(label, juce::dontSendNotification);
        tableKnob_->name.setColour(juce::Label::textColourId, user ? kAccent : kDim);
    }
    resetTableBtn_.setEnabled(user);

    // v0.12: the picture follows the slot, the table's data and the FRAME knob (also when the host automates them)
    const int rev = proc_.tableRevision();
    if (slot != shownTableSlot_ || rev != shownTableRevision_)
    {
        std::vector<float> t;
        if (proc_.getTableData(slot, t)) tableView_.setTable(t);
        shownTableSlot_ = slot;
        shownTableRevision_ = rev;
    }
    const int frame = juce::roundToInt(proc_.apvts.getRawParameterValue("cycle")->load());
    if (frame != shownTableFrame_) { shownTableFrame_ = frame; tableView_.setFrame(frame); }
}

void TableView::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float s = juce::jmax(0.5f, r.getWidth() / 150.0f);
    g.setColour(kBg);
    g.fillRoundedRectangle(r, 4.0f * s);
    g.setColour(kEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f * s, 1.0f);
    constexpr int kFr = wtimport::kFrames, kLen = wtimport::kFrameSize;
    if (table_.size() < (size_t) wtimport::kTableFloats) return;
    const int cur = juce::jlimit(0, kFr - 1, frame_);

    auto inner = r.reduced(6.0f * s, 5.0f * s);
    auto top = inner.removeFromTop(inner.getHeight() * 0.56f);
    inner.removeFromTop(4.0f * s);
    const auto stack = inner;

    auto wavePath = [&](int frame, juce::Rectangle<float> box, float amp, int points) {
        juce::Path p;
        const float* d = &table_[(size_t) frame * kLen];
        for (int i = 0; i <= points; ++i)
        {
            const float v = juce::jlimit(-1.0f, 1.0f, d[(i * kLen / points) % kLen] / 0.95f);
            const float x = box.getX() + box.getWidth() * (float) i / (float) points;
            const float y = box.getCentreY() - v * amp;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        return p;
    };

    // the frame at the FRAME knob
    g.setColour(kEdge);
    g.drawHorizontalLine((int) top.getCentreY(), top.getX(), top.getRight());
    g.setColour(kAccent);
    g.strokePath(wavePath(cur, top, top.getHeight() * 0.46f, 512), juce::PathStrokeType(1.4f * s, juce::PathStrokeType::curved));
    g.setColour(kDim);
    g.setFont(makeFont(9.5f * s));
    g.drawText("FRAME " + juce::String(cur) + " / 32", top.toNearestInt().removeFromTop(int(12 * s)), juce::Justification::topLeft);

    // all 33 frames: frame 0 in front (bottom), frame 32 at the back (top), the current one lit
    const float amp = stack.getHeight() * 0.16f;
    const float step = (stack.getHeight() - 2.0f * amp) / (float) (kFr - 1);
    auto box = [&](int f) { return juce::Rectangle<float>(stack.getX(), stack.getBottom() - amp - (float) f * step - amp, stack.getWidth(), 2.0f * amp); };
    // back to front; each frame is filled with the background below its line, so the frames in front hide the ones behind
    for (int f = kFr - 1; f >= 0; --f)
    {
        auto p = wavePath(f, box(f), amp, 128);
        juce::Path filled(p);
        filled.lineTo(stack.getRight(), stack.getBottom());
        filled.lineTo(stack.getX(), stack.getBottom());
        filled.closeSubPath();
        g.setColour(kBg);
        g.fillPath(filled);
        g.setColour(f == cur ? kAccent : kDim.withAlpha(0.8f));
        g.strokePath(p, juce::PathStrokeType((f == cur ? 1.4f : 0.8f) * s));
    }
    // the current frame once more, on top, so the frames in front never hide it
    g.setColour(kAccent);
    g.strokePath(wavePath(cur, box(cur), amp, 128), juce::PathStrokeType(1.4f * s));
}

void WT8Editor::loadTableFromFile()
{
    const int slot = juce::jlimit(0, WT8AudioProcessor::kTableSlots - 1, juce::roundToInt(proc_.apvts.getRawParameterValue("table")->load()) - 1);
    tableChooser_ = std::make_unique<juce::FileChooser>("Load a wavetable (WAV file) into table " + juce::String(slot + 1),
                                                        juce::File(), "*.wav");
    juce::Component::SafePointer<WT8Editor> safe(this);
    tableChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [safe, slot](const juce::FileChooser& fc) {
        if (safe == nullptr) return;
        const juce::File f = fc.getResult();
        if (!f.existsAsFile()) return; // cancelled
        // v0.11: ask for the frame size before loading. Auto (the default) behaves exactly as in v0.10.
        auto* ask = new juce::AlertWindow("Load \"" + f.getFileNameWithoutExtension() + "\" into table " + juce::String(slot + 1),
                                          "FRAME SIZE: how many samples one frame of the file has. Leave it on Auto unless the table sounds like chopped-up audio "
                                          "(common sizes: 2048 for Serum / Vital, 256 or 512 for others).",
                                          juce::MessageBoxIconType::QuestionIcon);
        ask->addComboBox("frame", {"Auto", "256", "512", "1024", "2048", "4096", "8192"}, "Frame size (samples)");
        ask->getComboBoxComponent("frame")->setSelectedItemIndex(0);
        ask->addButton("LOAD", 1, juce::KeyPress(juce::KeyPress::returnKey));
        ask->addButton("CANCEL", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<juce::AlertWindow> askSafe(ask);
        ask->enterModalState(true, juce::ModalCallbackFunction::create([safe, askSafe, slot, f](int result) {
            if (result != 1 || safe == nullptr || askSafe == nullptr) return;
            const int idx = askSafe->getComboBoxComponent("frame")->getSelectedItemIndex();
            static const int sizes[] = {0, 256, 512, 1024, 2048, 4096, 8192};
            const int frameSize = sizes[juce::jlimit(0, 6, idx)];
            juce::String msg;
            const bool ok = safe->proc_.loadUserTable(slot, f, msg, frameSize);
            safe->refreshTableControls();
            juce::AlertWindow::showMessageBoxAsync(ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                   ok ? "Wavetable loaded" : "Could not load the wavetable",
                                                   ok ? "Table " + juce::String(slot + 1) + " now holds \"" + f.getFileNameWithoutExtension() + "\": " + msg + "."
                                                      : msg);
        }), true);
    });
}

// ---- v0.9 pattern slots --------------------------------------------------------------------------------------------
void SlotButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    juce::TextButton::paintButton(g, over, down);
    if (queued) // v0.13: waiting for the end of the loop: an amber outline
    {
        g.setColour(kAccent);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 2.0f);
    }
    if (filled) // a small dot under the number: this slot holds a pattern
    {
        const float r = juce::jmax(1.5f, getHeight() * 0.075f);
        g.setColour(getToggleState() ? kBg : kAccent);
        g.fillEllipse(getWidth() * 0.5f - r, getHeight() - r * 3.0f, r * 2.0f, r * 2.0f);
    }
}

void WT8Editor::refreshSlotButtons()
{
    const int cur = proc_.getPatternSlot();
    const int queued = proc_.getQueuedPatternSlot();
    for (int i = 0; i < WT8AudioProcessor::kPatternSlots; ++i)
    {
        const bool filled = proc_.patternSlotHasSteps(i);
        if (slotBtn_[i].filled != filled) { slotBtn_[i].filled = filled; slotBtn_[i].repaint(); }
        if (slotBtn_[i].queued != (i == queued)) { slotBtn_[i].queued = (i == queued); slotBtn_[i].repaint(); }
        if (slotBtn_[i].getToggleState() != (i == cur)) slotBtn_[i].setToggleState(i == cur, juce::dontSendNotification);
    }
    // an empty pattern has nothing to copy; if it became empty while COPY was armed (CLEAR), the arming is dropped
    const bool canCopy = proc_.patternSlotHasSteps(cur);
    if (copyBtn_.isEnabled() != canCopy) copyBtn_.setEnabled(canCopy);
    if (!canCopy && copyBtn_.getToggleState()) setCopyMode(false);
    // the label: COPY n TO while COPY is armed; NEXT n while a slot waits for the end of the loop; otherwise PATTERN
    const juce::String label = copyBtn_.getToggleState() ? "COPY " + juce::String(cur + 1) + " TO"
                             : queued >= 0               ? "NEXT " + juce::String(queued + 1)
                                                         : juce::String("PATTERN");
    if (patLabel_.getText() != label) patLabel_.setText(label, juce::dontSendNotification);
}

// v0.16: the CHAIN row follows the processor (a project that was opened, a chain that moved to another slot)
void WT8Editor::refreshChainControls()
{
    const int cur = proc_.getPatternSlot();
    const bool on = proc_.getChain();
    if (chainBtn_.getToggleState() != on) chainBtn_.setToggleState(on, juce::dontSendNotification);
    const juce::String btnText = on ? "ON" : "OFF";
    if (chainBtn_.getButtonText() != btnText) chainBtn_.setButtonText(btnText);
    const juce::String lbl = "SLOT " + juce::String(cur + 1) + " PLAYS";
    if (repeatLabel_.getText() != lbl) repeatLabel_.setText(lbl, juce::dontSendNotification);
    const int rep = proc_.getSlotRepeats(cur);
    if (repeatBox_.getSelectedId() != rep) repeatBox_.setSelectedId(rep, juce::dontSendNotification);
    // the order the chain follows: every slot that holds a pattern, with its pass count ("1 x2 > 3 x1 > ...", then back to the first)
    juce::String order;
    for (int i = 0; i < WT8AudioProcessor::kPatternSlots; ++i)
        if (proc_.patternSlotHasSteps(i)) order += (order.isEmpty() ? "" : "  >  ") + juce::String(i + 1) + " x" + juce::String(proc_.getSlotRepeats(i));
    if (order.isNotEmpty()) order += "  > ...";
    if (chainOrderLabel_.getText() != order) chainOrderLabel_.setText(order, juce::dontSendNotification);
    const auto col = on ? kText : kDim;
    if (chainOrderLabel_.findColour(juce::Label::textColourId) != col) chainOrderLabel_.setColour(juce::Label::textColourId, col);
}

void WT8Editor::setCopyMode(bool on)
{
    copyBtn_.setToggleState(on, juce::dontSendNotification);
    copyBtn_.setButtonText(on ? "CANCEL" : "COPY");
    patLabel_.setText(on ? "COPY " + juce::String(proc_.getPatternSlot() + 1) + " TO" : juce::String("PATTERN"), juce::dontSendNotification);
}

void WT8Editor::onSlotClicked(int slot)
{
    if (copyBtn_.getToggleState())
    {
        if (slot == proc_.getPatternSlot()) setCopyMode(false); // "copy to itself": just leave COPY
        else copyCurrentSlotTo(slot);
        return;
    }
    proc_.requestPatternSlot(slot); // v0.13: at once, or queued for the end of the loop, depending on the AT LOOP END switch
    grid_.refresh();
    refreshSlotButtons();
}

void WT8Editor::copyCurrentSlotTo(int target)
{
    const int from = proc_.getPatternSlot();
    auto doCopy = [this, from, target] {
        proc_.copyPatternSlot(from, target); // the current slot keeps playing; the copy waits in the target slot
        setCopyMode(false);
        refreshSlotButtons();
    };
    if (!proc_.patternSlotHasSteps(target)) { doCopy(); return; }
    juce::Component::SafePointer<WT8Editor> self(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOkCancel(
                                     juce::MessageBoxIconType::NoIcon, "REPLACE PATTERN?",
                                     "Pattern " + juce::String(target + 1) + " already holds a pattern. Replace it with a copy of pattern " + juce::String(from + 1) + "?",
                                     "REPLACE", "CANCEL", this),
                                 juce::ModalCallbackFunction::create([self, doCopy](int r) {
                                     if (self == nullptr) return;
                                     if (r == 1) doCopy(); else self->setCopyMode(false);
                                 }));
}

// ---- presets ----------------------------------------------------------------------------------------------------------
juce::String WT8Editor::presetItemText(int id) const
{
    if (id == kInitId) return "INIT (defaults)";
    if (id >= kStarterId0 && id < kStarterId0 + WT8AudioProcessor::numStarterPresets()) return WT8AudioProcessor::starterPresetName(id - kStarterId0);
    if (id >= kUserId0 && id - kUserId0 < presetNames_.size()) return presetNames_[id - kUserId0];
    return {};
}

void WT8Editor::refreshPresetList()
{
    presetNames_ = proc_.listPresets();
    presetBox_.clear(juce::dontSendNotification);
    boxSetId_ = 0; // (clear() left nothing selected)
    presetIds_.clear();
    presetBox_.addItem(presetItemText(kInitId), kInitId);
    presetIds_.push_back(kInitId);
    presetBox_.addSeparator();
    presetBox_.addSectionHeading("STARTER");
    for (int i = 0; i < WT8AudioProcessor::numStarterPresets(); ++i)
    {
        presetBox_.addItem(presetItemText(kStarterId0 + i), kStarterId0 + i);
        presetIds_.push_back(kStarterId0 + i);
    }
    if (presetNames_.size() > 0)
    {
        presetBox_.addSeparator();
        presetBox_.addSectionHeading("USER");
        for (int i = 0; i < presetNames_.size(); ++i)
        {
            presetBox_.addItem(presetItemText(kUserId0 + i), kUserId0 + i);
            presetIds_.push_back(kUserId0 + i);
        }
    }
    shownPresetId_ = -1; // the items were rebuilt: show the selection and marker again
    refreshPresetBox();
}

void WT8Editor::refreshPresetBox()
{
    if (presetBox_.isPopupActive()) return; // the list is open (PresetCombo cleared the selection on purpose): leave it alone
    if (presetBox_.getSelectedId() != boxSetId_) return; // the user has just picked an item: its onChange is on its way and refreshes the box itself
    // Which item the processor says is current (0 = none: a fresh editor, or after a project was loaded)
    int wantId = 0;
    switch (proc_.getCurrentPresetKind())
    {
        case WT8AudioProcessor::PresetKind::None: break;
        case WT8AudioProcessor::PresetKind::Init: wantId = kInitId; break;
        case WT8AudioProcessor::PresetKind::Starter:
            for (int i = 0; i < WT8AudioProcessor::numStarterPresets(); ++i)
                if (WT8AudioProcessor::starterPresetName(i) == proc_.getCurrentPresetName()) wantId = kStarterId0 + i;
            break;
        case WT8AudioProcessor::PresetKind::User:
        {
            const int idx = presetNames_.indexOf(proc_.getCurrentPresetName(), true); // (file names are not case sensitive on the Mac)
            if (idx >= 0) wantId = kUserId0 + idx; // a file that was removed from the folder meanwhile: nothing is shown
            break;
        }
    }
    const bool modified = wantId != 0 && proc_.isPresetModified();
    if (wantId == shownPresetId_ && modified == shownModified_ && presetBox_.getSelectedId() == wantId) return;

    // "* name" in amber = the sound has been changed since this preset was loaded (the marker leads, so a long name cannot cut it off)
    if (shownPresetId_ > 0 && shownModified_) presetBox_.changeItemText(shownPresetId_, presetItemText(shownPresetId_));
    shownPresetId_ = wantId;
    shownModified_ = modified;
    if (wantId != 0) presetBox_.changeItemText(wantId, (modified ? "* " : "") + presetItemText(wantId));
    presetBox_.setSelectedId(wantId, juce::dontSendNotification); // (0 shows "PRESET"; the call also refreshes the shown text)
    boxSetId_ = wantId;
    presetBox_.setColour(juce::ComboBox::textColourId, modified ? kAccent : kText);
}

void WT8Editor::stepPreset(int dir)
{
    refreshPresetList(); // (new files may have been added in the folder)
    const int n = (int) presetIds_.size();
    int cur = -1;
    for (int i = 0; i < n; ++i) if (presetIds_[(size_t) i] == presetBox_.getSelectedId()) cur = i;
    const int next = cur < 0 ? (dir > 0 ? 0 : n - 1) : ((cur + dir) % n + n) % n; // nothing shown yet: > goes to INIT, < to the last one
    presetBox_.setSelectedId(presetIds_[(size_t) next], juce::sendNotificationSync);
}

void WT8Editor::promptSavePreset()
{
    auto* w = new juce::AlertWindow("SAVE PRESET", "Name for this sound (the filter, oscillator, envelope, LFO and effect settings):",
                                    juce::MessageBoxIconType::NoIcon, this);
    // suggest the name of the user preset that is current (saving then replaces it); for INIT / a starter preset the field starts empty
    w->addTextEditor("name", proc_.getCurrentPresetKind() == WT8AudioProcessor::PresetKind::User ? proc_.getCurrentPresetName() : juce::String());
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
        refreshPresetList(); // the saved preset is now the current one (the processor says so), so the box selects it
    };
    if (proc_.listPresets().contains(WT8AudioProcessor::presetStem(name), true))
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
    g.drawText(WT8_MIDI_FX ? "dotriaconta-tone" : "ipmohc", juce::Rectangle<int>(int(20 * scale), 0, int(300 * scale), int(48 * scale)),
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
    if (!WT8_MIDI_FX) { for (auto& grp : row1_) drawGroup(grp); for (auto& grp : row2_) drawGroup(grp); }
    Group seqGroup; seqGroup.title = "SEQUENCER"; seqGroup.bounds = seqBounds_;
    drawGroup(seqGroup);
    g.setColour(kEdge.brighter(0.25f));
    for (auto& d : seqDividers_) g.fillRect(d);
}

void WT8Editor::resized()
{
    const float scale = getWidth() / 840.0f;
    const int margin = int(14 * scale);
    const int gap = int(10 * scale);
    const int headerH = int(48 * scale);

    auto full = getLocalBounds().withTrimmedTop(headerH).reduced(margin, 0);
    auto area = full.removeFromTop(int(kKnobRowsH * scale)); // the two knob rows (none in the MIDI effect build)
    const int rowH = (area.getHeight() - gap) / 2;

    auto layoutRow = [&](std::vector<Group>& row, juce::Rectangle<int> r)
    {
        int total = 0;
        for (auto& g : row) total += (int) g.knobs.size() + g.extraCells;
        const int usable = r.getWidth() - gap * ((int) row.size() - 1);
        int x = r.getX();
        for (size_t i = 0; i < row.size(); ++i)
        {
            auto& grp = row[i];
            const int cells = (int) grp.knobs.size() + grp.extraCells;
            const int w = (i + 1 == row.size()) ? r.getRight() - x
                                                : usable * cells / total;
            grp.bounds = {x, r.getY(), w, r.getHeight()};
            auto inner = grp.bounds.withTrimmedTop(int(24 * scale)).reduced(int(4 * scale), int(4 * scale));
            const int cellW = inner.getWidth() / cells;
            if (grp.extraCells > 0) // v0.12: the table picture takes the cells after the knobs
                tableView_.setBounds(juce::Rectangle<int>(inner.getX() + (int) grp.knobs.size() * cellW, inner.getY(),
                                                          inner.getRight() - (inner.getX() + (int) grp.knobs.size() * cellW), inner.getHeight())
                                         .reduced(int(4 * scale), int(2 * scale)));
            for (size_t j = 0; j < grp.knobs.size(); ++j)
            {
                auto cell = inner.withX(inner.getX() + (int) j * cellW).withWidth(cellW);
                const int labelH = int(16 * scale);
                grp.knobs[j]->name.setBounds(cell.removeFromTop(labelH));
                grp.knobs[j]->name.getProperties().set("fontHeight", (cellW < int(72 * scale) ? 9.5f : 11.0f) * scale); // v0.19: the narrower cells of the 4-group row get a slightly smaller label
                const int textH = int(18 * scale);
                grp.knobs[j]->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, juce::jmax(48, int(cellW * 0.9f)), textH);
                grp.knobs[j]->slider.setBounds(cell);
            }
            x += w + gap;
        }
    };

    auto row1 = area.removeFromTop(rowH);
    area.removeFromTop(gap);
#if !WT8_MIDI_FX
    layoutRow(row1_, row1);
    layoutRow(row2_, area);
#endif

    // v0.10: LOAD / RESET sit under the WAVETABLE knob (its slider gives up the space)
    if (!WT8_MIDI_FX && tableKnob_ != nullptr)
    {
        auto b = tableKnob_->slider.getBounds();
        auto strip = b.removeFromBottom(int(26 * scale));
        strip.setWidth(strip.getWidth() + int(22 * scale)); // v0.20: the top row's cells are narrower, so the pair may reach a little into the free space to its right
        tableKnob_->slider.setBounds(b);
        const int bw = (strip.getWidth() - int(6 * scale)) / 2;
        loadTableBtn_.setBounds(strip.removeFromLeft(bw).reduced(0, int(1 * scale)));
        strip.removeFromLeft(int(6 * scale));
        resetTableBtn_.setBounds(strip.reduced(0, int(1 * scale)));
    }

    // v0.19: the filter TYPE box sits under CUTOFF / RESONANCE
    if (!WT8_MIDI_FX && filterKnobs_[0] != nullptr)
    {
        juce::Rectangle<int> strips[2];
        for (int n = 0; n < 2; ++n)
        {
            auto b = filterKnobs_[n]->slider.getBounds();
            strips[n] = b.removeFromBottom(int(28 * scale));
            filterKnobs_[n]->slider.setBounds(b);
        }
        filterTypeBox_.setBounds(strips[0].getUnion(strips[1]).reduced(int(6 * scale), int(2 * scale)));
    }

    // v0.17: the LFO shape boxes sit under the knob pairs (the four knobs give up the bottom strip)
    if (!WT8_MIDI_FX && lfoKnobs_[0] != nullptr)
    {
        juce::Rectangle<int> strips[4];
        for (int n = 0; n < 4; ++n)
        {
            auto b = lfoKnobs_[n]->slider.getBounds();
            strips[n] = b.removeFromBottom(int(28 * scale));
            lfoKnobs_[n]->slider.setBounds(b);
        }
        const int pad = int(6 * scale);
        pitchShapeBox_.setBounds(strips[0].getUnion(strips[1]).reduced(pad, int(2 * scale)));
        filterShapeBox_.setBounds(strips[2].getUnion(strips[3]).reduced(pad, int(2 * scale)));
    }

    // ---- sequencer strip ----
    if (!WT8_MIDI_FX) full.removeFromTop(gap);
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

    for (auto* l : {&syncLabel_, &divLabel_, &laneLabel_, &dirLabel_, &scaleLabel_, &rootLabel_, &octLabel_, &patLabel_, &rowEditLabel_, &rowPlayLabel_, &rowChainLabel_, &repeatLabel_, &chainOrderLabel_})
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

    // v0.15 layout: six rows, one job per row, every row's controls start at the same x after a 66 px caption.
    seqDividers_.clear();
    juce::Rectangle<int> controls;
    auto nextRow = [&] { controls = inner.removeFromTop(int(30 * scale)); inner.removeFromTop(int(6 * scale)); };
    auto put = [&](juce::Component& c, int w) { c.setBounds(controls.removeFromLeft(int(w * scale))); controls.removeFromLeft(int(6 * scale)); };
    // a thin line between two control groups; w = total width taken (the line sits in the middle of it)
    auto div = [&](int w) { seqDividers_.push_back({controls.getX() + int(w * scale) / 2, controls.getY() + int(4 * scale), 1, int(22 * scale)}); controls.removeFromLeft(int(w * scale)); };

    nextRow(); // EDIT: pattern editing, RANDOM on its own
    put(rowEditLabel_, 66);
    put(recBtn_, 46); put(restBtn_, 46); put(delBtn_, 42); put(clearBtn_, 50); div(8); put(randomBtn_, 58);

    nextRow(); // PLAY: transport, restart, timing, and when a slot switch happens
    put(rowPlayLabel_, 66);
    put(playBtn_, 52); put(muteBtn_, 52); put(retrigBtn_, 56); div(8);
    put(syncLabel_, 34); put(syncBox_, 78); put(divLabel_, 34); put(divBox_, 72); div(8);
    put(loopEndBtn_, 84);

    nextRow(); // DIRECTION: how the pattern is walked, and the octave jump
    put(dirLabel_, 66); put(dirBox_, 92); put(pendBtn_, 62); div(8);
    put(octLabel_, 62); put(octBox_, 140);

    nextRow(); // SCALE: which scale the notes are snapped to, and the transpose from MIDI in
    put(scaleLabel_, 66); put(scaleBox_, 170); put(rootLabel_, 34); put(rootBox_, 52); div(8);
    put(xposeBtn_, 82); put(xposeReadout_, 52); put(xposeResetBtn_, 52);

    nextRow(); // LANE: which per-step value the grid shows and edits, and the view (the buttons are a little narrower to fit next to the caption)
    put(laneLabel_, 66); put(pitchLaneBtn_, 44); put(probLaneBtn_, 40); put(ratchLaneBtn_, 52); put(gateLaneBtn_, 42);
    put(accentLaneBtn_, 56); put(octLaneBtn_, 36); put(condLaneBtn_, 44); div(8);
    put(gridViewBtn_, 42); put(ringViewBtn_, 44); put(twoRingsViewBtn_, 60);

    // PATTERN (v0.9): the 16 pattern slots, and COPY
    controls = inner.removeFromTop(int(30 * scale));
    inner.removeFromTop(int(6 * scale));
    put(patLabel_, 66);
    for (auto& b : slotBtn_) { b.setBounds(controls.removeFromLeft(int(28 * scale))); controls.removeFromLeft(int(2 * scale)); }
    controls.removeFromLeft(int(4 * scale));
    put(copyBtn_, 56);

    // CHAIN (v0.16): on / off, how many passes the playing slot plays, and the order the chain follows
    nextRow();
    put(rowChainLabel_, 66);
    put(chainBtn_, 52);
    put(repeatLabel_, 100); put(repeatBox_, 62);
    chainOrderLabel_.setBounds(controls);

    grid_.setBounds(inner);
}
