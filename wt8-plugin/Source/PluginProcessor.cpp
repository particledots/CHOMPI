#include "PluginProcessor.h"
#include <random>
#include "WT8Engine.h"
#include "PluginEditor.h"
#include "StarterPresets.h"
#include "WavetableImport.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <BinaryData.h>

namespace
{
using juce::NormalisableRange;
using juce::AudioParameterFloat;
using juce::AudioParameterInt;

// `ver` is the AU parameter version hint. Logic identifies AU parameters by position, and JUCE orders them by
// version hint first, so parameters added in a later plugin version must use a higher number than every earlier one.
std::unique_ptr<AudioParameterFloat> f(const char* id, const char* name, float lo, float hi, float def, int ver = 1)
{
    return std::make_unique<AudioParameterFloat>(juce::ParameterID{id, ver}, name, NormalisableRange<float>(lo, hi), def);
}
std::unique_ptr<juce::AudioParameterBool> b(const char* id, const char* name, bool def, int ver = 1)
{
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, ver}, name, def);
}

std::unique_ptr<juce::AudioParameterChoice> c(const char* id, const char* name, const juce::StringArray& choices, int def, int ver = 1)
{
    return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, ver}, name, choices, def);
}

std::unique_ptr<AudioParameterInt> i(const char* id, const char* name, int lo, int hi, int def, int ver = 1)
{
    return std::make_unique<AudioParameterInt>(juce::ParameterID{id, ver}, name, lo, hi, def);
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

    // v0.4 sequencer playback options. Version hint 2: added after v0.3 shipped (see the note on `ver` above).
    juce::StringArray directions;
    for (int d = 0; d < StepSequencer::kNumDirections; ++d) directions.add(StepSequencer::directionName(d));
    l.add(i("seq_loop", "Seq Loop Length", 0, StepSequencer::kMaxSteps, 0, 2)); // 0 = whole pattern
    l.add(c("seq_dir", "Seq Direction", directions, 0, 2));
    l.add(b("seq_pendrep", "Seq Pendulum Repeat Ends", false, 2));
    l.add(f("seq_prob", "Seq Probability", 0.f, 1.f, 1.f, 2));
    l.add(i("seq_seed", "Seq Seed", 0, 99, 0, 2)); // 0 = random every time

    // v0.5 pitch handling. Version hint 3: added after v0.4 shipped, so Logic keeps the order of everything older.
    juce::StringArray scales, roots;
    scales.add("Off");
    for (int s = 0; s < StepSequencer::kNumScales; ++s) scales.add(StepSequencer::scaleName(s));
    for (const char* r : {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}) roots.add(r);
    l.add(c("seq_scale", "Seq Scale", scales, 0, 3));      // item 0 = Off, item n = scale n-1 of the Scripter list
    l.add(c("seq_root", "Seq Scale Root", roots, 0, 3));
    l.add(b("seq_xpose", "Seq MIDI Transpose", false, 3)); // keys played in set the pattern transpose (C3 = none)

    // v0.6 per-step expression settings. Version hint 4: added after v0.5 shipped. (The per-step values themselves
    // - ratchet, step gate, accent, octave-jump chance, trigger condition - live in the saved pattern, not here.)
    juce::StringArray octModes;
    for (int m = 0; m < StepSequencer::kNumOctModes; ++m) octModes.add(StepSequencer::octModeName(m));
    l.add(f("seq_swing", "Seq Swing", 50.f, 75.f, 50.f, 4));    // 50 = straight, 66.7 = triplet feel, 75 = dotted
    l.add(f("seq_accent", "Seq Accent", 0.f, 1.f, 0.3f, 4));    // velocity added to accented steps (1 = +127)
    l.add(c("seq_octmode", "Seq Octave Jump", octModes, 0, 4)); // which jump a step makes when its octave-jump roll succeeds
    return l;
}

