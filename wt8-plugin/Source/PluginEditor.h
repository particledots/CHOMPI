#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>

// Look: dark matte panel, amber value arcs, small-caps labels. Pure JUCE drawing, no image assets.
class IpmohcLookAndFeel : public juce::LookAndFeel_V4
{
  public:
    IpmohcLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                          float startAngle, float endAngle, juce::Slider&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
};

// 32 clickable steps (2 rows of 16) with seven lanes. Which lane the grid shows and edits is chosen with the EDIT buttons:
//   PITCH:  click = note/rest, drag up/down or mouse wheel = pitch (in scale steps when a scale is set).
//   PROB:   drag up/down or wheel = that step's probability, double-click = back to 100%.
//   RATCH:  drag up/down or wheel = number of repeats (1-8), double-click = 1.
//   GATE:   drag up/down or wheel = that step's own gate, double-click = follow the GATE knob again.
//   ACCENT: click = accent on/off.
//   OCT:    drag up/down or wheel = chance (0-100 %) that the step jumps by the OCT JUMP setting, double-click = 0.
//   COND:   drag up/down or wheel = trigger condition ("pass 2 of every 3"), double-click = ALL (every pass).
class StepGrid : public juce::Component
{
  public:
    enum class Lane { Pitch, Prob, Ratchet, Gate, Accent, Oct, Cond };

    explicit StepGrid(StepSequencer& s) : seq_(s) {}
    void refresh(); // pulls the pattern from the sequencer, repaints if anything changed
    void setLane(Lane l) { if (l != lane_) { lane_ = l; repaint(); } }
    void setScale(int scale, int root) { scale_ = scale; root_ = root; } // scale -1 = off: pitch editing is chromatic
    void setGlobalGate(float g) { if (g != globalGate_) { globalGate_ = g; if (lane_ == Lane::Gate) repaint(); } } // 0.05..1, the GATE knob
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

  private:
    int cellAt(juce::Point<int>) const;
    juce::Rectangle<float> cellBounds(int i) const;
    int effectiveGate(const SeqStep&) const; // the gate % the step really uses (its own, or the GATE knob's)
    int laneValue(const SeqStep&) const;     // the number a drag starts from in the current lane
    void setLaneValue(int index, int value); // writes `value` into the current lane (no-op for PITCH / ACCENT)

    StepSequencer& seq_;
    SeqStep steps_[StepSequencer::kMaxSteps];
    int len_ = 0, playing_ = -1;
    int dragIdx_ = -1, dragStartY_ = 0, dragStartValue_ = 0;
    bool dragged_ = false;
    Lane lane_ = Lane::Pitch;
    int scale_ = -1, root_ = 0;
    float globalGate_ = 0.5f;
};

class WT8Editor : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit WT8Editor(WT8AudioProcessor&);
    ~WT8Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    void timerCallback() override;
    enum class Kind { Int, Octave, Pitch, Percent, Pan, Decibels, LoopLength, Seed, Swing };

    struct Knob
    {
        juce::Slider slider;
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    struct Group
    {
        juce::String title;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds; // filled in by resized()
    };

    Knob& addKnob(const juce::String& paramId, const juce::String& label, Kind kind, bool bipolar = false);
    void addGroup(const juce::String& title, std::initializer_list<Knob*> knobs);

    WT8AudioProcessor& proc_;
    IpmohcLookAndFeel laf_;

    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<Group> row1_, row2_;

    // sequencer strip
    StepGrid grid_;
    juce::TextButton recBtn_{"REC"}, restBtn_{"REST"}, delBtn_{"DEL"}, clearBtn_{"CLEAR"}, playBtn_{"PLAY"}, muteBtn_{"MUTE"};
    juce::TextButton pitchLaneBtn_{"PITCH"}, probLaneBtn_{"PROB"}, ratchLaneBtn_{"RATCH"}, gateLaneBtn_{"GATE"},
                     accentLaneBtn_{"ACCENT"}, octLaneBtn_{"OCT"}, condLaneBtn_{"COND"}, pendBtn_{"ENDS x2"};
    juce::TextButton xposeBtn_{"MIDI XPOSE"}, xposeResetBtn_{"RESET"};
    juce::ComboBox syncBox_, divBox_, dirBox_, scaleBox_, rootBox_, octBox_;
    juce::Label syncLabel_, divLabel_, laneLabel_, dirLabel_, scaleLabel_, rootLabel_, octLabel_, xposeReadout_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playAtt_, muteAtt_, pendAtt_, xposeAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncAtt_, divAtt_, dirAtt_, scaleAtt_, rootAtt_, octAtt_;
    std::vector<Knob*> seqKnobs_; // GATE PROB / LOOP SEED / SWING ACCENT (2 columns x 3 rows at the right of the strip)
    juce::Rectangle<int> seqBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WT8Editor)
};
