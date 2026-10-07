#include "WT8Engine.h"

// The WAVE engine headers (unmodified copies in Source/engine) expect libDaisy / DaisySP.
// Source/shim provides the few libDaisy pieces they use.
#include "daisysp.h"
#include "subtractiveEngine.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace
{
constexpr int kBlock = 24; // the firmware's audio block size; key requests are drained per block

constexpr int kPitchOffsetSemis = 12;
constexpr size_t kTableFloats = (size_t)WT8Engine::kCyclesPerTable * MAX_SAMPLES_PER_CYCLE;

// The firmware relies on statics being zero-initialised (SDRAM / BSS). Mirror that for heap objects.
template <typename T>
struct ZeroedDeleter
{
    void operator()(T* p) const
    {
        if (p) { p->~T(); std::free(p); }
    }
};
template <typename T>
std::unique_ptr<T, ZeroedDeleter<T>> makeZeroed()
{
    void* mem = std::calloc(1, sizeof(T));
    return std::unique_ptr<T, ZeroedDeleter<T>>(new (mem) T());
}
} // namespace

struct WT8Engine::Impl
{
    explicit Impl(double sr)
        : engine(makeZeroed<myEngine>()),
          reverb(makeZeroed<daisysp::Reverb>()),
          loader(makeZeroed<wavetableLoader>()),
          delayMem(delayMemSize((float)sr), chompi::InterpolatedDelayLine::AudioSample{0, 0}),
          tables((size_t)256 * MAX_SAMPLES_PER_CYCLE, 0.f), // 256 cycles max, same as firmware
          sampleRate((float)sr)
    {
        loader->wavetableMemory_ = reinterpret_cast<float(*)[MAX_SAMPLES_PER_CYCLE]>(tables.data());
        loader->numWavetables = 0;
        loader->index = 0;
    }

    void init()
    {
        engine->Init(sampleRate, delayMem.data(), reverb.get(), loader.get());
        applyAll(params, true);
    }

    void applyAll(const Params& p, bool force)
    {
        auto changed = [&](auto a, auto b) { return force || a != b; };

        if (changed(p.table, applied.table))
        {
            engine->nextTable((int8_t)p.table, true);
        }
        if (changed(p.cycle, applied.cycle))
            engine->setCycle((int8_t)p.cycle, true);
        if (force || p.octave != applied.octave)
        {
            engine->setOctave(p.octave - engine->getOctave());
        }
        if (changed(p.pitchSemis, applied.pitchSemis))
            engine->setGlobalPitch(0.5f + p.pitchSemis / 24.f);
        if (changed(p.attack, applied.attack))               engine->setAttack(p.attack);
        if (changed(p.release, applied.release))             engine->setRelease(p.release);
        if (changed(p.cutoff, applied.cutoff))               engine->setMasterCutoff(p.cutoff);
        if (changed(p.resonance, applied.resonance))         engine->setMasterResonance(p.resonance);
        if (changed(p.fxMacro, applied.fxMacro))             engine->setDelayFeedback(p.fxMacro);
        if (changed(p.delayTime, applied.delayTime))         engine->setDelayTime(p.delayTime);
        if (changed(p.pitchLfoDepth, applied.pitchLfoDepth)) engine->setPitchLfoDepth(p.pitchLfoDepth);
        if (changed(p.pitchLfoRate, applied.pitchLfoRate))   engine->setPitchLfoRate(p.pitchLfoRate);
        if (changed(p.filterLfoDepth, applied.filterLfoDepth)) engine->setLfoDepth(p.filterLfoDepth);
        if (changed(p.filterLfoRate, applied.filterLfoRate)) engine->setLfoRate(p.filterLfoRate);
        if (changed(p.gain, applied.gain))                   engine->setGain(p.gain);
        if (changed(p.pan, applied.pan))                     engine->setPan(p.pan);
        if (changed(p.comp, applied.comp))                   engine->setFinalComp(p.comp);
        outputGain = std::pow(10.f, p.outputDb / 20.f);
        applied = p;
    }

    void push(const KeyRequest& r)
    {
        while (engine->request_fifo.GetNumElements() >= 60)
            engine->Prepare();
        engine->request_fifo.PushBack(r);
    }