WT8AudioProcessor::WT8AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "WT8", createLayout())
{
    for (auto& f : handoffFlag_) f.store(false);
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

bool WT8AudioProcessor::builtinTableData(int slot, std::vector<float>& out) const
{
    const char* data[] = {BinaryData::wavetable01_wav, BinaryData::wavetable02_wav, BinaryData::wavetable03_wav,
                          BinaryData::wavetable04_wav, BinaryData::wavetable05_wav, BinaryData::wavetable06_wav,
                          BinaryData::wavetable07_wav};
    const int sizes[] = {BinaryData::wavetable01_wavSize, BinaryData::wavetable02_wavSize,
                         BinaryData::wavetable03_wavSize, BinaryData::wavetable04_wavSize,
                         BinaryData::wavetable05_wavSize, BinaryData::wavetable06_wavSize,
                         BinaryData::wavetable07_wavSize};
    if (slot < 0 || slot >= kTableSlots) return false;
    return wtimport::readRawTable(reinterpret_cast<const uint8_t*>(data[slot]), (size_t) sizes[slot], out);
}

void WT8AudioProcessor::setSlotData(int slot, const std::vector<float>& data)
{
    tableRevision_.fetch_add(1);
    const juce::SpinLock::ScopedLockType sl(handoffLock_);
    handoff_[slot] = data;
    handoffFlag_[slot].store(true);
    handoffAny_.store(true);
}

void WT8AudioProcessor::applyPendingTables()
{
    if (!handoffAny_.load() || !handoffLock_.tryEnter())
        return; // busy: the next block picks it up
    for (int i = 0; i < kTableSlots; ++i)
        if (handoffFlag_[i].load())
        {
            engine_->setTableData(i, handoff_[i].data(), handoff_[i].size());
            handoffFlag_[i].store(false);
        }
    handoffAny_.store(false);
    handoffLock_.exit();
}

bool WT8AudioProcessor::slotHasUserTable(int slot) const
{
    if (slot < 0 || slot >= kTableSlots) return false;
    const juce::ScopedLock sl(userLock_);
    return !userTable_[slot].empty();
}

juce::String WT8AudioProcessor::userTableName(int slot) const
{
    if (slot < 0 || slot >= kTableSlots) return {};
    const juce::ScopedLock sl(userLock_);
    return userName_[slot];
}

bool WT8AudioProcessor::getTableData(int slot, std::vector<float>& out) const
{
    if (slot < 0 || slot >= kTableSlots) return false;
    {
        const juce::ScopedLock sl(userLock_);
        if (!userTable_[slot].empty()) { out = userTable_[slot]; return true; }
    }
    return builtinTableData(slot, out);
}

bool WT8AudioProcessor::loadUserTable(int slot, const juce::File& file, juce::String& message, int frameSize)
{
    if (slot < 0 || slot >= kTableSlots) { message = "No such table slot."; return false; }
    if (frameSize != 0 && (frameSize < 64 || frameSize > 16384)) { message = "The frame size must be between 64 and 16384 samples."; return false; }
    if (!file.existsAsFile()) { message = "The file does not exist."; return false; }
    if (file.getSize() > 64 * 1024 * 1024) { message = "The file is larger than 64 MB."; return false; }
    juce::MemoryBlock mb;
    if (!file.loadFileAsData(mb)) { message = "The file could not be read."; return false; }
    std::vector<float> mono, table;
    int sr = 0, clm = 0;
    std::string err, note;
    // a frame size chosen by the person wins over the file's own marker; 0 = Auto (marker, else the rules in WavetableImport.h)
    if (!wtimport::readWav(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), mono, sr, clm, err)
        || !wtimport::convertToTable(mono, frameSize > 0 ? frameSize : clm, table, note, err))
    {
        message = juce::String(err) + ". Only WAV files can be loaded.";
        return false;
    }
    {
        const juce::ScopedLock sl(userLock_);
        userTable_[slot] = table;
        userName_[slot] = file.getFileNameWithoutExtension().substring(0, 40);
    }
    setSlotData(slot, table);
    message = juce::String(note);
    return true;
}

