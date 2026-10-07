#include "StepSequencer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

struct StepSequencer::Lock
{
    explicit Lock(std::atomic_flag& f) : f_(f) { while (f_.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { f_.clear(std::memory_order_release); }
    std::atomic_flag& f_;
};

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
        if (i) out += ',';
        if (steps_[i].rest) out += 'r';
        else out += std::to_string((int) steps_[i].note) + ":" + std::to_string((int) steps_[i].vel);
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
            int note = 60, vel = 100;
            if (std::sscanf(tok.c_str(), "%d:%d", &note, &vel) >= 1)
            {
                s.note = (uint8_t) std::max(0, std::min(127, note));
                s.vel = (uint8_t) std::max(1, std::min(127, vel));
                s.rest = false;
            }
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
    double ppq0;
    if (s.followHost)
    {
        ppq0 = h.ppq;
        if (wasRunning_ && std::fabs(ppq0 - expectedPpq_) > 0.02) releaseHeld(0); // relocate / loop jump
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
        const int idx = (int) (((k % len_) + len_) % len_);
        displayIdx_.store(idx, std::memory_order_relaxed);
        const SeqStep& st = steps_[idx];
        if (!st.rest && !s.mute)
        {
            emit(off, true, st.note, st.vel);
            held_ = true;
            heldNote_ = st.note;
            gateOffPpq_ = s.gate < 0.999f ? b + (double) s.gate * stepLen : -1.0;
        }
        ++k;
    }

    if (!s.followHost) freePpq_ = ppq1;
    expectedPpq_ = ppq1;
    wasRunning_ = true;
    return n;
}
