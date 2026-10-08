// WavetableImport: JUCE-free reading of a .wav file and conversion of it into an ipmohc wavetable
// (33 frames x 2048 samples, mono float). Used by the LOAD button; tools/make_wavetables.py does the same job offline.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wtimport
{
constexpr int kFrames = 33;
constexpr int kFrameSize = 2048;
constexpr int kTableFloats = kFrames * kFrameSize;
constexpr float kPeak = 0.9f; // the built-in tables peak at 0.9

/** Reads a RIFF/WAVE file (PCM 8/16/24/32 bit or float 32/64, any channel count, mixed down to mono).
    `clmFrameSize` is the frame size a Serum-style "clm " chunk announces (0 when there is none). False with `error` on failure. */
bool readWav(const uint8_t* data, size_t numBytes, std::vector<float>& mono, int& sampleRate, int& clmFrameSize, std::string& error);

/** Turns mono audio into a table (`out` gets kTableFloats samples, DC removed in every frame, peak kPeak).
    frameSizeHint > 0 (from a "clm " chunk) says how long one frame in the file is. Without a hint:
      - a length that is a multiple of 2048 is read as 2048-sample frames (the usual wavetable layout);
      - less than two frames' worth is read as ONE cycle (the table then holds that cycle in all 33 frames);
      - anything else is a recording and is cut into 33 equal parts.
    More or fewer than 33 frames are mapped onto 33 (neighbouring frames are blended; first and last stay as they are).
    `note` describes what was done, for the person. False (and `error` filled in) when the audio is too short or silent. */
bool convertToTable(const std::vector<float>& mono, int frameSizeHint, std::vector<float>& out, std::string& note, std::string& error);

/** The table data of a 32-bit float table WAV as the engine reads it (kTableFloats floats), without any change. */
bool readRawTable(const uint8_t* data, size_t numBytes, std::vector<float>& out);
} // namespace wtimport
