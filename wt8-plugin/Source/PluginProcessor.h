#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

class WT8Engine;

class WT8AudioProcessor : public juce::AudioProcessor
{
  public:
    WT8AudioProcessor();
    ~WT8AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ipmohc"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadWavetables(WT8Engine& e);

    std::unique_ptr<WT8Engine> engine_;
    double sampleRate_ = 48000.0;
    bool engineReady_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WT8AudioProcessor)
};
