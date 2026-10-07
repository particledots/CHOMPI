// Headless timing tests for StepSequencer (no JUCE, no audio).  Run: ./seq_test
#include "StepSequencer.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

struct Ev { long long pos; bool on; int note; };
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void fill(StepSequencer& s, std::initializer_list<int> notes) // -1 = rest
{
    s.clear();
    for (int n : notes) { if (n < 0) s.addRest(); else s.recordNote(n, 100); }
}

// Runs `total` samples in blocks; host ppq advances with the (fixed) tempo unless `jumps` says otherwise.
static std::vector<Ev> run(StepSequencer& s, SeqSettings st, double bpm, double sr, int block, long long total,
                           bool follow, double startPpq = 0.0, std::vector<std::pair<long long, double>> jumps = {})
{
    std::vector<Ev> evs; SeqEvent buf[64];
    double ppq = startPpq; const double pps = bpm / 60.0 / sr;
    st.followHost = follow;
    for (long long t = 0; t < total; t += block)
    {
        const int n = (int) std::min<long long>(block, total - t);
        for (auto& j : jumps) if (j.first == t) ppq = j.second;
        SeqHostInfo h; h.hostPlaying = true; h.havePpq = true; h.ppq = ppq; h.bpm = bpm;
        const int c = s.process(sr, n, h, st, buf, 64);
        for (int i = 0; i < c; ++i) evs.push_back({t + buf[i].offset, buf[i].on, buf[i].note});
        ppq += n * pps;
    }
    return evs;
}
static bool near(long long a, long long b, long long tol = 1) { return std::llabs(a - b) <= tol; }

