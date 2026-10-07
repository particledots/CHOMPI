// WT8Engine: JUCE-free wrapper around the CHOMPI WAVE synth engine.
//
// This is the only place the CHOMPI (Chase Bliss / CHOMPI Club, MIT) firmware code
// is touched. Everything above this file (the plugin) talks to this small API only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

class WT8Engine
{
  public:
    static constexpr int kNumTables = 7;       // wavetable01..07.wav
    static constexpr int kCyclesPerTable = 33; // frames per table
    static constexpr int kMaxCycle = kCyclesPerTable - 1;

    /** Every value here maps to one control on the CHOMPI WAVE front panel
     *  (0..1 knob values) except where noted. */
    struct Params
    {
        int   table          = 0;     // 0..6
        int   cycle          = 0;     // 0..32  (frame within the table)
        int   octave         = 0;     // -1..1
        float pitchSemis     = 0.f;   // -12..12 (panel "pitch" knob: +/- 1 octave)
        float attack         = 0.f;   // 0..1
        float release        = 0.f;   // 0..1
        float cutoff         = 0.5f;  // 0..1  (.5 = open/neutral)
        float resonance      = 0.63f; // 0..1
        float fxMacro        = 0.5f;  // 0..1  (0 = delay, .5 = dry, 1 = reverb)
        float delayTime      = 0.4f;  // 0..1  (also reverb time)
        float pitchLfoDepth  = 0.f;   // 0..1
        float pitchLfoRate   = 0.58f; // 0..1
        float filterLfoDepth = 0.f;   // 0..1
        float filterLfoRate  = 0.58f; // 0..1
        float gain           = 0.84f; // 0..1
        float pan            = 0.5f;  // 0..1
        float comp           = 0.f;   // 0..1  (<.5 compressor, >.5 saturation)
        float outputDb       = 20.f;  // plugin-only makeup gain, see notes in the README
    };

    explicit WT8Engine(double sampleRate);
    ~WT8Engine();

    /** Load one 32-bit-float WAV (33 cycles x 2048 samples) into table slot `index`.
     *  Returns false if the data isn't in the expected format. */
    bool loadWavetable(int index, const void* wavData, size_t numBytes);

    void setParams(const Params& p);

    // `fromSequencer` keeps sequencer notes separate from played ones, as in the firmware: a key is only
    // released once both sources have let go of it.
    void noteOn(int midiNote, int midiVelocity /*1..127*/, bool fromSequencer = false);
    void noteOff(int midiNote, bool fromSequencer = false);
    void allNotesOff();

    /** Overwrites left/right with `numSamples` of stereo output. */
    void render(float* left, float* right, int numSamples);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