void WT8AudioProcessor::resetUserTable(int slot)
{
    if (slot < 0 || slot >= kTableSlots) return;
    std::vector<float> builtin;
    {
        const juce::ScopedLock sl(userLock_);
        if (userTable_[slot].empty()) return;
        userTable_[slot].clear();
        userName_[slot] = juce::String();
    }
    if (builtinTableData(slot, builtin)) setSlotData(slot, builtin);
}

void WT8AudioProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    engineReady_ = false;
    auto fresh = std::make_unique<WT8Engine>(sampleRate);
    loadWavetables(*fresh);
    {
        // user tables go into the new engine straight away; anything still waiting in the hand-off is then out of date
        const juce::ScopedLock sl(userLock_);
        for (int i = 0; i < kTableSlots; ++i)
            if (!userTable_[i].empty()) fresh->setTableData(i, userTable_[i].data(), userTable_[i].size());
        const juce::SpinLock::ScopedLockType hl(handoffLock_);
        for (auto& fl : handoffFlag_) fl.store(false);
        handoffAny_.store(false);
    }
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

    applyPendingTables();

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
    ss.loopLen    = (int) *apvts.getRawParameterValue("seq_loop");
    ss.direction  = (int) *apvts.getRawParameterValue("seq_dir");
    ss.pendRepeat = *apvts.getRawParameterValue("seq_pendrep") > 0.5f;
    ss.prob       = *apvts.getRawParameterValue("seq_prob");
    ss.seed       = (int) *apvts.getRawParameterValue("seq_seed");
    const bool xposeOn = *apvts.getRawParameterValue("seq_xpose") > 0.5f;
    ss.scale      = (int) *apvts.getRawParameterValue("seq_scale") - 1; // choice 0 = Off -> -1
    ss.root       = (int) *apvts.getRawParameterValue("seq_root");
    ss.transpose  = xposeOn ? seqTranspose_.load() : 0;                 // switched off = the pattern plays as recorded
    ss.swing      = *apvts.getRawParameterValue("seq_swing");
    ss.accent     = *apvts.getRawParameterValue("seq_accent");
    ss.octMode    = (int) *apvts.getRawParameterValue("seq_octmode");
    const int numSeq = seq_.process(sampleRate_, numSamples, host, ss, seqEvents_, kSeqEventCapacity);

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
                const bool recording = seqRecording_.load();
                if (xposeOn && !recording)
                {
                    // MIDI XPOSE: the key sets the pattern transpose (C3 = 0). Recording takes priority: while REC is armed
                    // keys record steps as usual. The key only sounds when the pattern is not running.
                    seqTranspose_.store(juce::jlimit(-127, 127, m.getNoteNumber() - 60));
                    if (!seq_.isRunning()) engine_->noteOn(m.getNoteNumber(), vel);
                }
                else
                {
                    engine_->noteOn(m.getNoteNumber(), vel);
                    if (recording) seq_.recordNote(m.getNoteNumber(), vel); // step-record: each played note adds a step
                }
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
    apvts.state.setProperty("transpose", seqTranspose_.load(), nullptr);
    apvts.state.setProperty("slotLoopEnd", slotAtLoopEnd_.load() ? 1 : 0, nullptr); // v0.13; a project without it switches at once, as before
    {
        // v0.8 pattern slots: the current slot is "sequence"; the other slots that hold anything are saved as pat<n>.
        // A project saved before v0.8 has none of these and loads with its pattern in slot 1.
        // (v0.13: a queued switch that the audio thread has just carried out is collected first, in the same step as reading the
        // live pattern, so the pattern is always filed under the slot it belongs to. A switch still waiting in the queue is not saved.)
        const juce::ScopedLock sl(slotLock_);
        std::string live;
        syncSlotsLocked(&live, false);
        apvts.state.setProperty("sequence", juce::String(live), nullptr);
        slots_[slotCur_] = live;
        for (int i = 0; i < kPatternSlots; ++i)
        {
            const juce::Identifier id("pat" + juce::String(i));
            if (i != slotCur_ && !slots_[i].empty()) apvts.state.setProperty(id, juce::String(slots_[i]), nullptr);
            else apvts.state.removeProperty(id, nullptr);
        }
        apvts.state.setProperty("patCur", slotCur_, nullptr);
    }
    {
        // v0.10 user wavetables: utab<n> = the table's 67,584 floats (little-endian) as base64, utabn<n> = its name.
        // A project without them (older version, or no table loaded) comes back with the built-in tables.
        const juce::ScopedLock sl(userLock_);
        for (int i = 0; i < kTableSlots; ++i)
        {
            const juce::Identifier id("utab" + juce::String(i)), nid("utabn" + juce::String(i));
            if (!userTable_[i].empty())
            {
                apvts.state.setProperty(id, juce::Base64::toBase64(userTable_[i].data(), userTable_[i].size() * sizeof(float)), nullptr);
                apvts.state.setProperty(nid, userName_[i], nullptr);
            }
            else
            {
                apvts.state.removeProperty(id, nullptr);
                apvts.state.removeProperty(nid, nullptr);
            }
        }
    }
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void WT8AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            // (A parameter missing from an older saved state comes back at its default: JUCE's APVTS does that itself,
            // and plugin_test checks it, so a project saved by v0.3 plays as before.)
            {
                const juce::ScopedLock sl(slotLock_);
                // v0.13: whatever was queued belongs to the old project (cancel first, so no swap can land in the loaded pattern)
                std::string discardedText; int discardedSlot = 0;
                seq_.takeSwitch(nullptr, discardedText, discardedSlot, true);
                slotAtLoopEnd_.store((int) apvts.state.getProperty("slotLoopEnd", 0) != 0); // older projects have none: switch at once
                seq_.deserialize(apvts.state.getProperty("sequence").toString().toStdString());
                slotCur_ = juce::jlimit(0, kPatternSlots - 1, (int) apvts.state.getProperty("patCur", 0));
                for (int i = 0; i < kPatternSlots; ++i)
                    slots_[i] = i == slotCur_ ? std::string() : apvts.state.getProperty(juce::Identifier("pat" + juce::String(i))).toString().toStdString();
            }
            setSeqTranspose((int) apvts.state.getProperty("transpose", 0)); // states saved before v0.5 have none: 0
            for (int i = 0; i < kTableSlots; ++i)
            {
                // v0.10 user wavetables (see getStateInformation); anything missing or malformed leaves the built-in table
                std::vector<float> t;
                const juce::String text = apvts.state.getProperty(juce::Identifier("utab" + juce::String(i))).toString();
                if (text.isNotEmpty())
                {
                    juce::MemoryOutputStream mo;
                    if (juce::Base64::convertFromBase64(mo, text) && mo.getDataSize() == (size_t) wtimport::kTableFloats * sizeof(float))
                    {
                        t.resize((size_t) wtimport::kTableFloats);
                        std::memcpy(t.data(), mo.getData(), mo.getDataSize());
                        for (float& v : t) if (!std::isfinite(v)) v = 0.f;
                    }
                }
                if (!t.empty())
                {
                    {
                        const juce::ScopedLock ul(userLock_);
                        userTable_[i] = t;
                        userName_[i] = apvts.state.getProperty(juce::Identifier("utabn" + juce::String(i))).toString().substring(0, 40);
                    }
                    setSlotData(i, t);
                }
                else
                    resetUserTable(i);
            }
            {
                // v0.9: the sound came back with the project, not from a preset, so no preset is current any more
                const juce::ScopedLock sl(presetLock_);
                presetKind_ = PresetKind::None;
                presetName_ = juce::String();
                presetBaseline_.clear();
            }
            seq_.resetTransport();
            // Never start playing on its own just because a project was opened
            if (auto* play = apvts.getParameter("seq_play")) play->setValueNotifyingHost(0.f);
        }
}

