// Headless check (no JUCE, no plugin host): loads the bundled wavetables, plays notes through
// WT8Engine and verifies the output is sane. Run:  ./render_test ../wavetables out.wav
#include "WT8Engine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

static std::vector<char> slurp(const std::string& path)
{
    std::vector<char> d; FILE* f = fopen(path.c_str(), "rb"); if (!f) return d;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); d.resize((size_t)n);
    if (fread(d.data(), 1, (size_t)n, f) != (size_t)n) d.clear();
    fclose(f); return d;
}
static void writeWav(const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* w = fopen(path, "wb"); uint32_t dl = (uint32_t)L.size() * 4, rate = (uint32_t)sr, br = rate * 4, rs = 36 + dl, fs = 16;
    uint16_t fmt = 1, ch = 2, bits = 16, ba = 4;
    fwrite("RIFF", 1, 4, w); fwrite(&rs, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w); fwrite(&fs, 4, 1, w);
    fwrite(&fmt, 2, 1, w); fwrite(&ch, 2, 1, w); fwrite(&rate, 4, 1, w); fwrite(&br, 4, 1, w);
    fwrite(&ba, 2, 1, w); fwrite(&bits, 2, 1, w); fwrite("data", 1, 4, w); fwrite(&dl, 4, 1, w);
    for (size_t i = 0; i < L.size(); ++i)
    {
        int16_t a = (int16_t)(std::max(-1.f, std::min(1.f, L[i])) * 32767), b = (int16_t)(std::max(-1.f, std::min(1.f, R[i])) * 32767);
        fwrite(&a, 2, 1, w); fwrite(&b, 2, 1, w);
    }
    fclose(w);
}
static float rms(const std::vector<float>& v, size_t a, size_t b)
{ double s = 0; for (size_t i = a; i < b; ++i) s += v[i] * v[i]; return (float)std::sqrt(s / (b - a)); }

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: render_test <wavetable dir> <out.wav>\n"); return 2; }
    const int sr = 48000;
    WT8Engine eng(sr);
    for (int t = 0; t < WT8Engine::kNumTables; ++t)
    {
        char name[64]; snprintf(name, sizeof name, "%s/wavetable%02d.wav", argv[1], t + 1);
        auto d = slurp(name);
        if (!eng.loadWavetable(t, d.data(), d.size())) { fprintf(stderr, "FAIL: could not load %s\n", name); return 1; }
    }
    WT8Engine::Params p; eng.setParams(p);

    std::vector<float> L, R;
    auto run = [&](float seconds) { int n = (int)(seconds * sr); size_t o = L.size(); L.resize(o + n); R.resize(o + n); eng.render(&L[o], &R[o], n); };

    // 1) single note, then release
    eng.noteOn(60, 100); run(1.0f); eng.noteOff(60); run(1.0f);
    // 2) chord, sweep wavetable frame while held
    eng.noteOn(48, 100); eng.noteOn(55, 100); eng.noteOn(64, 100);
    for (int c = 0; c <= 32; c += 4) { p.cycle = c; eng.setParams(p); run(0.1f); }
    eng.noteOff(48); eng.noteOff(55); eng.noteOff(64); run(1.0f);
    // 3) different table + reverb macro up
    p.table = 3; p.cycle = 10; p.fxMacro = 0.9f; eng.setParams(p);
    eng.noteOn(67, 110); run(1.0f); eng.noteOff(67); run(1.5f);

    float peak = 0; bool bad = false;
    for (size_t i = 0; i < L.size(); ++i) { peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i]))); if (!std::isfinite(L[i]) || !std::isfinite(R[i])) bad = true; }
    float sustain = rms(L, sr / 2, sr), after = rms(L, (size_t)(1.9f * sr), (size_t)(2.0f * sr));
    printf("samples=%zu peak=%.3f sustain_rms=%.4f tail_rms(1.9-2.0s)=%.6f nonfinite=%d\n", L.size(), peak, sustain, after, (int)bad);
    writeWav(argv[2], L, R, sr);
    bool ok = !bad && peak > 0.01f && peak <= 1.0f && sustain > 0.005f && after < sustain * 0.05f;
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
