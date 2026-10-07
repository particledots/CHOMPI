#include "PluginProcessor.h"
#include "WT8Engine.h"
#include "PluginEditor.h"
#include <limits>
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
std::unique_ptr<juce::AudioParameterBool> b(const char* id, const char* name, bool def)
{
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, def);
}

std::unique_ptr<juce::AudioParameterChoice> c(const char* id, const char* name, const juce::StringArray& choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, choices, def);
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

    // Step sequencer. (Ids are stable; new ones only ever get added.)
    juce::StringArray divisions;
    for (int d = 0; d < StepSequencer::kNumDivisions; ++d) divisions.add(StepSequencer::divisionName(d));
    l.add(b("seq_play", "Seq Play", false));
    l.add(c("seq_sync", "Seq Sync", juce::StringArray{"Free", "Logic"}, 0));
    l.add(c("seq_div", "Seq Step Length", divisions, 2));
    l.add(f("seq_gate", "Seq Gate", 0.05f, 1.f, 0.5f));
    l.add(b("seq_mute", "Seq Mute", false));
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
    seq_.resetTransport();
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

    // Sequencer: ask it which notes start/stop in this block (timed from the host's tempo and position).
    SeqHostInfo host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.hostPlaying = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) host.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { host.ppq = *ppq; host.havePpq = true; }
        }
    SeqSettings ss;
    ss.play       = *apvts.getRawParameterValue("seq_play") > 0.5f;
    ss.followHost = (int) *apvts.getRawParameterValue("seq_sync") == 1;
    ss.division   = (int) *apvts.getRawParameterValue("seq_div");
    ss.gate       = *apvts.getRawParameterValue("seq_gate");
    ss.mute       = *apvts.getRawParameterValue("seq_mute") > 0.5f;
    ss.recording  = seqRecording_.load();
    const int numSeq = seq_.process(sampleRate_, numSamples, host, ss, seqEvents_, 64);

    // Render in segments so every note starts at its sample-accurate position (MIDI + sequencer merged).
    int pos = 0;
    int si = 0;
    auto it = midi.begin();
    const auto itEnd = midi.end();
    auto renderTo = [&](int p) {
        if (p > pos) { engine_->render(left + pos, right + pos, p - pos); pos = p; }
    };
    while (it != itEnd || si < numSeq)
    {
        const int midiPos = it != itEnd ? juce::jlimit(0, numSamples, (*it).samplePosition) : std::numeric_limits<int>::max();
        const int seqPos = si < numSeq ? seqEvents_[si].offset : std::numeric_limits<int>::max();
        if (seqPos <= midiPos)
        {
            renderTo(seqPos);
            const auto& e = seqEvents_[si++];
            if (e.on) engine_->noteOn(e.note, e.vel, true);
            else      engine_->noteOff(e.note, true);
        }
        else
        {
            renderTo(midiPos);
            const auto m = (*it).getMessage();
            ++it;
            if (m.isNoteOn())
            {
                const int vel = juce::jmax(1, (int) m.getVelocity());
                engine_->noteOn(m.getNoteNumber(), vel);
                if (seqRecording_.load()) seq_.recordNote(m.getNoteNumber(), vel); // step-record: each played note adds a step
            }
            else if (m.isNoteOff())
                engine_->noteOff(m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                engine_->allNotesOff();
        }
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
    apvts.state.setProperty("sequence", juce::String(seq_.serialize()), nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void WT8AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            seq_.deserialize(apvts.state.getProperty("sequence").toString().toStdString());
            seq_.resetTransport();
            // Never start playing on its own just because a project was opened
            if (auto* play = apvts.getParameter("seq_play")) play->setValueNotifyingHost(0.f);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WT8AudioProcessor();
}
