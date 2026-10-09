#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <atomic>
#include <map>
#include <string>
#include <vector>
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

    // ---- v0.8 pattern slots: 16 patterns saved inside the project. The sequencer always plays the current slot; the other
    // slots are kept as text. Only the steps belong to a slot (not direction, loop, scale, swing... and not the transpose).
    // Message thread only (the editor); the audio thread never touches the slots, it only plays seq_.
    static constexpr int kPatternSlots = 16;
    int  getPatternSlot() const;
    bool patternSlotHasSteps(int slot) const;  // the current slot asks the live pattern, the others their saved text
    /** Saves the live pattern into the current slot, makes `slot` current and plays it from now on (an empty slot is silent). */
    void selectPatternSlot(int slot);
    /** v0.9: copies the pattern of slot `from` into slot `to`, replacing whatever `to` held. The current slot keeps playing as it
        is; only if `to` IS the current slot does the copy start playing (from step 1, as when a slot is selected). Returns false
        (and changes nothing) when a slot number is out of range or from == to. Message thread only, like selectPatternSlot. */
    bool copyPatternSlot(int from, int to);

    // ---- v0.8 presets: the SOUND settings (not the sequencer, the pattern, or the output level/pan) as small files in a
    // folder on the computer, so they are available in every project.
    static const juce::StringArray& presetParameterIds();     // the parameters a preset stores
    juce::File getPresetFolder() const;
    void setPresetFolder(const juce::File& f) { presetFolderOverride_ = f; } // tests use their own folder
    juce::StringArray listPresets() const;                    // names (no extension), sorted
    /** Writes `name` into the preset folder (replacing a preset of the same name). False with `error` filled in on failure. */
    bool savePreset(const juce::String& name, juce::String& error);
    /** Loads a preset by name: every sound parameter in the file is set, the ones it does not mention go to their defaults. */
    bool loadPreset(const juce::String& name);
    void loadInitPreset();                                    // all sound parameters back to their defaults
    static juce::String presetFileName(const juce::String& name); // the legal file name for a preset name (without the folder)
    static juce::String presetStem(const juce::String& name);     // the same without ".ipmohcpreset": the name the list shows

    // ---- v0.9 starter presets: compiled in, read-only (StarterPresets.h). Loading one sets the same 15 sound parameters as a
    // file preset does and leaves GAIN / PAN / BOOST and the sequencer alone.
    static int numStarterPresets();
    static juce::String starterPresetName(int index);         // "Starter 01" ...
    bool loadStarterPreset(int index);

    // ---- v0.9 "which preset is this, and has it been changed since": for the preset box. The preset that was last loaded (or saved)
    // in this session counts as current; its 15 sound parameters are remembered at that moment and isPresetModified() compares the
    // live ones with them (the output level and the sequencer are not part of a preset, so they never count). Nothing is current
    // after a project has been loaded: the sound came back with the project, not from a preset.
    enum class PresetKind { None, Init, Starter, User };
    PresetKind getCurrentPresetKind() const;
    juce::String getCurrentPresetName() const;                 // the starter / user preset's name; empty for None and Init
    bool isPresetModified() const;

    // ---- v0.10 user wavetables: any of the 7 table slots can hold a table loaded from a .wav file instead of the built-in one.
    // The loaded table is stored inside the project (about 270 KB per slot), so a project does not depend on the file any more.
    // A preset only remembers the TABLE number, not the table. Message thread, like the pattern slots.
    static constexpr int kTableSlots = 7;                      // = WT8Engine::kNumTables
    /** Reads `file` (a WAV), converts it to 33 x 2048 (WavetableImport.h) and puts it in `slot` (0..6). `message` says what
        was done, or why not; on failure nothing changes.
        v0.11: `frameSize` 0 = Auto (as in v0.10); 64..16384 says how many samples one frame of the file has, overriding any marker in it. */
    bool loadUserTable(int slot, const juce::File& file, juce::String& message, int frameSize = 0);
    void resetUserTable(int slot);                             // back to the built-in table
    bool slotHasUserTable(int slot) const;
    juce::String userTableName(int slot) const;                // the file name it came from, empty for a built-in table

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setSlotData(int slot, const std::vector<float>& data); // hands a table to the audio thread, which copies it into the engine
    bool builtinTableData(int slot, std::vector<float>& out) const;
    void applyPendingTables();                                  // audio thread: copies what setSlotData handed over (never waits)
    mutable juce::CriticalSection userLock_;                    // guards userTable_ / userName_
    std::vector<float> userTable_[kTableSlots];                 // empty = the built-in table
    juce::String userName_[kTableSlots];
    juce::SpinLock handoffLock_;                                // the audio thread only try-locks it
    std::vector<float> handoff_[kTableSlots];
    std::atomic<bool> handoffFlag_[kTableSlots];
    std::atomic<bool> handoffAny_{false};
    void loadWavetables(WT8Engine& e);

    std::unique_ptr<WT8Engine> engine_;
    double sampleRate_ = 48000.0;
    bool engineReady_ = false;

    StepSequencer seq_;
    mutable juce::CriticalSection slotLock_;            // guards slots_ / slotCur_ (never taken on the audio thread)
    std::string slots_[kPatternSlots];                  // saved text of each slot; slots_[slotCur_] is stale, seq_ holds it
    int slotCur_ = 0;
    juce::File presetFolderOverride_;
    void applyPresetValues(const std::map<juce::String, float>& values);
    void markPresetCurrent(PresetKind kind, const juce::String& name); // remembers the live sound parameters as this preset's values
    mutable juce::CriticalSection presetLock_;          // guards the four members below (message thread, but a host may restore state elsewhere)
    PresetKind presetKind_ = PresetKind::None;
    juce::String presetName_;
    std::vector<float> presetBaseline_;                 // normalised 0..1 values of presetParameterIds() when the preset became current
    std::atomic<bool> seqRecording_{false};
    std::atomic<int> seqTranspose_{0};
    // Room for the worst case a host can produce (very fast tempo, huge block, 1/32T steps with 8 repeats: ~540 events
    // in one block, see seq_test T27). An event that did not fit would be a lost note-off, i.e. a stuck note.
    static constexpr int kSeqEventCapacity = 2048;
    SeqEvent seqEvents_[kSeqEventCapacity];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WT8AudioProcessor)
};
