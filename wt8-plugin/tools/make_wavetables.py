#!/usr/bin/env python3
"""ipmohc wavetable maker (v0.10 work in progress).

Writes .wav files in the format the ipmohc engine reads: mono, 32-bit float, 33 frames x 2048 samples
(67,584 samples), peak 0.9, no DC offset per frame (the same as the 7 built-in tables).

Sub-commands:
  morph     two waveforms, spectrally morphed across the 33 frames      (math)
  sweep     one waveform whose brightness opens from a sine to full     (math)
  slice     cut a recording into 33 frames                              (from your own audio)
  convert   turn an external wavetable (e.g. Serum style, any number of 2048-sample frames) into this format
  check     report on any .wav: is it a valid ipmohc table, and what does it look like

Examples:
  make_wavetables.py morph --a sine --b saw --out sine_to_saw.wav
  make_wavetables.py sweep --wave square --out square_open.wav
  make_wavetables.py slice voice.wav --out voice_table.wav
  make_wavetables.py convert some_serum_table.wav --out converted.wav
  make_wavetables.py check sine_to_saw.wav

Needs numpy only.  Licence: MIT, same as the plugin.
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

FRAMES, N = 33, 2048
PEAK = 0.9
WAVES = ("sine", "tri", "saw", "square", "pulse", "noise")


# ---------------------------------------------------------------- reading / writing

def write_table(path, table):
    t = np.asarray(table, dtype="<f4")
    assert t.shape == (FRAMES, N), t.shape
    data = t.reshape(-1).tobytes()
    fmt = struct.pack("<HHIIHH", 3, 1, 44100, 44100 * 4, 4, 32)
    body = b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(data)) + data
    Path(path).write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def read_wav(path):
    """Mono float array + sample rate. Handles PCM 8/16/24/32 and float32/64; several channels are averaged."""
    b = Path(path).read_bytes()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(b):
        tag, sz = b[pos:pos + 4], struct.unpack("<I", b[pos + 4:pos + 8])[0]
        if tag == b"fmt ":
            fmt = struct.unpack("<HHIIHH", b[pos + 8:pos + 24])
            if fmt[0] == 0xFFFE and sz >= 26:        # WAVE_FORMAT_EXTENSIBLE: real format is the first 2 bytes of the GUID
                fmt = (struct.unpack("<H", b[pos + 32:pos + 34])[0],) + fmt[1:]
        elif tag == b"data":
            data = b[pos + 8:pos + 8 + sz]
            break
        pos += 8 + sz + (sz & 1)
    if fmt is None or data is None:
        raise ValueError("no fmt/data chunk")
    code, ch, sr, _, _, bits = fmt
    if code == 3 and bits in (32, 64):
        x = np.frombuffer(data, "<f4" if bits == 32 else "<f8").astype(np.float64)
    elif code == 1 and bits == 16:
        x = np.frombuffer(data, "<i2").astype(np.float64) / 32768
    elif code == 1 and bits == 24:
        a = np.frombuffer(data[: len(data) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.int32)
        v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
        x = np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64) / (1 << 23)
    elif code == 1 and bits == 32:
        x = np.frombuffer(data, "<i4").astype(np.float64) / 2 ** 31
    elif code == 1 and bits == 8:
        x = (np.frombuffer(data, np.uint8).astype(np.float64) - 128) / 128
    else:
        raise ValueError(f"unsupported wav format (code {code}, {bits} bit)")
    x = x[: len(x) // ch * ch].reshape(-1, ch).mean(axis=1)
    return x, sr


# ---------------------------------------------------------------- building blocks

def finish(table):
    """Remove each frame's DC, scale the whole table to peak 0.9 (like the built-in tables)."""
    t = np.asarray(table, dtype=np.float64)
    t = t - t.mean(axis=1, keepdims=True)
    pk = np.abs(t).max()
    if pk < 1e-9:
        raise ValueError("table is silent")
    return t * (PEAK / pk)


