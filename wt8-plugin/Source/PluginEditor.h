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
};

class WT8Editor : public juce::AudioProcessorEditor
{
  public:
    explicit WT8Editor(WT8AudioProcessor&);
    ~WT8Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

  private:
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WT8Editor)
};