    std::unique_ptr<myEngine, ZeroedDeleter<myEngine>>                       engine;
    std::unique_ptr<daisysp::Reverb, ZeroedDeleter<daisysp::Reverb>>         reverb;
    std::unique_ptr<wavetableLoader, ZeroedDeleter<wavetableLoader>>         loader;
    std::vector<chompi::InterpolatedDelayLine::AudioSample>                  delayMem;
    std::vector<float>                                                       tables;
    float  sampleRate;
    Params params, applied;
    float  outputGain = 1.f;
    bool   initialised = false;

    void ensureInit()
    {
        if (!initialised) { init(); initialised = true; }
    }
};

WT8Engine::WT8Engine(double sampleRate) : impl_(new Impl(sampleRate)) {}
WT8Engine::~WT8Engine() = default;

bool WT8Engine::loadWavetable(int index, const void* wavData, size_t numBytes)
{
    if (index < 0 || index >= kNumTables || wavData == nullptr || numBytes < 44)
        return false;

    const auto* b = static_cast<const uint8_t*>(wavData);
    auto rd32 = [&](size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto rd16 = [&](size_t o) { uint16_t v; std::memcpy(&v, b + o, 2); return v; };

    if (std::memcmp(b, "RIFF", 4) != 0 || std::memcmp(b + 8, "WAVE", 4) != 0)
        return false;

    bool haveFmt = false;
    size_t pos = 12;
    while (pos + 8 <= numBytes)
    {
        const uint32_t sz = rd32(pos + 4);
        if (std::memcmp(b + pos, "fmt ", 4) == 0 && pos + 8 + 16 <= numBytes)
        {
            // IEEE float, 1 channel, 32 bit
            haveFmt = rd16(pos + 8) == 3 && rd16(pos + 10) == 1 && rd16(pos + 22) == 32;
        }
        else if (std::memcmp(b + pos, "data", 4) == 0)
        {
            const size_t need = kTableFloats * sizeof(float);
            if (!haveFmt || sz < need || pos + 8 + need > numBytes)
                return false;
            std::memcpy(&impl_->tables[(size_t)index * kTableFloats], b + pos + 8, need);
            if (index + 1 > impl_->loader->numWavetables)
                impl_->loader->numWavetables = (int16_t)(index + 1);
            return true;
        }
        pos += 8 + sz + (sz & 1);
    }
    return false;
}

void WT8Engine::setParams(const Params& p)
{
    impl_->params = p;
    if (!impl_->initialised)
        impl_->ensureInit(); // Init() applies impl_->params itself
    else
        impl_->applyAll(p, false);
}

void WT8Engine::noteOn(int midiNote, int midiVelocity, bool fromSequencer)
{
    impl_->ensureInit();
    // The firmware maps MIDI note -> transpose_nn = note - 60 and uses vel+1 on a 1..128 scale.
    // That puts MIDI 60 at 130.81 Hz (an octave below other synths); +12 makes MIDI 60 = 261.63 Hz.
    // Note-off matches on the MIDI key, so it needs no offset.
    impl_->push(KeyRequest(KeyRequest::Type::START, (float)(midiNote - 60 + kPitchOffsetSemis), midiNote,
                           (float)(midiVelocity + 1),
                           fromSequencer ? KeyRequest::Source::SEQUENCER : KeyRequest::Source::USER));
}

void WT8Engine::noteOff(int midiNote, bool fromSequencer)
{
    impl_->ensureInit();
    impl_->push(KeyRequest(KeyRequest::Type::STOP, (float)(midiNote - 60), midiNote, 127.f,
                           fromSequencer ? KeyRequest::Source::SEQUENCER : KeyRequest::Source::USER));
}

void WT8Engine::allNotesOff()
{
    impl_->ensureInit();
    impl_->engine->stopAllVoices();
}

void WT8Engine::render(float* left, float* right, int numSamples)
{
    impl_->ensureInit();

    float z[kBlock] = {};
    const float* in[4] = {z, z, z, z};
    float o0[kBlock], o1[kBlock], o2[kBlock], o3[kBlock];
    float* out[4] = {o0, o1, o2, o3};

    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min(kBlock, numSamples - done);

        // Firmware pops one key request per block; drain them all so chords stay together.
        while (!impl_->engine->request_fifo.IsEmpty())
            impl_->engine->Prepare();

        impl_->engine->Process(in, out, (size_t)n);

        for (int i = 0; i < n; ++i)
        {
            left[done + i]  = o0[i] * impl_->outputGain;
            right[done + i] = o1[i] * impl_->outputGain;
        }
        done += n;
    }
}
