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

// 32 clickable steps (2 rows of 16): click = note/rest, drag up/down or mouse wheel = pitch.
class StepGrid : public juce::Component
{
  public:
    explicit StepGrid(StepSequencer& s) : seq_(s) {}
    void refresh(); // pulls the pattern from the sequencer, repaints if anything changed
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

  private:
    int cellAt(juce::Point<int>) const;
    juce::Rectangle<float> cellBounds(int i) const;

    StepSequencer& seq_;
    SeqStep steps_[StepSequencer::kMaxSteps];
    int len_ = 0, playing_ = -1;
    int dragIdx_ = -1, dragStartY_ = 0, dragStartNote_ = 0;
    bool dragged_ = false;
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
    enum class Kind { Int, Octave, Pitch, Percent, Pan, Decibels };

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
    juce::ComboBox syncBox_, divBox_;
    juce::Label syncLabel_, divLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playAtt_, muteAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncAtt_, divAtt_;
    Knob* gateKnob_ = nullptr;
    juce::Rectangle<int> seqBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WT8Editor)
};
