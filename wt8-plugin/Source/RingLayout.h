// RingLayout: which ring and which slot show which step in the editor's ring views (v0.7).
// JUCE-free on purpose, so tests/seq_test.cpp can check it without a GUI. Display only: the pattern itself is always the
// plain list of steps 1..32 (here 0-based: step 0 = step 1), and the sequencer knows nothing about rings.
//
// A ring has 16 slots. Slot 0 is at 12 o'clock and the slots run clockwise: 4 = 3 o'clock, 8 = 6 o'clock, 12 = 9 o'clock.
#pragma once

namespace RingLayout
{
constexpr int kSlots = 16;
constexpr int kSteps = 32;

/** "2 RINGS" view: one 32-step sequence drawn as a figure-eight over a left ring (ring 0) and a right ring (ring 1).
    Steps 1-9 sit on the left ring from 9 o'clock clockwise to 3 o'clock, steps 10-25 go once all the way round the right ring
    (starting at its 9 o'clock), steps 26-32 sit on the bottom arc of the left ring (from 4 o'clock clockwise to 8 o'clock),
    and step 1 follows at 9 o'clock again. The two hops between the rings are 9 -> 10 (across the top gap) and 25 -> 26 (across
    the bottom gap). A step outside 0..31 is clamped. */
inline void twoRingsSlot(int step, int& ring, int& slot)
{
    if (step < 0) step = 0;
    if (step > kSteps - 1) step = kSteps - 1;
    if (step < 9)       { ring = 0; slot = (12 + step) % kSlots; }
    else if (step < 25) { ring = 1; slot = (12 + step - 9) % kSlots; }
    else                { ring = 0; slot = 5 + (step - 25); }
}

/** The step shown by `slot` of `ring` in the 2 RINGS view (the inverse of twoRingsSlot). */
inline int twoRingsStep(int ring, int slot)
{
    slot = ((slot % kSlots) + kSlots) % kSlots;
    if (ring == 1) return 9 + (slot - 12 + kSlots) % kSlots;
    if (slot >= 12) return slot - 12; // 12..15 -> steps 0..3
    if (slot <= 4) return slot + 4;   // 0..4   -> steps 4..8
    return 25 + (slot - 5);           // 5..11  -> steps 25..31
}

/** The single-ring view: `page` 0 shows steps 0..15, page 1 steps 16..31, slot 0 at 12 o'clock. */
inline int singleRingStep(int slot, int page) { return page * kSlots + slot; }
} // namespace RingLayout
