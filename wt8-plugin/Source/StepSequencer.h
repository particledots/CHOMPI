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
};

class StepSequencer
{
  public:
    static constexpr int kMaxSteps = 32;
    static constexpr int kNumDivisions = 8;
    static double divisionQuarterNotes(int division); // 0..3 = 1/4 1/8 1/16 1/32, 4..7 = triplets
    static const char* divisionName(int division);

    // ---- any thread (briefly takes a spin lock) ----
    int  length() const;
    void snapshot(SeqStep* out /*kMaxSteps*/, int& len, int& playingIdx) const;
    void clear();
    void deleteLast();
    void addRest();
    void setNote(int index, int note);   // keeps rest/velocity; does nothing past the end
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
};
