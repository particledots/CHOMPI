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

    // v0.6 per-step expression. The defaults are neutral: a step that has never been touched plays exactly as in v0.5.
    uint8_t ratchet = 1;   // 1..8: the step is played as this many evenly spaced repeats
    uint8_t gate = 0;      // 0 = follow the GATE knob; 5..100 = this step's own gate in % of its length (100 = legato)
    bool    accent = false; // velocity boost by the ACCENT amount
    uint8_t octChance = 0; // 0..100 %: chance that this step jumps by the octave(s) set with OCT JUMP
    uint8_t condA = 1;     // trigger condition "play on pass condA of every condB passes" (1..condB of 1..8); 1 of 1 = always
    uint8_t condB = 1;
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

    // v0.6. The defaults (no swing, no flagged steps) reproduce v0.5 behaviour exactly.
    float swing = 50.f;       // 50..75 (%): MPC-style. 50 = straight, 66.7 = triplet feel, 75 = dotted. Every second step is delayed.
    float accent = 0.3f;      // 0..1: how much velocity an accented step gets on top of its own (1 = +127, capped at 127)
    int   octMode = 0;        // see octModeName(): which octave jump a step makes when its octave-jump roll succeeds
};

class StepSequencer
{
  public:
    static constexpr int kMaxSteps = 32;
    static constexpr int kNumDivisions = 8;
    static constexpr int kNumDirections = 4;
    static constexpr int kMaxRatchet = 8;
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

    // ---- v0.6: octave jumps and trigger conditions ----
    static constexpr int kNumOctModes = 6;
    static const char* octModeName(int mode);
    /** Semitones to add for a successful octave-jump roll. `r` is a uniform [0,1) number used to pick up or down in the "either" modes. */
    static int octaveShift(int mode, double r);
    /** Trigger conditions: index 0 = always (1 of 1), then 1:2 2:2, 1:3 2:3 3:3 ... up to 8:8. */
    static constexpr int kNumConditions = 36;
    static void conditionFromIndex(int index, int& a, int& b);
    static int conditionToIndex(int a, int b);
    /** Which pass through the loop step counter `k` belongs to (0 = the first). A pass is one full trip through the loop:
        N steps (forward, backward, random), 2N steps (pendulum with the ends repeated) or 2N-2 steps (pendulum). */
    static long long passIndex(long long k, int loopLength, int direction, bool pendulumRepeatEnds);
    /** v0.13: number of steps in one pass (see passIndex). A pass starts whenever the step counter is a multiple of this. */
    static long long passPeriod(int loopLength, int direction, bool pendulumRepeatEnds);
    /** True when "pass a of every b" lets the step play on pass `pass`. */
    static bool conditionPasses(int a, int b, long long pass);

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
    // v0.6 per-step values. Like setProb, all of these do nothing past the end of the pattern or on a rest.
    void setRatchet(int index, int repeats);        // 1..8
    void setStepGate(int index, int percent);       // 0 = follow the GATE knob, otherwise 5..100
    void toggleAccent(int index);
    void setOctChance(int index, int percent);      // 0..100
    void setCondition(int index, int a, int b);     // "pass a of every b": b 1..8, a 1..b
    void toggleRest(int index);          // rest <-> note; past the end extends the pattern
    /** v0.14 RANDOM: replaces the whole pattern with `steps` (1..kMaxSteps) random steps. Each step is a rest with a chance of
        `restPercent` (0..100), otherwise a note between `lowNote` and `highNote` (inclusive): when `scale` is 0..kNumScales-1 only tones
        of that scale (on `root`), otherwise any semitone. Velocity 100; every per-step value other than pitch/rest is neutral. The
        result always holds at least one note. The same `seed` gives the same pattern. Returns the number of notes. */
    int randomize(int steps, int lowNote, int highNote, int restPercent, int scale, int root, uint64_t seed);
    std::string serialize() const;
    void deserialize(const std::string& text);
    /** v0.8 (pattern slots): after the pattern was swapped, play again from step 1 (Free sync). A note that is sounding is
        not cut: it ends through its gate or when the next step starts. In Logic sync the position still comes from the host. */
    void restart();

    // ---- v0.13: STEP 1 (RETRIG) and loop-end pattern switching ----
    /** "Free" sync only. From the next step start on, the pattern plays from step 1 again (counters, passes and, with SEED off, the
        random choices start fresh). The step that is sounding is not cut: it, and its ratchet repeats, play out, and step 1 comes on
        the step boundary after, so the timing grid is kept (like the Octone, whose reset takes effect on the next clock). Ignored in
        Logic sync, and when the sequence is not running (a request never waits around for a later start). Any thread. */
    void requestStep1() { step1Pending_.store(true, std::memory_order_relaxed); }
    /** Queues a pattern (text as from serialize()) that replaces the live one when the current pass through the loop ends (the same
        "pass" the COND lane counts), tagged with the slot number the processor files it under. Replaces an earlier queue. The new
        pattern starts on its own step 1 at that moment. If the sequence is not running (stopped, recording, empty pattern) it is
        swapped in at the next audio block instead. Message thread. Returns false (nothing queued) while an earlier swap has not
        been collected with takeSwitch(). */
    bool queueSwitch(const std::string& text, int slot);
    /** Replaces the queued pattern's text, but only if `slot` is the queued slot. True if it was. (COPY into a queued slot.) */
    bool replaceQueued(int slot, const std::string& text);
    void cancelQueuedSwitch();
    int  queuedSlot() const { return queuedSlot_.load(std::memory_order_relaxed); } // -1 = nothing queued
    /** True when the audio thread has swapped in a queued pattern and the message thread has not collected it yet. */
    bool switchPending() const { return switchedFlag_.load(std::memory_order_relaxed); }
    /** Message thread. One atomic step under the sequencer's lock: writes the live pattern's text to `live` (if not null), cancels the
        queue if asked, and, if a queued pattern was swapped in since the last call, returns true with `outgoing` = the text of the
        pattern that was replaced and `newSlot` = the slot that is live now. */
    bool takeSwitch(std::string* live, std::string& outgoing, int& newSlot, bool cancelQueue);


