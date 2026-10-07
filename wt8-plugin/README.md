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
- Generic parameter UI for now (every control as a slider, automatable in Logic). A custom panel comes later.
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
- **Pitch.** Engine pitch follows the firmware's own formula (MIDI note 60 -> transpose index 0). Measured note 60 as ~260 Hz in a zero-crossing test, roughly where you'd expect C4 but it has not been checked against a tuner, and it depends on how many cycles each wavetable stores. Check by ear against another instrument; the Pitch/Octave params can correct it, and a fixed offset can be added in `WT8Engine::noteOn`.
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
