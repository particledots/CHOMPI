// ipmohc: small, dependency-free maps used by the filter code (kept apart so the tests can check them directly).
#pragma once
#include <cmath>

namespace chompi
{

/** v0.20: the resonance of the Low-pass / High-pass / Band-pass types, from the RESONANCE knob (0..0.99, after the engine's clamp).
 *  Same as the DJ filter's 0.95 * knob, plus a boost of up to 17.5 % that fades out toward the top of the knob: the filter core
 *  turns unstable at an effective resonance of about 1.0 (measured), and the top of the knob already gives 0.94. */
inline float singleFilterResonance(float knob)
{
    const float base = knob * .95f;
    float t = (knob - .6f) / .39f;
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    const float smooth = t * t * (3.f - 2.f * t);
    return base * (1.f + .175f * (1.f - smooth));
}

/** v0.20: the filter envelope's DECAY knob (0..1) as a time in seconds, exponential from 10 ms to 4 s. */
inline float filterEnvDecaySeconds(float knob)
{
    return .01f * std::pow(400.f, knob < 0.f ? 0.f : (knob > 1.f ? 1.f : knob));
}

} // namespace chompi
