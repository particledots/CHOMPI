#include "StepSequencer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <random>

struct StepSequencer::Lock
{
    explicit Lock(std::atomic_flag& f) : f_(f) { while (f_.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { f_.clear(std::memory_order_release); }
    std::atomic_flag& f_;
};

namespace
{
// splitmix64 finaliser: a cheap, well-mixed 64-bit hash
uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Uniform [0, 1) from (key, stream, step counter). Stateless, so the result never depends on block size.
double unitRandom(uint64_t key, uint64_t stream, long long k)
{
    const uint64_t h = mix64(mix64(key ^ (stream * 0xd1342543de82ef95ULL)) ^ (uint64_t) k);
    return (double) (h >> 11) * (1.0 / 9007199254740992.0); // 53 bits
}

long long posMod(long long a, long long m) { return ((a % m) + m) % m; }

// The scale table: the same 28 scales, in the same order and with the same names, as the Logic Scripter scripts
// ("The Quartet"), so a scale number means the same thing in both. Each scale is a 12-bit mask of its pitch classes.
constexpr unsigned M(std::initializer_list<int> intervals)
{
    unsigned m = 0;
    for (int i : intervals) m |= 1u << i;
    return m;
}

struct ScaleDef { const char* name; unsigned mask; };
constexpr ScaleDef kScaleTable[StepSequencer::kNumScales] = {
    {"Major (Ionian)",             M({0, 2, 4, 5, 7, 9, 11})},
    {"Dorian",                     M({0, 2, 3, 5, 7, 9, 10})},
    {"Phrygian",                   M({0, 1, 3, 5, 7, 8, 10})},
    {"Lydian",                     M({0, 2, 4, 6, 7, 9, 11})},
    {"Mixolydian",                 M({0, 2, 4, 5, 7, 9, 10})},
    {"Aeolian (Natural Minor)",    M({0, 2, 3, 5, 7, 8, 10})},
    {"Locrian",                    M({0, 1, 3, 5, 6, 8, 10})},
    {"Harmonic Minor",             M({0, 2, 3, 5, 7, 8, 11})},
    {"Melodic Minor",              M({0, 2, 3, 5, 7, 9, 11})},
    {"Harmonic Major",             M({0, 2, 4, 5, 7, 8, 11})},
    {"Hungarian Minor",            M({0, 2, 3, 6, 7, 8, 11})},
    {"Phrygian Dominant",          M({0, 1, 4, 5, 7, 8, 10})},
    {"Double Harmonic (Byzantine)", M({0, 1, 4, 5, 7, 8, 11})},
    {"Neapolitan Minor",           M({0, 1, 3, 5, 7, 8, 11})},
    {"Neapolitan Major",           M({0, 1, 3, 5, 7, 9, 11})},
    {"Enigmatic",                  M({0, 1, 4, 6, 8, 10, 11})},
    {"Major Pentatonic",           M({0, 2, 4, 7, 9})},
    {"Minor Pentatonic",           M({0, 3, 5, 7, 10})},
    {"Blues",                      M({0, 3, 5, 6, 7, 10})},
    {"Hirajoshi",                  M({0, 2, 3, 7, 8})},
    {"In Sen",                     M({0, 1, 5, 7, 10})},
    {"Kumoi",                      M({0, 2, 3, 7, 9})},
    {"Iwato",                      M({0, 1, 5, 6, 10})},
    {"Whole Tone",                 M({0, 2, 4, 6, 8, 10})},
    {"Octatonic (Whole-Half)",     M({0, 2, 3, 5, 6, 8, 9, 11})},
    {"Octatonic (Half-Whole)",     M({0, 1, 3, 4, 6, 7, 9, 10})},
    {"Prometheus",                 M({0, 2, 4, 6, 9, 10})},
    {"Chromatic",                  M({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11})},
};
} // namespace

const char* StepSequencer::scaleName(int scale)
{
    return kScaleTable[std::max(0, std::min(kNumScales - 1, scale))].name;
}

bool StepSequencer::inScale(int note, int root, int scale)
{
    if (scale < 0 || scale >= kNumScales) return true;
    const int pc = (int) posMod((long long) note - root, 12);
    return (kScaleTable[scale].mask >> pc) & 1u;
}

int StepSequencer::quantizeNote(int note, int root, int scale)
{
    note = std::max(0, std::min(127, note));
    if (scale < 0 || scale >= kNumScales) return note;
    for (int d = 0; d <= 12; ++d)
    {
        const int lo = note - d; // checked first, so a note exactly between two tones snaps down
        if (lo >= 0 && inScale(lo, root, scale)) return lo;
        const int hi = note + d;
        if (hi <= 127 && inScale(hi, root, scale)) return hi;
    }
    return note;
}

int StepSequencer::scaleStep(int note, int dir, int root, int scale)
{
    note = std::max(0, std::min(127, note));
    if (dir == 0) return note;
    const int step = dir > 0 ? 1 : -1;
    if (scale < 0 || scale >= kNumScales) return std::max(0, std::min(127, note + step));
    for (int n = note + step; n >= 0 && n <= 127; n += step)
        if (inScale(n, root, scale)) return n;
    return note;
}

int StepSequencer::playedNote(int note, int transpose, int root, int scale)
{
    return quantizeNote(std::max(0, std::min(127, note + transpose)), root, scale);
}

StepSequencer::StepSequencer()
{
    uint64_t seed = (uint64_t) std::chrono::steady_clock::now().time_since_epoch().count() ^ (uint64_t) (uintptr_t) this;
    try { std::random_device rd; seed ^= ((uint64_t) rd() << 32) ^ (uint64_t) rd(); } catch (...) {}
    rngState_ = mix64(seed) | 1ULL;
    runKey_ = mix64(rngState_ + 1);
}

uint64_t StepSequencer::nextRandom()
{
    rngState_ ^= rngState_ << 13; // xorshift64
    rngState_ ^= rngState_ >> 7;
    rngState_ ^= rngState_ << 17;
    return mix64(rngState_);
}

const char* StepSequencer::directionName(int d)
{
    static const char* n[kNumDirections] = {"Forward", "Backward", "Pendulum", "Random"};
    return n[std::max(0, std::min(kNumDirections - 1, d))];
}

int StepSequencer::stepIndexFor(long long k, int L, int direction, bool pendRepeat, uint64_t key)
{
    if (L <= 1) return 0;
    switch (direction)
    {
        case 1: // backward
            return L - 1 - (int) posMod(k, L);
        case 2: // pendulum: 1-2-3-2-1-2-3...  or, with repeated ends, 1-2-3-3-2-1-1-2-3...
        {
            const long long period = pendRepeat ? 2LL * L : 2LL * L - 2;
            const long long n = posMod(k, period);
            if (n < L) return (int) n;
            return (int) (pendRepeat ? 2LL * L - 1 - n : 2LL * L - 2 - n);
        }
        case 3: // random (a step can come up twice in a row)
            return std::min(L - 1, (int) (unitRandom(key, 1, k) * L));
        default: // forward
            return (int) posMod(k, L);
    }
}

double StepSequencer::divisionQuarterNotes(int d)
{
    static const double v[kNumDivisions] = {1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0};
    return v[std::max(0, std::min(kNumDivisions - 1, d))];
}

const char* StepSequencer::divisionName(int d)
{
    static const char* n[kNumDivisions] = {"1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/32T"};
    return n[std::max(0, std::min(kNumDivisions - 1, d))];
}

// ------------------------------------------------------------------------------------------------
int StepSequencer::length() const
{
    Lock l(lock_);
    return len_;
}

void StepSequencer::snapshot(SeqStep* out, int& len, int& playingIdx) const
{
    Lock l(lock_);
    for (int i = 0; i < kMaxSteps; ++i) out[i] = steps_[i];
    len = len_;
    playingIdx = displayIdx_.load(std::memory_order_relaxed);
}

void StepSequencer::clear()
{
    Lock l(lock_);
    for (auto& s : steps_) s = SeqStep();
    len_ = 0;
    displayIdx_.store(-1, std::memory_order_relaxed);
}

void StepSequencer::deleteLast()
{
    Lock l(lock_);
    if (len_ > 0) steps_[--len_] = SeqStep();
}

void StepSequencer::addRest()
{
    Lock l(lock_);
    if (len_ < kMaxSteps) { steps_[len_] = SeqStep(); ++len_; }
}

void StepSequencer::setNote(int index, int note)
{
    Lock l(lock_);
    if (index < 0 || index >= len_) return;
    steps_[index].note = (uint8_t) std::max(0, std::min(127, note));
    lastNote_ = steps_[index].note;
}

void StepSequencer::setProb(int index, int percent)
{
    Lock l(lock_);
    if (index < 0 || index >= len_ || steps_[index].rest) return;
    steps_[index].prob = (uint8_t) std::max(0, std::min(100, percent));
}

void StepSequencer::toggleRest(int index)
{
    Lock l(lock_);
    if (index < 0 || index >= kMaxSteps) return;
    while (len_ <= index) { steps_[len_] = SeqStep(); ++len_; } // extend with rests
    SeqStep& s = steps_[index];
    if (s.rest)
    {
        s.rest = false;
        if (s.note == 60 && lastNote_ != 60) s.note = (uint8_t) lastNote_;
        if (s.vel == 0) s.vel = 100;
    }
    else
    {
        s.rest = true;
    }
}

std::string StepSequencer::serialize() const
{
    Lock l(lock_);
    std::string out;
    for (int i = 0; i < len_; ++i)
    {
        // v0.3 format is "note:vel" or "r". v0.4 appends ":prob" (or "r:prob") only when the probability is not 100,
        // so a pattern that does not use probability is saved exactly as before.
        if (i) out += ',';
        const bool hasProb = steps_[i].prob != 100;
        if (steps_[i].rest) out += hasProb ? "r:" + std::to_string((int) steps_[i].prob) : std::string("r");
        else
        {
            out += std::to_string((int) steps_[i].note) + ":" + std::to_string((int) steps_[i].vel);
            if (hasProb) out += ":" + std::to_string((int) steps_[i].prob);
        }
    }
    return out;
}

void StepSequencer::deserialize(const std::string& text)
{
    Lock l(lock_);
    for (auto& s : steps_) s = SeqStep();
    len_ = 0;
    size_t pos = 0;
    while (pos <= text.size() && len_ < kMaxSteps && !text.empty())
    {
        size_t end = text.find(',', pos);
        if (end == std::string::npos) end = text.size();
        const std::string tok = text.substr(pos, end - pos);
        SeqStep s;
        if (!tok.empty() && tok[0] != 'r')
        {
            int note = 60, vel = 100, prob = 100;
            const int got = std::sscanf(tok.c_str(), "%d:%d:%d", &note, &vel, &prob);
            if (got >= 1)
            {
                s.note = (uint8_t) std::max(0, std::min(127, note));
                s.vel = (uint8_t) std::max(1, std::min(127, vel));
                s.prob = (uint8_t) std::max(0, std::min(100, prob)); // stays 100 when the token has no third field
                s.rest = false;
            }
        }
        else if (!tok.empty())
        {
            int prob = 100;
            if (std::sscanf(tok.c_str(), "r:%d", &prob) == 1) s.prob = (uint8_t) std::max(0, std::min(100, prob));
        }
        steps_[len_++] = s;
        pos = end + 1;
    }
    displayIdx_.store(-1, std::memory_order_relaxed);
}

// ------------------------------------------------------------------------------------------------
void StepSequencer::recordNote(int note, int velocity)
{
    Lock l(lock_);
    if (len_ >= kMaxSteps) return;
    SeqStep s;
    s.note = (uint8_t) std::max(0, std::min(127, note));
    s.vel = (uint8_t) std::max(1, std::min(127, velocity));
    s.rest = false;
    steps_[len_++] = s;
    lastNote_ = s.note;
}

void StepSequencer::resetTransport()
{
    Lock l(lock_);
    wasRunning_ = false;
    held_ = false;
    gateOffPpq_ = -1.0;
    displayIdx_.store(-1, std::memory_order_relaxed);
    running_.store(false, std::memory_order_relaxed);
}

int StepSequencer::process(double sr, int numSamples, const SeqHostInfo& h, const SeqSettings& s,
                           SeqEvent* out, int maxOut)
{
    Lock l(lock_);
    int n = 0;
    auto emit = [&](int offset, bool on, int note, int vel) {
        if (n < maxOut) out[n++] = SeqEvent{std::max(0, std::min(numSamples > 0 ? numSamples - 1 : 0, offset)), on, note, vel};
    };
    auto releaseHeld = [&](int offset) {
        if (held_) { emit(offset, false, heldNote_, 0); held_ = false; }
        gateOffPpq_ = -1.0;
    };

    bool running = false;
    if (!s.recording && len_ > 0)
        running = s.followHost ? (h.hostPlaying && h.havePpq) : s.play;

    running_.store(running, std::memory_order_relaxed);
    if (!running)
    {
        releaseHeld(0);
        wasRunning_ = false;
        displayIdx_.store(-1, std::memory_order_relaxed);
        return n;
    }

    if (s.mute) releaseHeld(0); // muting silences a sounding note straight away

    const double bpm = h.bpm > 0.0 ? h.bpm : 120.0;
    const double pps = bpm / 60.0 / sr; // quarter notes per sample
    if (!wasRunning_) runKey_ = nextRandom(); // SEED off: every start gets fresh random choices
    double ppq0;
    if (s.followHost)
    {
        ppq0 = h.ppq;
        if (wasRunning_ && std::fabs(ppq0 - expectedPpq_) > 0.02)
        {
            releaseHeld(0); // relocate / loop jump
            runKey_ = nextRandom(); // ...and so does every pass of a Logic cycle
        }
    }
    else
    {
        if (!wasRunning_) freePpq_ = 0.0; // start straight on step 1
        ppq0 = freePpq_;
    }
    const double ppq1 = ppq0 + numSamples * pps;
    const double stepLen = divisionQuarterNotes(s.division);
    const double eps = 1e-7;
    auto toOffset = [&](double p) { return (int) std::lround((p - ppq0) / pps); };

    // v0.4: the loop covers the first `loopLen` steps (0 = all); direction and probability use the same random key
    const int loop = (s.loopLen <= 0) ? len_ : std::max(1, std::min(s.loopLen, len_));
    const uint64_t key = s.seed > 0 ? mix64((uint64_t) s.seed) : runKey_;
    const double globalProb = std::max(0.0, std::min(1.0, (double) s.prob));

    long long k = (long long) std::ceil(ppq0 / stepLen - eps); // first step boundary at or after the block start
    for (;;)
    {
        const double b = (double) k * stepLen;
        const bool haveB = b < ppq1 - eps;
        const bool haveG = held_ && gateOffPpq_ >= 0.0 && gateOffPpq_ < ppq1 - eps;
        if (!haveB && !haveG) break;

        if (haveG && (!haveB || gateOffPpq_ < b))
        {
            emit(toOffset(gateOffPpq_), false, heldNote_, 0);
            held_ = false;
            gateOffPpq_ = -1.0;
            continue;
        }

        const int off = toOffset(b);
        if (held_) { emit(off, false, heldNote_, 0); held_ = false; gateOffPpq_ = -1.0; }
        const int idx = stepIndexFor(k, loop, s.direction, s.pendRepeat, key);
        displayIdx_.store(idx, std::memory_order_relaxed);
        const SeqStep& st = steps_[idx];
        bool fire = !st.rest && !s.mute;
        if (fire)
        {
            const double p = (st.prob / 100.0) * globalProb;
            if (p < 1.0 - 1e-9) fire = unitRandom(key, 2, k) < p; // a missed roll behaves like a rest
        }
        if (fire)
        {
            // v0.5: the note that sounds is the stored note, transposed, then snapped to the scale (if one is set)
            const int played = playedNote(st.note, s.transpose, s.root, s.scale);
            emit(off, true, played, st.vel);
            held_ = true;
            heldNote_ = played;
            gateOffPpq_ = s.gate < 0.999f ? b + (double) s.gate * stepLen : -1.0;
        }
        ++k;
    }

    if (!s.followHost) freePpq_ = ppq1;
    expectedPpq_ = ppq1;
    wasRunning_ = true;
    return n;
}
