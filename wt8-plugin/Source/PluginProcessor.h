#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <atomic>
#include "StepSequencer.h"

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

    // Step sequencer (edited from the editor, played from the audio thread)
    StepSequencer& sequencer() { return seq_; }
    bool isSeqRecording() const { return seqRecording_.load(); }
    void setSeqRecording(bool on) { seqRecording_.store(on); }

    // v0.5: pattern transpose in semitones, set by the last key played into the plugin while MIDI XPOSE is on
    // (the key C3 = 0). It stays until another key is played or RESET is pressed, and is saved with the project.
    int  getSeqTranspose() const { return seqTranspose_.load(); }
    void setSeqTranspose(int semitones) { seqTranspose_.store(juce::jlimit(-127, 127, semitones)); }

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadWavetables(WT8Engine& e);

    std::unique_ptr<WT8Engine> engine_;
    double sampleRate_ = 48000.0;
    bool engineReady_ = false;

    StepSequencer seq_;
    std::atomic<bool> seqRecording_{false};
    std::atomic<int> seqTranspose_{0};
    SeqEvent seqEvents_[64];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WT8AudioProcessor)
};
