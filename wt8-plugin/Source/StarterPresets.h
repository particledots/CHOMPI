#pragma once
// v0.9 starter presets: the 14 sound presets that ship with the CHOMPI WAVE firmware, converted to ipmohc's sound parameters.
//
// Source: firmware/card-profiles/wave-1.0/presets.json in this repository (MIT licence, (c) the CHOMPI Club authors; see the
// licence and THIRD_PARTY notices of the repository, which stay with this file). They were chosen by ear on the hardware, which
// is why they are used here instead of values picked from the parameter ranges by a program that cannot listen. The rows below
// are the firmware's integers, copied as they are, so they can be compared with that file line by line (plugin_test does it).
//
// Firmware control order in a row (MenuPage.h / PresetManager.h; the values are the knob value x 1000 except where noted):
//    0 pitch (x1000; 500 = no shift, 0 = -1 octave, 999 = +1 octave)   1 cycle (0..32, as is)   2 table (0..6, as is)
//    3 attack        4 pitch-LFO depth     5 release      6 filter-LFO depth      7 cutoff
//    8 pitch-LFO rate        9 delay <-> reverb ("delay position")      10 resonance
//   11 filter-LFO rate      12 delay / reverb time      13 LFO on/off bits (pitch = 1, filter = 2; 3 in all 14 presets)
// MenuPage.h SetVoiceSlot() hands them to the engine setters the plugin's parameters use, so no value curve is involved.
//
// What is different in the plugin: the pitch is rounded to a whole semitone (the firmware's pitch knob moves in semitones; the
// stored 498 / 501 / 999 are 1 or 2 thousandths off the grid, which I read as rounding drift of the firmware's own save and
// load - an inference, not something the code says; 0 is exactly -1 octave); there is no LFO on/off (all 14 have both on, and a depth of 0 is silent anyway);
// OCTAVE and COMP / SAT are not part of a firmware preset, so they go to their defaults (0 and 0), like INIT; and the output
// level (GAIN, PAN, BOOST) is left alone, as for every preset.
namespace starter
{
constexpr int kCount = 14;
constexpr int kControls = 14;

constexpr int kRows[kCount][kControls] = {
    { 999,    5,    3,    0,   30,   26,  629,  139,  669,  169,  630, 1000,  819,    3},
    { 999,   23,    0,    0,  120,  180,   30,  240,  490,  640,  630,   10, 1000,    3},
    {   0,   31,    6,    0,    0,  279,  369,  700, 1000,  679,  970, 1000,  639,    3},
    { 999,   17,    0,   36,   59,  333,   60,  349,  280,  380,  299,   10,  390,    3},
    { 999,   13,    4,   18,   89,   72,    0,  379,  550,  910,  630,  580,  699,    3},
    { 501,   29,    2,  324,   89,    0, 1000, 1000,  460,  880,  940,  700,  819,    3},
    { 498,    2,    1,    0,  220,  117,    0,  500,  610,  769,  630,  580,    0,    3},
    { 498,    9,    4,    9,    0,    0,    0,  500,  580,  500,  630,  580,  400,    3},
    { 498,   19,    3,    0,    0,   89,    0,  739,  580,  120,  959,  580,  149,    3},
    { 999,   20,    1,    0,  209,   18,  149,  400,  520, 1000,  120,  100,  179,    3},
    { 498,   10,    5,    0,    0,    0,  100,  239,  580,  500, 1000,   40,  400,    3},
    { 498,    9,    5,    0,    0,    0,  149,  799,  580,  290, 1000,  160,  520,    3},
    { 999,   12,    1,    0,  490,    0,    0,  500, 1000,  410,  630,  580,  400,    3},
    { 498,   21,    5,    0, 1000,   36,   89,  820, 1000,  329,  850, 1000,    0,    3},
};
} // namespace starter
