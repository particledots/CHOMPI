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

WT8Editor::WT8Editor(WT8AudioProcessor& p) : juce::AudioProcessorEditor(&p), proc_(p)
{
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

    setResizable(true, true);
    setResizeLimits(630, 330, 1470, 770);
    getConstrainer()->setFixedAspectRatio(840.0 / 440.0);
    setSize(840, 440);
}

WT8Editor::~WT8Editor()
{
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
}

void WT8Editor::resized()
{
    const float scale = getWidth() / 840.0f;
    const int margin = int(14 * scale);
    const int gap = int(10 * scale);
    const int headerH = int(48 * scale);

    auto area = getLocalBounds().withTrimmedTop(headerH).reduced(margin, 0).withTrimmedBottom(margin);
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

    layoutRow(row1_, area.removeFromTop(rowH));
    area.removeFromTop(gap);
    layoutRow(row2_, area);
}