// ---------------------------------------------------------------------------------------------------------------------
// v0.8 pattern slots
void WT8AudioProcessor::syncSlotsLocked(std::string* live, bool cancelQueue) const
{
    // The slot list can only be out of date when the audio thread has swapped a queued pattern in; asking costs nothing unless it has.
    if (live == nullptr && !cancelQueue && !seq_.switchPending()) return;
    std::string outgoing; int newSlot = slotCur_;
    if (seq_.takeSwitch(live, outgoing, newSlot, cancelQueue))
    {
        slots_[slotCur_] = outgoing;                        // the pattern that was playing goes back into its own slot
        slotCur_ = juce::jlimit(0, kPatternSlots - 1, newSlot); // and the queued slot is the live one now
    }
}

int WT8AudioProcessor::getPatternSlot() const
{
    const juce::ScopedLock sl(slotLock_);
    syncSlotsLocked(nullptr, false);
    return slotCur_;
}

bool WT8AudioProcessor::patternSlotHasSteps(int slot) const
{
    if (slot < 0 || slot >= kPatternSlots) return false;
    const juce::ScopedLock sl(slotLock_);
    syncSlotsLocked(nullptr, false);
    return slot == slotCur_ ? seq_.length() > 0 : !slots_[slot].empty();
}

void WT8AudioProcessor::selectPatternSlot(int slot)
{
    slot = juce::jlimit(0, kPatternSlots - 1, slot);
    const juce::ScopedLock sl(slotLock_);
    std::string live;
    syncSlotsLocked(&live, true); // a switch that was queued is dropped: this click decides
    if (slot == slotCur_) return;
    slots_[slotCur_] = live;
    slotCur_ = slot;
    seq_.deserialize(slots_[slot]); // swaps the steps under the sequencer's own lock; the audio thread just plays the new ones
    seq_.restart();
}

