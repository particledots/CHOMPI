// Minimal replacement for libDaisy's <daisy.h>.
// The CHOMPI WAVE engine only needs a handful of libDaisy utilities (FIFO and
// float<->int16 helpers). Everything hardware-related (SD card, audio driver,
// GPIO) is intentionally absent here.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include "util/FIFO.h"   // libDaisy (MIT), unmodified copy

namespace daisy
{
struct System
{
    static uint32_t GetNow() { return 0; } // only used by the hardware sequencer, not the engine
};
} // namespace daisy

// Conversions copied from libDaisy's daisy_core.h (MIT, (c) Electrosmith)
#define FBIPMAX 0.999985f
#define FBIPMIN (-FBIPMAX)
#define S162F_SCALE 3.0518509475997192e-05f
#define F2S16_SCALE 32767.0f

inline float s162f(int32_t x) { return (float)x * S162F_SCALE; }
inline int32_t f2s16(float x)
{
    x = x <= FBIPMIN ? FBIPMIN : x;
    x = x >= FBIPMAX ? FBIPMAX : x;
    return (int32_t)(x * F2S16_SCALE);
}
