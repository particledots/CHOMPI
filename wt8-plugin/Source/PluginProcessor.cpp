#include "PluginProcessor.h"
#include "WT8Engine.h"
#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
using juce::NormalisableRange;
using juce::AudioParameterFloat;
using juce::AudioParameterInt;

std::unique_ptr<AudioParameterFloat> f(const char* id, const char* name, float lo, float hi, float def)
{
    return std::make_unique<AudioParameterFloat>(juce::ParameterID{id, 1}, name, NormalisableRange<float>(lo, hi), def);
}
std::unique_ptr<AudioParameterInt> i(const char* id, const char* name, int lo, int hi, int def)
{
    return std::make_unique<AudioParameterInt>(juce::ParameterID{id, 1}, name, lo, hi, def);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout WT8AudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    // Ids are stable (saved in projects). Ranges/defaults follow the CHOMPI WAVE panel.
    l.add(i("table", "Wavetable", 1, WT8Engine::kNumTables, 1));
    l.add(i("cycle", "Frame", 0, WT8Engine::kMaxCycle, 0));
    l.add(i("octave", "Octave", -1, 1, 0));
    l.add(f("pitch", "Pitch (semitones)", -12.f, 12.f, 0.f));
    l.add(f("attack", "Attack", 0.f, 1.f, 0.f));
    l.add(f("release", "Release", 0.f, 1.f, 0.f));
    l.add(f("cutoff", "Filter Cutoff", 0.f, 1.f, 0.5f));
    l.add(f("resonance", "Filter Resonance", 0.f, 1.f, 0.63f));
    l.add(f("fx", "Delay <-> Reverb", 0.f, 1.f, 0.5f));
    l.add(f("fxtime", "Delay / Reverb Time", 0.f, 1.f, 0.4f));
    l.add(f("pitchlfodepth", "Pitch LFO Depth", 0.f, 1.f, 0.f));
    l.add(f("pitchlforate", "Pitch LFO Rate", 0.f, 1.f, 0.58f));
    l.add(f("filterlfodepth", "Filter LFO Depth", 0.f, 1.f, 0.f));
    l.add(f("filterlforate", "Filter LFO Rate", 0.f, 1.f, 0.58f));
    l.add(f("gain", "Gain", 0.f, 1.f, 0.84f));
    l.add(f("pan", "Pan", 0.f, 1.f, 0.5f));
    l.add(f("comp", "Compressor <-> Saturation", 0.f, 1.f, 0.f));
    l.add(f("output", "Output Boost (dB)", -12.f, 36.f, 20.f));
    return l;
}

WT8AudioProcessor::WT8AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "WT8", createLayout())
{
}

WT8AudioProcessor::~WT8AudioProcessor() = default;

bool WT8AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainInputChannelSet().isDisabled();
}

void WT8AudioProcessor::loadWavetables(WT8Engine& e)
{
    const char* data[] = {BinaryData::wavetable01_wav, BinaryData::wavetable02_wav, BinaryData::wavetable03_wav,
                          BinaryData::wavetable04_wav, BinaryData::wavetable05_wav, BinaryData::wavetable06_wav,
                          BinaryData::wavetable07_wav};
    const int sizes[] = {BinaryData::wavetable01_wavSize, BinaryData::wavetable02_wavSize,
                         BinaryData::wavetable03_wavSize, BinaryData::wavetable04_wavSize,
                         BinaryData::wavetable05_wavSize, BinaryData::wavetable06_wavSize,
                         BinaryData::wavetable07_wavSize};
    for (int t = 0; t < WT8Engine::kNumTables; ++t)
    {
        const bool ok = e.loadWavetable(t, data[t], (size_t) sizes[t]);
        jassert(ok);
        juce::ignoreUnused(ok);
    }
}

void WT8AudioProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    engineReady_ = false;
    auto fresh = std::make_unique<WT8Engine>(sampleRate);
    loadWavetables(*fresh);
    engine_ = std::move(fresh);
    engineReady_ = true;
}

void WT8AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (!engineReady_ || buffer.getNumChannels() < 2)
        return;

    WT8Engine::Params p;
    p.table          = (int) *apvts.getRawParameterValue("table") - 1;
    p.cycle          = (int) *apvts.getRawParameterValue("cycle");
    p.octave         = (int) *apvts.getRawParameterValue("octave");
    p.pitchSemis     = *apvts.getRawParameterValue("pitch");
    p.attack         = *apvts.getRawParameterValue("attack");
    p.release        = *apvts.getRawParameterValue("release");
    p.cutoff         = *apvts.getRawParameterValue("cutoff");
    p.resonance      = *apvts.getRawParameterValue("resonance");
    p.fxMacro        = *apvts.getRawParameterValue("fx");
    p.delayTime      = *apvts.getRawParameterValue("fxtime");
    p.pitchLfoDepth  = *apvts.getRawParameterValue("pitchlfodepth");
    p.pitchLfoRate   = *apvts.getRawParameterValue("pitchlforate");
    p.filterLfoDepth = *apvts.getRawParameterValue("filterlfodepth");
    p.filterLfoRate  = *apvts.getRawParameterValue("filterlforate");
    p.gain           = *apvts.getRawParameterValue("gain");
    p.pan            = *apvts.getRawParameterValue("pan");
    p.comp           = *apvts.getRawParameterValue("comp");
    p.outputDb       = *apvts.getRawParameterValue("output");
    engine_->setParams(p);

    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    // Render in segments so notes start at their sample-accurate position.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int evPos = juce::jlimit(0, numSamples, meta.samplePosition);
        if (evPos > pos)
        {
            engine_->render(left + pos, right + pos, evPos - pos);
            pos = evPos;
        }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            engine_->noteOn(m.getNoteNumber(), juce::jmax(1, (int) m.getVelocity()));
        else if (m.isNoteOff())
            engine_->noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            engine_->allNotesOff();
    }
    if (pos < numSamples)
        engine_->render(left + pos, right + pos, numSamples - pos);
}

juce::AudioProcessorEditor* WT8AudioProcessor::createEditor()
{
    return new WT8Editor(*this);
}

void WT8AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void WT8AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WT8AudioProcessor();
}