int main()
{
    const double sr = 48000, bpm = 120; // 1 quarter note = 24000 samples
    SeqSettings st; st.play = true; st.division = 2; st.gate = 0.5f; // 1/16 = 6000 samples

    printf("T1 free-run timing, pattern C E rest G, 1/16, gate 50%%\n");
    {
        StepSequencer s; fill(s, {60, 64, -1, 67});
        auto e = run(s, st, bpm, sr, 512, 48000, false);
        std::vector<Ev> ons, offs; for (auto& x : e) (x.on ? ons : offs).push_back(x);
        const long long expPos[] = {0, 6000, 18000, 24000, 30000, 42000};
        const int expNote[] = {60, 64, 67, 60, 64, 67};
        CHECK(ons.size() == 6, "expected 6 note-ons, got %zu", ons.size());
        for (size_t i = 0; i < ons.size() && i < 6; ++i)
            CHECK(near(ons[i].pos, expPos[i]) && ons[i].note == expNote[i], "on %zu at %lld note %d (want %lld note %d)", i, ons[i].pos, ons[i].note, expPos[i], expNote[i]);
        for (size_t i = 0; i < offs.size() && i < ons.size(); ++i)
            CHECK(near(offs[i].pos, ons[i].pos + 3000), "off %zu at %lld (want %lld)", i, offs[i].pos, ons[i].pos + 3000);
    }

    printf("T2 same events at block sizes 64 / 512 / 997 / 4096\n");
    {
        StepSequencer a, b, c, d; fill(a, {60, 64, -1, 67}); fill(b, {60, 64, -1, 67}); fill(c, {60, 64, -1, 67}); fill(d, {60, 64, -1, 67});
        auto ea = run(a, st, bpm, sr, 64, 96000, false), eb = run(b, st, bpm, sr, 512, 96000, false);
        auto ec = run(c, st, bpm, sr, 997, 96000, false), ed = run(d, st, bpm, sr, 4096, 96000, false);
        CHECK(ea.size() == eb.size() && ea.size() == ec.size() && ea.size() == ed.size(), "event counts differ %zu %zu %zu %zu", ea.size(), eb.size(), ec.size(), ed.size());
        for (size_t i = 0; i < ea.size() && i < eb.size() && i < ec.size() && i < ed.size(); ++i)
            CHECK(near(ea[i].pos, eb[i].pos) && near(ea[i].pos, ec[i].pos) && near(ea[i].pos, ed[i].pos) && ea[i].on == eb[i].on && ea[i].note == ec[i].note,
                  "event %zu differs: %lld %lld %lld %lld", i, ea[i].pos, eb[i].pos, ec[i].pos, ed[i].pos);
    }

    printf("T3 triplet 1/16T = 4000 samples at 120 bpm\n");
    {
        StepSequencer s; fill(s, {60, 62, 64}); SeqSettings t3 = st; t3.division = 6;
        auto e = run(s, t3, bpm, sr, 300, 24000, false);
        long long last = -1; int ons = 0;
        for (auto& x : e) if (x.on) { if (last >= 0) CHECK(near(x.pos - last, 4000), "spacing %lld", x.pos - last); last = x.pos; ++ons; }
        CHECK(ons == 6, "expected 6 ons in 1 beat of triplets, got %d", ons);
    }

    printf("T4 follow Logic: start mid-step at 3.3 quarters, 1/8 steps\n");
    {
        StepSequencer s; fill(s, {60, 62, 64, 65}); SeqSettings t4 = st; t4.division = 1; t4.play = false;
        auto e = run(s, t4, bpm, sr, 512, 24000, true, 3.3);
        CHECK(!e.empty() && e[0].on, "no first note");
        if (!e.empty()) CHECK(near(e[0].pos, 4800) && e[0].note == 65, "first on at %lld note %d (want 4800, note 65 = step 4 of 4)", e[0].pos, e[0].note);
    }

    printf("T5 follow Logic: loop jump back to bar start releases and restarts\n");
    {
        StepSequencer s; fill(s, {60, 62, 64, 65}); SeqSettings t5 = st; t5.division = 1; t5.play = false; t5.gate = 1.f;
        auto e = run(s, t5, bpm, sr, 512, 36000, true, 0.0, {{12288, 0.0}});
        bool found = false;
        for (size_t i = 0; i + 1 < e.size(); ++i)
            if (e[i].pos == 12288 && !e[i].on && e[i + 1].pos == 12288 && e[i + 1].on && e[i + 1].note == 60) found = true;
        CHECK(found, "expected off then on(60) at sample 12288 after the jump");
    }

    printf("T6 gate 100%% is legato: off and next on land on the same sample\n");
    {
        StepSequencer s; fill(s, {60, 62}); SeqSettings t6 = st; t6.gate = 1.f;
        auto e = run(s, t6, bpm, sr, 512, 24000, false);
        for (size_t i = 1; i + 1 < e.size(); ++i)
            if (!e[i].on) CHECK(e[i + 1].on && e[i].pos == e[i + 1].pos, "off at %lld not paired with on", e[i].pos);
    }

    printf("T7 record armed / stopped transport / mute silence the sequence and release a held note\n");
    {
        StepSequencer s; fill(s, {60, 62}); SeqEvent buf[64]; SeqHostInfo h; h.hostPlaying = true; h.havePpq = true; h.bpm = bpm;
        SeqSettings t7 = st; t7.gate = 1.f;
        int c = s.process(sr, 512, h, t7, buf, 64); CHECK(c == 1 && buf[0].on, "should start a note");
        t7.recording = true; c = s.process(sr, 512, h, t7, buf, 64);
        CHECK(c == 1 && !buf[0].on && buf[0].offset == 0, "recording should release the held note");
        t7.recording = false; c = s.process(sr, 512, h, t7, buf, 64); CHECK(c >= 1 && buf[0].on, "should resume from step 1");
        t7.mute = true; c = s.process(sr, 512, h, t7, buf, 64); CHECK(c == 1 && !buf[0].on, "mute should release the note");
        c = s.process(sr, 6000, h, t7, buf, 64); bool anyOn = false; for (int i = 0; i < c; ++i) anyOn |= buf[i].on; CHECK(!anyOn, "muted sequence should make no notes");
        SeqSettings f = st; f.followHost = true; f.mute = false; f.gate = 1.f; h.ppq = 0.0; s.resetTransport();
        c = s.process(sr, 512, h, f, buf, 64); CHECK(c == 1 && buf[0].on, "follow mode starts a note when host plays");
        h.hostPlaying = false; c = s.process(sr, 512, h, f, buf, 64); CHECK(c == 1 && !buf[0].on, "host stop should release the note");
    }

    printf("T8 recording steps, 32 step cap, save/restore, editing\n");
    {
        StepSequencer s; for (int i = 0; i < 40; ++i) s.recordNote(40 + i, 90);
        CHECK(s.length() == 32, "length %d", s.length());
        s.clear(); s.recordNote(60, 100); s.addRest(); s.recordNote(67, 80);
        const std::string text = s.serialize(); CHECK(text == "60:100,r,67:80", "serialized '%s'", text.c_str());
        StepSequencer r; r.deserialize(text); CHECK(r.serialize() == text, "round trip failed");
        r.setNote(2, 70); r.toggleRest(1); r.toggleRest(4); r.deleteLast();
        SeqStep st32[32]; int len, idx; r.snapshot(st32, len, idx);
        CHECK(len == 4 && st32[2].note == 70 && !st32[1].rest, "editing result len %d note %d", len, st32[2].note);
        r.deserialize("garbage,,xyz"); CHECK(r.length() == 3, "tolerant parse, length %d", r.length());
    }

    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures ? 1 : 0;
}
