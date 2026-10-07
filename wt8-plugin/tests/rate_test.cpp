// Sample-rate / tuning regression test (engine only, no JUCE). Run:  ./rate_test ../wavetables
// Checks that (1) MIDI note 60 sounds at 261.63 Hz and (2) the delay time in seconds is the same
// at 44.1, 48 and 96 kHz. These both used to be wrong (octave low; delay length counted in samples).
#include "WT8Engine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

static std::vector<char> slurp(const std::string& p)
{
    std::vector<char> d; FILE* f = fopen(p.c_str(), "rb"); if (!f) return d;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); d.resize((size_t)n);
    if (fread(d.data(), 1, (size_t)n, f) != (size_t)n) d.clear();
    fclose(f); return d;
}
static void load(WT8Engine& e, const char* dir)
{
    for (int t = 0; t < WT8Engine::kNumTables; ++t)
    {
        char n[256]; snprintf(n, sizeof n, "%s/wavetable%02d.wav", dir, t + 1);
        auto d = slurp(n); e.loadWavetable(t, d.data(), d.size());
    }
}
static std::vector<float> run(WT8Engine& e, double sr, double sec)
{
    int n = (int)(sec * sr); std::vector<float> L(n), R(n); e.render(L.data(), R.data(), n); return L;
}
static double mag(const std::vector<float>& x, size_t a, size_t b, double f, double sr)
{
    double w = 2 * M_PI * f / sr, c = 2 * std::cos(w), s1 = 0, s2 = 0;
    for (size_t i = a; i < b; ++i)
    {
        double win = 0.5 - 0.5 * std::cos(2 * M_PI * (double)(i - a) / (double)(b - a));
        double s = x[i] * win + c * s1 - s2; s2 = s1; s1 = s;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: rate_test <wavetable dir>\n"); return 2; }
    bool ok = true;
    double echo48 = 0;
    for (double sr : {48000.0, 44100.0, 96000.0})
    {
        // pitch of MIDI 60 (strongest peak within +-30 cents of 261.63 Hz)
        double f1 = 0;
        {
            WT8Engine e(sr); load(e, argv[1]); WT8Engine::Params p; p.fxMacro = 0.5f; e.setParams(p);
            e.noteOn(60, 100); auto x = run(e, sr, 1.5);
            size_t a = (size_t)(0.4 * sr), b = (size_t)(1.4 * sr); double best = 0;
            for (double f = 261.63 * 0.983; f < 261.63 * 1.017; f += 0.05) { double m = mag(x, a, b, f, sr); if (m > best) { best = m; f1 = f; } }
        }
        const double cents = 1200.0 * std::log2(f1 / 261.63);
        // first echo time: full delay, short note, find first envelope peak after the dry note
        double echo = 0;
        {
            WT8Engine e(sr); load(e, argv[1]); WT8Engine::Params p; p.fxMacro = 0.f; p.delayTime = 0.5f; p.release = 0.f; e.setParams(p);
            e.noteOn(60, 127); auto a = run(e, sr, 0.03); e.noteOff(60); auto b = run(e, sr, 3.0); a.insert(a.end(), b.begin(), b.end());
            const size_t win = (size_t)(0.005 * sr); std::vector<double> env;
            for (size_t i = 0; i + win < a.size(); i += win) { double s = 0; for (size_t j = 0; j < win; ++j) s += a[i + j] * a[i + j]; env.push_back(std::sqrt(s / (double)win)); }
            double mx = 0; for (size_t i = 20; i < env.size(); ++i) mx = std::max(mx, env[i]);
            for (size_t i = 20; i + 1 < env.size(); ++i)
                if (env[i] > 0.25 * mx && env[i] >= env[i - 1] && env[i] >= env[i + 1]) { echo = (double)i * 0.005; break; }
        }
        if (sr == 48000.0) echo48 = echo;
        const bool pitchOk = std::fabs(cents) < 3.0;
        const bool echoOk = echo48 == 0 || std::fabs(echo - echo48) < 0.02;
        printf("sr=%6.0f  MIDI 60 = %.2f Hz (%+.1f cents)  first echo = %.3f s  %s\n", sr, f1, cents, echo, (pitchOk && echoOk) ? "ok" : "FAIL");
        ok = ok && pitchOk && echoOk;
    }
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