    // ---- v0.16: slot chaining ----
    // With CHAIN on, the sequencer itself walks through the pattern slots: the slot that is playing plays `chainRepeat(slot)` passes
    // through its loop (the same "pass" the COND lane counts), then the next slot that holds a pattern (wrapping round after the last;
    // empty slots are skipped) starts on its step 1, on the step boundary where the last pass ended. It does that on the audio thread,
    // because only there is the loop end known to the sample, so it keeps its own copy of all 16 patterns (the "bank"). The processor
    // hands the bank over with setChainBank() whenever a slot changes, and brackets every slot change with holdChain(true / false) so the
    // audio thread cannot move to another slot half-way through. A queued switch (AT LOOP END) wins over the chain at the same boundary.
    static constexpr int kChainSlots = 16;
    static constexpr int kMaxRepeats = 16;
    void setChain(bool on) { chainOn_.store(on, std::memory_order_relaxed); }
    bool chainOn() const { return chainOn_.load(std::memory_order_relaxed); }
    void setChainRepeat(int slot, int passes);   // 1..kMaxRepeats (clamped); slots outside 0..15 are ignored
    int  chainRepeat(int slot) const;
    /** Message thread. `texts` = the 16 slots as text (as from serialize()); `liveSlot` = the slot the live pattern belongs to (its
        text is ignored: the live pattern is the truth). While `hold` is on the chain does not advance (it catches up at the next pass end). */
    /** Returns false, and changes nothing, while a swap the audio thread has made has not been collected (takeSwitch / takeSwitchAll):
        the caller's idea of which slot is live is out of date then. Collect, then call again. */
    bool setChainBank(const std::string* texts, int liveSlot);
    void holdChain(bool hold) { chainHold_.store(hold, std::memory_order_relaxed); }
    /** Like takeSwitch, but also fills `slotTexts` (16 strings) with every slot's text except the live one (left empty), which is what a
        message thread needs after the chain may have swapped several times since it last looked. `newSlot` = the live slot. */
    bool takeSwitchAll(std::string* live, std::string* slotTexts, int& newSlot, bool cancelQueue);
    int  chainLiveSlot() const;                  // the slot the audio thread believes is live (tests)

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
    // v0.6 ratchets: the repeats of the current step that have not started yet (repeat 0 is the step's own start)
    double ratBase_ = 0.0;   // host position (quarter notes) where the step started
    double ratSub_ = 0.0;    // length of one repeat
    double ratGate_ = 1.0;   // gate of each repeat as a fraction of its length (>= 0.999: legato)
    int    ratNext_ = 0;     // next repeat to start; none left when ratNext_ >= ratTotal_
    int    ratTotal_ = 0;
    int    ratVel_ = 100;
    int    ratNote_ = 60;
    std::atomic<int> displayIdx_{-1};
    std::atomic<bool> running_{false};

    // v0.13. kOff_: the step counter at which the current pattern started (STEP 1, or a loop-end switch); every use of the counter
    // for playback (which step, which pass, the random rolls) is relative to it, while swing keeps using the absolute counter so the
    // timing grid never moves. It is 0 whenever playback (re)starts or a Logic relocate happens.
    long long kOff_ = 0;
    std::atomic<bool> step1Pending_{false};
    SeqStep queued_[kMaxSteps];
    int     queuedLen_ = 0;
    std::atomic<int> queuedSlot_{-1};            // written under the lock; -1 = nothing queued
    SeqStep outgoing_[kMaxSteps];                // the pattern the audio thread replaced, until the message thread collects it
    int     outgoingLen_ = 0;
    int     switchedSlot_ = -1;
    bool    switched_ = false;                   // under the lock
    std::atomic<bool> switchedFlag_{false};      // a copy of switched_ that can be read without the lock


    // v0.16 chaining (all under the lock except the two atomics). bank_[chainCur_] is stale while that slot is live.
    struct BankSlot { SeqStep steps[kMaxSteps]; int len = 0; };
    BankSlot bank_[kChainSlots];
    int  chainCur_ = 0;
    int  chainRep_[kChainSlots];
    int  chainPass_ = -1;                        // passes of the live pattern that have ended while counting; -1 = counting has not started
    std::atomic<bool> chainOn_{false};
    std::atomic<bool> chainHold_{false};

    // random choices (probability, random direction) are a pure function of (key, step counter); with SEED off the
    // key is redrawn whenever playback starts or the host jumps, so every start sounds different
    uint64_t rngState_ = 0;
    uint64_t runKey_ = 0;
    uint64_t nextRandom();
};