def spectrum(name, max_harm, rng=None):
    """Complex harmonic spectrum (index = harmonic number, 0..N/2) of a basic waveform, limited to max_harm harmonics."""
    s = np.zeros(N // 2 + 1, dtype=np.complex128)
    h = np.arange(1, max_harm + 1)
    if name == "sine":
        s[1] = -1j
    elif name == "saw":
        s[h] = -1j / h
    elif name == "square":
        o = h[h % 2 == 1]
        s[o] = -1j / o
    elif name == "tri":
        o = h[h % 2 == 1]
        s[o] = -1j * ((-1.0) ** ((o - 1) // 2)) / o ** 2
    elif name == "pulse":                            # 25 % pulse
        s[h] = -1j * np.sin(np.pi * h * 0.25) / h
    elif name == "noise":                            # fixed random phases, 1/sqrt(n) tilt: a repeatable "noisy" cycle
        r = rng or np.random.default_rng(1)
        s[h] = np.exp(1j * r.uniform(0, 2 * np.pi, len(h))) / np.sqrt(h)
    else:
        raise ValueError(name)
    return s


def to_cycle(spec):
    return np.fft.irfft(spec, N) * N


# ---------------------------------------------------------------- generators

def make_morph(a, b, max_harm, curve):
    A, B = spectrum(a, max_harm), spectrum(b, max_harm)
    fr = []
    for i in range(FRAMES):
        x = (i / (FRAMES - 1)) ** curve
        fr.append(to_cycle((1 - x) * A + x * B))   # complex blend: magnitudes and phases move together
    return finish(fr)


def make_sweep(wave_name, max_harm, curve):
    S = spectrum(wave_name, max_harm)
    fr = []
    for i in range(FRAMES):
        x = (i / (FRAMES - 1)) ** curve
        cutoff = 1 + x * (max_harm - 1)                   # harmonics open one by one, the top one fades in smoothly
        gain = np.clip(cutoff - np.arange(N // 2 + 1) + 1, 0, 1)
        fr.append(to_cycle(S * gain))
    return finish(fr)


def make_slice(x, cycle_len):
    """33 frames cut at evenly spaced places along the recording. cycle_len = samples that go into one frame
    (0: the recording is divided into 33 equal parts). Each piece is resampled to 2048 and its ends are
    cross-faded so the loop point does not click."""
    if len(x) < 64:
        raise ValueError("recording too short")
    span = len(x) / FRAMES if cycle_len <= 0 else cycle_len
    if cycle_len > 0 and cycle_len > len(x):
        raise ValueError("cycle length longer than the recording")
    starts = np.linspace(0, len(x) - span, FRAMES)
    fr = []
    for s in starts:
        pos = s + np.arange(N) * span / N
        piece = np.interp(pos, np.arange(len(x)), x)
        k = N // 16                                        # 128-sample end blend
        w = np.linspace(0, 1, k)
        tail = np.interp(s + span + np.arange(k) * span / N, np.arange(len(x)), x, right=x[-1])
        piece[:k] = piece[:k] * w + tail * (1 - w)         # start fades in from what follows the cycle
        fr.append(piece)
    return finish(fr)


def make_convert(x, frame_size):
    if frame_size < 32:
        raise ValueError("frame size too small")
    n = len(x) // frame_size
    if n < 1:
        raise ValueError(f"file has fewer than one {frame_size}-sample frame")
    src = x[: n * frame_size].reshape(n, frame_size)
    if frame_size != N:                                    # resample each frame to 2048 (periodic interpolation)
        pos = np.arange(N) * frame_size / N
        i0 = pos.astype(int)
        f = pos - i0
        src = src[:, i0] * (1 - f) + src[:, (i0 + 1) % frame_size] * f
    fr = []
    for i in range(FRAMES):
        p = i * (n - 1) / (FRAMES - 1) if n > 1 else 0.0   # first and last frame stay exactly the file's first and last
        j = int(np.floor(p))
        f = p - j
        fr.append(src[j] if f == 0 or j + 1 >= n else src[j] * (1 - f) + src[j + 1] * f)
    return finish(fr)


# ---------------------------------------------------------------- check

def check(path):
    b = Path(path).read_bytes()
    print(f"{path}: {len(b)} bytes")
    try:
        x, sr = read_wav(path)
    except Exception as e:
        print("  NOT READABLE:", e)
        return False
    pos, fmt = 12, None
    while pos + 8 <= len(b):
        tag, sz = b[pos:pos + 4], struct.unpack("<I", b[pos + 4:pos + 8])[0]
        if tag == b"fmt ":
            fmt = struct.unpack("<HHIIHH", b[pos + 8:pos + 24])
        pos += 8 + sz + (sz & 1)
    ok = fmt is not None and fmt[0] == 3 and fmt[1] == 1 and fmt[5] == 32
    print(f"  format: code {fmt[0]}, {fmt[1]} ch, {fmt[5]} bit  -> {'ipmohc format' if ok else 'NOT the ipmohc format (needs mono float32); use `convert`'}")
    print(f"  samples: {len(x)}  (ipmohc needs at least {FRAMES * N}; frames of 2048: {len(x) / N:.2f})")
    ok = ok and len(x) >= FRAMES * N
    if len(x) >= FRAMES * N:
        t = x[: FRAMES * N].reshape(FRAMES, N)
        print(f"  peak {np.abs(t).max():.3f}  rms {np.sqrt((t ** 2).mean()):.3f}  worst frame DC {np.abs(t.mean(axis=1)).max():.4f}")
        print(f"  finite: {bool(np.isfinite(t).all())}")
        d = np.sqrt(((t[1:] - t[:-1]) ** 2).mean(axis=1))
        print(f"  frame-to-frame change rms: min {d.min():.3f} max {d.max():.3f} (a big jump = an abrupt step in the sweep)")
        ok = ok and bool(np.isfinite(t).all())
    print("  RESULT:", "valid ipmohc table" if ok else "not usable as is")
    return ok


# ---------------------------------------------------------------- main

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("morph", "sweep"):
        p = sub.add_parser(name)
        if name == "morph":
            p.add_argument("--a", choices=WAVES, required=True)
            p.add_argument("--b", choices=WAVES, required=True)
        else:
            p.add_argument("--wave", choices=WAVES, required=True)
        p.add_argument("--harmonics", type=int, default=64, help="most harmonics used (default 64; fewer = softer, less aliasing at high notes)")
        p.add_argument("--curve", type=float, default=1.0, help="1 = even steps; above 1 spends more frames near the start (default 1)")
        p.add_argument("--out", required=True)
    p = sub.add_parser("slice")
    p.add_argument("input")
    p.add_argument("--cycle-samples", type=int, default=0, help="samples per frame; 0 = divide the recording into 33 equal parts")
    p.add_argument("--out", required=True)
    p = sub.add_parser("convert")
    p.add_argument("input")
    p.add_argument("--frame-size", type=int, default=2048, help="samples per frame in the input (default 2048)")
    p.add_argument("--out", required=True)
    p = sub.add_parser("check")
    p.add_argument("files", nargs="+")
    a = ap.parse_args(argv)

    if a.cmd == "check":
        return 0 if all([check(f) for f in a.files]) else 1
    if a.cmd in ("morph", "sweep") and not 1 <= a.harmonics <= 512:
        ap.error("--harmonics must be 1..512")
    if a.cmd == "morph":
        t = make_morph(a.a, a.b, a.harmonics, a.curve)
    elif a.cmd == "sweep":
        t = make_sweep(a.wave, a.harmonics, a.curve)
    elif a.cmd == "slice":
        x, _ = read_wav(a.input)
        t = make_slice(x, a.cycle_samples)
    else:
        x, _ = read_wav(a.input)
        t = make_convert(x, a.frame_size)
    write_table(a.out, t)
    print("wrote", a.out)
    return 0 if check(a.out) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError) as e:
        print("error:", e, file=sys.stderr)
        sys.exit(2)