int WT8AudioProcessor::randomizePattern()
{
    const int loop = (int) *apvts.getRawParameterValue("seq_loop");
    const int steps = loop > 0 ? loop : (seq_.length() > 0 ? seq_.length() : 16);
    const int scale = (int) *apvts.getRawParameterValue("seq_scale") - 1; // choice 0 = Off -> -1
    const int root = (int) *apvts.getRawParameterValue("seq_root");
    std::random_device rd;
    const uint64_t seed = ((uint64_t) rd() << 32) ^ (uint64_t) rd();
    seq_.randomize(steps, 60, 84, 25, scale, root, seed);
    return juce::jlimit(1, StepSequencer::kMaxSteps, steps);
}

bool WT8AudioProcessor::copyPatternSlot(int from, int to)
{
    if (from < 0 || from >= kPatternSlots || to < 0 || to >= kPatternSlots || from == to) return false;
    const juce::ScopedLock sl(slotLock_);
    std::string live;
    syncSlotsLocked(&live, false);
    const std::string text = from == slotCur_ ? live : slots_[from]; // the current slot's truth is the live pattern
    if (seq_.replaceQueued(to, text)) // v0.13: copying into the slot that is waiting for the loop end: the waiting copy is the new one
    {
        slots_[to] = text;
        return true;
    }
    syncSlotsLocked(nullptr, false); // (the queued switch to `to` may have been carried out a moment ago)
    if (to == slotCur_)
    {
        seq_.deserialize(text); // same swap as selecting a slot: the audio thread just plays the new steps
        seq_.restart();
    }
    else
        slots_[to] = text;
    return true;
}

void WT8AudioProcessor::setSlotAtLoopEnd(bool on)
{
    const juce::ScopedLock sl(slotLock_);
    slotAtLoopEnd_.store(on);
    if (!on) syncSlotsLocked(nullptr, true); // back to "at once": nothing stays waiting
}

