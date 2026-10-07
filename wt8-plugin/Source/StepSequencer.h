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

    // random choices (probability, random direction) are a pure function of (key, step counter); with SEED off the
    // key is redrawn whenever playback starts or the host jumps, so every start sounds different
    uint64_t rngState_ = 0;
    uint64_t runKey_ = 0;
    uint64_t nextRandom();
};
