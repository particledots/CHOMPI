# ipmohc (formerly WT8) — wavetable synth plugin built from the CHOMPI WAVE engine

An 8-voice wavetable synthesizer (AU / VST3 / Standalone) that runs the **CHOMPI WAVE 1.0** synth engine
from the open-source CHOMPI release (https://github.com/CHOMPI-Club/CHOMPI, MIT) inside a JUCE plugin.
Not affiliated with or endorsed by Chase Bliss / CHOMPI Club. "CHOMPI" is their trademark; this project
uses a different name on purpose.

**Status: v0.1, first working skeleton.** The engine, parameters, MIDI, state save/restore and wavetable
loading are verified by headless tests on Linux (see Tests). It has **not yet been built on macOS or loaded
in Logic** — that is the next step.

## What's in it
- 8 voices: wavetable oscillator (7 tables x 33 frames) -> resonant filter -> amp envelope
- 2 LFOs (pitch, filter), delay <-> reverb macro, compressor <-> saturation macro, pan, gain
- Custom panel (v0.2): knobs grouped by signal flow (oscillator, envelope, filter, LFO, effects, output), value readouts, double-click a knob to reset it, double-click a readout to type a value, resizable window. All controls are automatable in Logic. No wavetable display yet.
- Not yet: the 32-step sequencer, MIDI clock, presets (beyond Logic's own plugin presets), user wavetables.

## Build on your Mac (Apple silicon or Intel)
One-time setup:
1. Install Xcode from the App Store, open it once, accept the license. Then in Terminal: `xcode-select --install`
2. Install Homebrew (https://brew.sh), then: `brew install cmake`

Build (in Terminal, from this folder):
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
```
The first configure downloads JUCE (needs internet, a couple of minutes). A successful build copies:
- `~/Library/Audio/Plug-Ins/Components/ipmohc.component`  (Audio Unit — this is the one Logic uses)
- `~/Library/Audio/Plug-Ins/VST3/ipmohc.vst3`

Check it: `./build/render_test wavetables build/out.wav` and `./build/plugin_test` should both print PASS.
Validate as an Audio Unit: `auval -v aumu Ipmo Ptdt`. Then in Logic: a Software Instrument track ->
Instrument slot -> AU Instruments -> particledots -> ipmohc. If it doesn't show up, restart Logic
(Logic Pro > Settings > Plug-in Manager > Reset & Rescan Selection).

## Parameters (all map to the CHOMPI WAVE panel)
| Parameter | Range / default | WAVE panel control |
|---|---|---|
| Wavetable | 1-7 / 1 | wavetable select (shift menu) |
| Frame | 0-32 / 0 | wavetable cycle (page 2, knob 1) |
| Octave | -1..1 / 0 | octave |
| Pitch (semitones) | -12..12 / 0 | pitch knob (+/- 1 octave) |
| Attack, Release | 0-1 / 0, 0 | knobs 2 and 3 |
| Filter Cutoff, Resonance | 0-1 / .5, .63 | knob 4 (page 2), shift menu |
| Delay <-> Reverb | 0-1 / .5 | knob 4: 0 = delay, .5 = dry, 1 = reverb |
| Delay / Reverb Time | 0-1 / .4 | shift menu |
| Pitch LFO depth, rate; Filter LFO depth, rate | 0-1 | page 2 and shift menu |
| Gain, Pan | 0-1 / .84, .5 | knob 6 |
| Compressor <-> Saturation | 0-1 / 0 | shift menu final comp: below .5 compressor, above .5 saturation |
| Output Boost (dB) | -12..36 / +20 | **plugin only.** The engine's raw output is very quiet (it feeds analog hardware gain); this makes up the difference. 8 notes at once at +20 dB can approach full scale. |

## Known limitations / things to verify
- **Pitch.** The firmware plays MIDI note 60 at 130.81 Hz (an octave below most synths). ipmohc adds +12 semitones in `WT8Engine::noteOn` so MIDI 60 = 261.63 Hz; `tests/rate_test.cpp` checks this to within 3 cents at 44.1, 48 and 96 kHz. Octave +1 / -1 still shifts by an octave from there.
- **Sample rate.** The firmware counts delay length in samples at 48 kHz. The engine now scales the delay length and memory by host rate / 48000 (one small change in `Source/engine/subtractiveEngine.h`), so delay time in seconds is the same at every rate. Reverb size and LFO speeds are not yet verified across rates.
- **Sample rate.** The firmware runs at 48 kHz; some constants (parameter smoothing, the 960-sample wavetable crossfade, the delay buffer length) were tuned for that, so at 44.1 / 96 kHz timing shifts slightly. Not tested at 96 kHz.
- **Block size.** The engine processes 24-sample blocks as on hardware; key requests are applied between blocks (sub-millisecond).
- Velocity -> level follows the firmware (vel+1)/127.

## Tests (headless, no audio hardware)
- `render_test`: loads the 7 wavetables, plays a note, a chord with a frame sweep, and a reverb tail; checks for finite output, sane level, and silence after release. Writes a WAV.
- `plugin_test`: instantiates the real plugin, sends MIDI through `processBlock` at 44.1 kHz / 512 samples, checks sound and release, and round-trips state.

## How it's put together
```
Source/engine/    UNMODIFIED copies of the CHOMPI WAVE DSP headers (from firmware/chompi-wave/code/src)
Source/shim/      tiny stand-ins for libDaisy's daisy.h, FIFO (copied), FatFs and the SD file queue
Source/WT8Engine  JUCE-free wrapper: params -> engine setters, MIDI -> key requests, render(), WAV loading
Source/PluginProcessor  JUCE glue: parameters, MIDI timing, state, generic editor
third_party/DaisySP  DaisySP (MIT, Electrosmith), used as-is
wavetables/       the 7 wavetable WAVs from the CHOMPI repo
```

## Licenses and credits
- CHOMPI firmware sources and wavetables: MIT, (c) 2026 CHOMPI Club — `third_party/CHOMPI-LICENSE.txt`; see `third_party/CHOMPI-THIRD_PARTY.md` for authorship (Electrosmith / Chase Bliss).
- `reverb.h`, `fx_engine.h`: (c) Emilie Gillet (Mutable Instruments), MIT, notice kept in the files.
- DaisySP, libDaisy FIFO: MIT, (c) Electrosmith — `third_party/DaisySP/LICENSE`, `third_party/libDaisy-LICENSE.txt`.
- JUCE (downloaded at configure time): AGPLv3 or commercial licence. Fine for personal use; if you ever distribute binaries you must either release this project's source under AGPLv3 or buy a JUCE licence.