void WT8AudioProcessor::requestPatternSlot(int slot)
{
    slot = juce::jlimit(0, kPatternSlots - 1, slot);
    const juce::ScopedLock sl(slotLock_);
    syncSlotsLocked(nullptr, false);
    if (!slotAtLoopEnd_.load()) { selectPatternSlot(slot); return; }
    if (slot == slotCur_) { syncSlotsLocked(nullptr, true); return; } // the playing slot was clicked: cancel what was waiting
    if (!seq_.isRunning()) { selectPatternSlot(slot); return; }       // nothing is playing, so there is no loop end to wait for
    if (!seq_.queueSwitch(slots_[slot], slot))
    {
        syncSlotsLocked(nullptr, false);                              // an earlier swap had not been collected: collect it and look again
        if (slot == slotCur_) return;
        seq_.queueSwitch(slots_[slot], slot);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// v0.8 presets
const juce::StringArray& WT8AudioProcessor::presetParameterIds()
{
    // The sound: oscillator, envelope, filter, LFOs, effects and the compressor / saturation. Not included on purpose: GAIN,
    // PAN and BOOST (the level into the mixer, which a preset should not change), nor anything of the sequencer.
    static const juce::StringArray ids{"table", "cycle", "octave", "pitch", "attack", "release", "cutoff", "resonance", "fx",
                                       "fxtime", "pitchlfodepth", "pitchlforate", "filterlfodepth", "filterlforate", "comp"};
    return ids;
}

juce::File WT8AudioProcessor::getPresetFolder() const
{
    if (presetFolderOverride_ != juce::File()) return presetFolderOverride_;
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support"); // JUCE gives ~/Library on the Mac
#endif
    return base.getChildFile("particledots").getChildFile("ipmohc").getChildFile("Presets");
}

juce::String WT8AudioProcessor::presetFileName(const juce::String& name)
{
    const auto legal = juce::File::createLegalFileName(name.trim()).trim();
    return legal.isEmpty() ? juce::String() : legal + ".ipmohcpreset";
}

juce::String WT8AudioProcessor::presetStem(const juce::String& name)
{
    return presetFileName(name).upToLastOccurrenceOf(".ipmohcpreset", false, false);
}

juce::StringArray WT8AudioProcessor::listPresets() const
{
    juce::StringArray names;
    for (const auto& f : getPresetFolder().findChildFiles(juce::File::findFiles, false, "*.ipmohcpreset"))
        names.add(f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool WT8AudioProcessor::savePreset(const juce::String& name, juce::String& error)
{
    const auto fileName = presetFileName(name);
    if (fileName.isEmpty()) { error = "Please type a name."; return false; }
    const auto folder = getPresetFolder();
    if (!folder.createDirectory()) { error = "Could not create the preset folder:\n" + folder.getFullPathName(); return false; }

    juce::XmlElement xml("ipmohcPreset");
    xml.setAttribute("version", 1);
    xml.setAttribute("name", presetStem(name));
    for (const auto& id : presetParameterIds())
        if (auto* p = apvts.getParameter(id))
        {
            auto* e = xml.createNewChildElement("P");
            e->setAttribute("id", id);
            e->setAttribute("v", (double) p->convertFrom0to1(p->getValue()));
        }
    if (!folder.getChildFile(fileName).replaceWithText(xml.toString()))
    {
        error = "Could not write the file:\n" + folder.getChildFile(fileName).getFullPathName();
        return false;
    }
    markPresetCurrent(PresetKind::User, presetStem(name)); // the sound now equals this preset
    return true;
}

void WT8AudioProcessor::applyPresetValues(const std::map<juce::String, float>& values)
{
    for (const auto& id : presetParameterIds())
        if (auto* p = apvts.getParameter(id))
        {
            const auto it = values.find(id);
            const float real = it != values.end() ? it->second : p->convertFrom0to1(p->getDefaultValue());
            p->beginChangeGesture();
            p->setValueNotifyingHost(juce::jlimit(0.f, 1.f, p->convertTo0to1(real)));
            p->endChangeGesture();
        }
}

bool WT8AudioProcessor::loadPreset(const juce::String& name)
{
    const auto fileName = presetFileName(name);
    if (fileName.isEmpty()) return false;
    const auto xml = juce::parseXML(getPresetFolder().getChildFile(fileName));
    if (xml == nullptr || !xml->hasTagName("ipmohcPreset")) return false;
    std::map<juce::String, float> values;
    for (auto* e : xml->getChildWithTagNameIterator("P"))
        if (e->hasAttribute("id") && e->hasAttribute("v")) values[e->getStringAttribute("id")] = (float) e->getDoubleAttribute("v");
    applyPresetValues(values); // (a name or value this version does not know is ignored; one the file lacks gets its default)
    markPresetCurrent(PresetKind::User, presetStem(name));
    return true;
}

void WT8AudioProcessor::loadInitPreset()
{
    applyPresetValues({});
    markPresetCurrent(PresetKind::Init, juce::String());
}

// ---------------------------------------------------------------------------------------------------------------------
// v0.9 starter presets and the "modified" check
int WT8AudioProcessor::numStarterPresets() { return starter::kCount; }

juce::String WT8AudioProcessor::starterPresetName(int index)
{
    return "Starter " + juce::String(index + 1).paddedLeft('0', 2);
}

bool WT8AudioProcessor::loadStarterPreset(int index)
{
    if (index < 0 || index >= starter::kCount) return false;
    const int* r = starter::kRows[index];
    auto k = [](int v) { return (float) v * 0.001f; };
    std::map<juce::String, float> values;
    values["table"]          = (float) (r[2] + 1);   // the firmware counts tables from 0, the plugin's WAVETABLE knob from 1
    values["cycle"]          = (float) r[1];
    // the firmware's pitch knob is a stepped +/- 12 semitones around 0.5; round to the step (see StarterPresets.h)
    values["pitch"]          = (float) juce::jlimit(-12, 12, juce::roundToInt((k(r[0]) - 0.5f) * 24.0f));
    values["attack"]         = k(r[3]);
    values["pitchlfodepth"]  = k(r[4]);
    values["release"]        = k(r[5]);
    values["filterlfodepth"] = k(r[6]);
    values["cutoff"]         = k(r[7]);
    values["pitchlforate"]   = k(r[8]);
    values["fx"]             = k(r[9]);
    values["resonance"]      = k(r[10]);
    values["filterlforate"]  = k(r[11]);
    values["fxtime"]         = k(r[12]);
    // "octave" and "comp" are not in a firmware preset: left out, so applyPresetValues() puts them at their defaults
    applyPresetValues(values);
    markPresetCurrent(PresetKind::Starter, starterPresetName(index));
    return true;
}

void WT8AudioProcessor::markPresetCurrent(PresetKind kind, const juce::String& name)
{
    const auto& ids = presetParameterIds();
    std::vector<float> base;
    base.reserve((size_t) ids.size());
    for (const auto& id : ids)
    {
        auto* p = apvts.getParameter(id);
        base.push_back(p != nullptr ? p->getValue() : 0.f);
    }
    const juce::ScopedLock sl(presetLock_);
    presetKind_ = kind;
    presetName_ = name;
    presetBaseline_ = std::move(base);
}

WT8AudioProcessor::PresetKind WT8AudioProcessor::getCurrentPresetKind() const
{
    const juce::ScopedLock sl(presetLock_);
    return presetKind_;
}

juce::String WT8AudioProcessor::getCurrentPresetName() const
{
    const juce::ScopedLock sl(presetLock_);
    return presetName_;
}

bool WT8AudioProcessor::isPresetModified() const
{
    const juce::ScopedLock sl(presetLock_);
    if (presetKind_ == PresetKind::None) return false;
    const auto& ids = presetParameterIds();
    for (int i = 0; i < ids.size() && i < (int) presetBaseline_.size(); ++i)
        if (auto* p = apvts.getParameter(ids[i]))
            if (std::abs(p->getValue() - presetBaseline_[(size_t) i]) > 5.0e-4f) // (smaller than any step a knob or the host can make)
                return true;
    return false;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WT8AudioProcessor();
}
