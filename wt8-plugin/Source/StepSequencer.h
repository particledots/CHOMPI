// StepSequencer: JUCE-free 32-step sequencer for ipmohc, timed from the host's tempo.
//
// Modelled on the CHOMPI WAVE sequencer (record notes as steps, rests, loop, gate, mute) but clocked from
// the host (Logic) instead of the hardware timer. Notes come out as sample-accurate events for the audio
// thread to merge with live MIDI.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>

struct SeqStep
{
    uint8_t note = 60;
    uint8_t vel = 100;
    bool    rest = true;
    uint8_t prob = 100; // chance (0..100 %) that this step plays on each pass
};

struct SeqEvent
{
    int  offset; // sample offset inside the block
    bool on;
    int  note;
    int  vel;
};

struct SeqHostInfo
{
    bool   hostPlaying = false;
    bool   havePpq = false;
    double ppq = 0.0; // host position in quarter notes at the start of the block
    double bpm = 0.0; // <= 0: unknown (120 is used)
};

struct SeqSettings
{
    bool  play = false;       // "Free" sync: run from the Play button, from step 1, at the host tempo
    bool  followHost = false; // "Logic" sync: run while the host plays, locked to its bar position
    int   division = 2;       // see divisionName()
    float gate = 0.5f;        // 0.05..1 of the step length (1 = legato)
    bool  mute = false;
    bool  recording = false;  // step-record armed: sequence does not play

    // v0.4 playback options (the defaults reproduce v0.3 behaviour exactly)
    int   loopLen = 0;        // 0 = whole pattern; 1..32 = loop only the first N steps (clamped to the pattern length)
    int   direction = 0;      // 0 forward, 1 backward, 2 pendulum, 3 random
    bool  pendRepeat = false; // pendulum: the two end steps play twice (1-2-3-3-2-1) instead of once (1-2-3-2-1)
    float prob = 1.0f;        // global probability 0..1, multiplied with each step's own probability
    int   seed = 0;           // 0 = different every time playback starts; 1..n = the same random choices every time

    // v0.5 pitch handling, applied to each note as it is played (the recorded pattern is never changed).
    // The defaults (no scale, no transpose) reproduce v0.4 behaviour exactly.
    int   scale = -1;         // -1 = off; 0..kNumScales-1 = snap played notes to this scale (see scaleName())
    int   root = 0;           // scale root as a pitch class, 0 = C .. 11 = B
    int   transpose = 0;      // semitones added to every note BEFORE it is snapped to the scale
};

class StepSequencer
{
  public:
    static constexpr int kMaxSteps = 32;
    static constexpr int kNumDivisions = 8;
    static constexpr int kNumDirections = 4;
    static double divisionQuarterNotes(int division); // 0..3 = 1/4 1/8 1/16 1/32, 4..7 = triplets
    static const char* divisionName(int division);
    static const char* directionName(int direction);

    // ---- scales (v0.5). Same 28 scales, in the same order and with the same names, as the Scripter scripts ----
    static constexpr int kNumScales = 28;
    static const char* scaleName(int scale);
    /** True when MIDI note `note` is a tone of `scale` built on pitch class `root`. A scale outside 0..kNumScales-1 counts as "everything". */
    static bool inScale(int note, int root, int scale);
    /** Nearest scale tone to `note` (a note halfway between two tones snaps DOWN). Scale off / out of range returns `note` unchanged. */
    static int quantizeNote(int note, int root, int scale);
    /** Next scale tone strictly above (dir > 0) or below (dir < 0) `note`, for editing in scale steps. Stays put at the ends of the keyboard. */
    static int scaleStep(int note, int dir, int root, int scale);
    /** What actually sounds for a stored note: transposed, kept inside 0..127, then snapped to the scale. */
    static int playedNote(int note, int transpose, int root, int scale);

    /** Which pattern step plays at absolute step counter `k` (0 = bar start / first step). Pure function: the same
        inputs always give the same step, so playback does not depend on how the host chops audio into blocks. */
    static int stepIndexFor(long long k, int loopLength, int direction, bool pendulumRepeatEnds, uint64_t randomKey);

    StepSequencer();

    // ---- any thread (briefly takes a spin lock) ----
    int  length() const;
    void snapshot(SeqStep* out /*kMaxSteps*/, int& len, int& playingIdx) const;
    void clear();
    void deleteLast();
    void addRest();
    void setNote(int index, int note);   // keeps rest/velocity; does nothing past the end
    void setProb(int index, int percent); // 0..100; does nothing past the end or on a rest
    void toggleRest(int index);          // rest <-> note; past the end extends the pattern
    std::string serialize() const;
    void deserialize(const std::string& text);

    // ---- audio thread ----
    void recordNote(int note, int velocity);
    void resetTransport();
    /** True while the last processed block was running the pattern (PLAY on / host playing, pattern not empty, not recording). */
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }
    /** Produces the note events for the next `numSamples`. Returns how many were written to `out`. */
    int process(double sampleRate, int numSamples, const SeqHostInfo&, const SeqSettings&, SeqEvent* out, int maxOut);

  private:
    struct Lock;
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

    SeqStep steps_[kMaxSteps];
    int     len_ = 0;
    int     lastNote_ = 60;

    // transport / playback state (audio thread, under the lock)
    bool   wasRunning_ = false;
    double freePpq_ = 0.0;
    double expectedPpq_ = 0.0;
    bool   held_ = false;
    int    heldNote_ = 60;
    double gateOffPpq_ = -1.0;
    std::atomic<int> displayIdx_{-1};
    std::atomic<bool> running_{false};

    // random choices (probability, random direction) are a pure function of (key, step counter); with SEED off the
    // key is redrawn whenever playback starts or the host jumps, so every start sounds different
    uint64_t rngState_ = 0;
    uint64_t runKey_ = 0;
    uint64_t nextRandom();
};
