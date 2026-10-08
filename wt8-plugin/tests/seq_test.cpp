// Headless timing tests for StepSequencer (no JUCE, no audio).  Run: ./seq_test
#include "StepSequencer.h"
#include "RingLayout.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <random>
#include <string>
#include <vector>

struct Ev { long long pos; bool on; int note; int vel = 0; };
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
    std::vector<Ev> evs; static SeqEvent buf[1024];
    double ppq = startPpq; const double pps = bpm / 60.0 / sr;
    st.followHost = follow;
    for (long long t = 0; t < total; t += block)
    {
        const int n = (int) std::min<long long>(block, total - t);
        for (auto& j : jumps) if (j.first == t) ppq = j.second;
        SeqHostInfo h; h.hostPlaying = true; h.havePpq = true; h.ppq = ppq; h.bpm = bpm;
        const int c = s.process(sr, n, h, st, buf, 1024);
        for (int i = 0; i < c; ++i) evs.push_back({t + buf[i].offset, buf[i].on, buf[i].note, buf[i].vel});
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

    // ================================ v0.4: loop length, direction, probability, seed ================================
    auto onNotes = [](const std::vector<Ev>& e, size_t maxCount = 100000) {
        std::vector<int> v; for (auto& x : e) if (x.on && v.size() < maxCount) v.push_back(x.note); return v;
    };
    auto sameEvents = [](const std::vector<Ev>& a, const std::vector<Ev>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (a[i].pos != b[i].pos || a[i].on != b[i].on || a[i].note != b[i].note) return false;
        return true;
    };
    auto countOn = [](const std::vector<Ev>& e, int note = -1) { int n = 0; for (auto& x : e) if (x.on && (note < 0 || x.note == note)) ++n; return n; };
    const long long step16 = 6000; // one 1/16 step at 120 bpm / 48 kHz

    printf("T9 stepIndexFor: all four directions, loop of 1 and 2, negative step counters\n");
    {
        const int fw[] = {0, 1, 2, 3, 0, 1, 2, 3}, bw[] = {3, 2, 1, 0, 3, 2, 1, 0};
        const int pn[] = {0, 1, 2, 3, 2, 1, 0, 1, 2, 3}, pr[] = {0, 1, 2, 3, 3, 2, 1, 0, 0, 1, 2, 3};
        for (int k = 0; k < 8; ++k) CHECK(StepSequencer::stepIndexFor(k, 4, 0, false, 1) == fw[k] && StepSequencer::stepIndexFor(k, 4, 1, false, 1) == bw[k], "fwd/back k=%d", k);
        for (int k = 0; k < 10; ++k) CHECK(StepSequencer::stepIndexFor(k, 4, 2, false, 1) == pn[k], "pendulum k=%d got %d want %d", k, StepSequencer::stepIndexFor(k, 4, 2, false, 1), pn[k]);
        for (int k = 0; k < 12; ++k) CHECK(StepSequencer::stepIndexFor(k, 4, 2, true, 1) == pr[k], "pendulum+ends k=%d got %d want %d", k, StepSequencer::stepIndexFor(k, 4, 2, true, 1), pr[k]);
        CHECK(StepSequencer::stepIndexFor(-1, 4, 0, false, 1) == 3 && StepSequencer::stepIndexFor(-1, 4, 1, false, 1) == 0, "negative k fwd/back");
        for (long long k = -20; k < 20; ++k)
            for (int d = 0; d < 4; ++d)
            {
                CHECK(StepSequencer::stepIndexFor(k, 1, d, d == 2, 5) == 0, "loop of 1 must always give step 0");
                const int a = StepSequencer::stepIndexFor(k, 2, d, false, 5), b = StepSequencer::stepIndexFor(k, 7, d, true, 5);
                CHECK(a >= 0 && a < 2 && b >= 0 && b < 7, "index out of range (k=%lld dir=%d)", k, d);
            }
    }

    printf("T10 directions through the sequencer, pattern C D E F, 1/16\n");
    {
        const std::vector<int> fwd = {60,62,64,65,60,62,64,65,60,62,64,65}, bwd = {65,64,62,60,65,64,62,60,65,64,62,60},
                               pen = {60,62,64,65,64,62,60,62,64,65,64,62}, penR = {60,62,64,65,65,64,62,60,60,62,64,65};
        const int dirs[4] = {0, 1, 2, 2}; const bool rep[4] = {false, false, false, true}; const std::vector<int>* want[4] = {&fwd, &bwd, &pen, &penR};
        for (int t = 0; t < 4; ++t)
        {
            StepSequencer s; fill(s, {60, 62, 64, 65}); SeqSettings d = st; d.direction = dirs[t]; d.pendRepeat = rep[t];
            const auto got = onNotes(run(s, d, bpm, sr, 512, 12 * step16, false));
            CHECK(got == *want[t], "direction %d repeat=%d played the wrong order (%zu notes)", dirs[t], (int) rep[t], got.size());
        }
    }

    printf("T11 loop length: first N steps only, clamped to the pattern, 0 = all\n");
    {
        StepSequencer s; fill(s, {60, 61, 62, 63, 64, 65, 66, 67}); SeqSettings d = st;
        d.loopLen = 3; auto g = onNotes(run(s, d, bpm, sr, 512, 7 * step16, false));
        CHECK(g == std::vector<int>({60, 61, 62, 60, 61, 62, 60}), "loop 3 forward");
        d.direction = 1; StepSequencer b; fill(b, {60, 61, 62, 63, 64, 65, 66, 67}); g = onNotes(run(b, d, bpm, sr, 512, 4 * step16, false));
        CHECK(g == std::vector<int>({62, 61, 60, 62}), "loop 3 backward");
        StepSequencer c; fill(c, {60, 62, 64, 65}); d = st; d.loopLen = 12; g = onNotes(run(c, d, bpm, sr, 512, 5 * step16, false));
        CHECK(g == std::vector<int>({60, 62, 64, 65, 60}), "loop longer than the pattern uses the whole pattern");
        StepSequencer z; fill(z, {60, 61, 62, 63, 64, 65, 66, 67}); d = st; d.loopLen = 0; g = onNotes(run(z, d, bpm, sr, 512, 9 * step16, false));
        CHECK(g == std::vector<int>({60, 61, 62, 63, 64, 65, 66, 67, 60}), "loop 0 = whole pattern");
        StepSequencer p; fill(p, {60, 61, 62, 63, 64, 65, 66, 67}); d = st; d.loopLen = 4; d.direction = 2; g = onNotes(run(p, d, bpm, sr, 512, 8 * step16, false));
        CHECK(g == std::vector<int>({60, 61, 62, 63, 62, 61, 60, 61}), "pendulum inside a loop of 4");
    }

    printf("T12 probability: global, per step, rests, missed roll acts like a rest\n");
    {
        { StepSequencer s; fill(s, {60, 62, 64, 65}); SeqSettings d = st; d.prob = 0.f; d.seed = 3;
          CHECK(run(s, d, bpm, sr, 512, 64 * step16, false).empty(), "global 0% must make no events at all"); }
        { StepSequencer s; fill(s, {60, 62, 64, 65}); SeqSettings d = st; d.prob = 1.f; d.seed = 3;
          CHECK(countOn(run(s, d, bpm, sr, 512, 64 * step16, false)) == 64, "100% x 100% must play every step"); }
        { StepSequencer s; fill(s, {60, 62, 64, 65}); SeqSettings d = st; d.prob = 0.5f; d.seed = 3;
          const int n = countOn(run(s, d, bpm, sr, 4096, 2000 * step16, false));
          CHECK(n > 900 && n < 1100, "global 50%% over 2000 steps played %d (want about 1000)", n); }
        { StepSequencer s; fill(s, {60, 62, 64, 65}); s.setProb(1, 0); s.setProb(2, 50); SeqSettings d = st; d.seed = 5;
          const auto e = run(s, d, bpm, sr, 512, 800 * step16, false);
          CHECK(countOn(e, 62) == 0, "0%% step must never play");
          CHECK(countOn(e, 60) == 200 && countOn(e, 65) == 200, "100%% steps must always play (%d, %d)", countOn(e, 60), countOn(e, 65));
          const int m = countOn(e, 64); CHECK(m > 70 && m < 130, "50%% step played %d of 200", m); }
        { StepSequencer s; fill(s, {60, 62, 64, 65}); s.setProb(0, 50); SeqSettings d = st; d.prob = 0.5f; d.seed = 5; // 50% x 50% = 25%
          const int n = countOn(run(s, d, bpm, sr, 4096, 4000 * step16, false), 60);
          CHECK(n > 190 && n < 310, "step 50%% x global 50%% played %d of 1000 (want about 250)", n); }
        { StepSequencer s; fill(s, {60, 62}); s.setProb(1, 0); SeqSettings d = st; d.gate = 1.f; // missed step: previous note still ends on time
          const auto e = run(s, d, bpm, sr, 512, 6 * step16, false);
          bool ok = !e.empty(); int ons = 0;
          for (size_t i = 0; i < e.size(); ++i) { if (e[i].on) { ++ons; ok = ok && e[i].note == 60; } else ok = ok && i > 0 && e[i - 1].on && near(e[i].pos, e[i - 1].pos + step16); }
          CHECK(ok && ons == 3, "missed step should just end the previous note (ons=%d)", ons); }
        { StepSequencer s; fill(s, {60, -1, 62}); SeqSettings d = st; d.seed = 2;
          CHECK(countOn(run(s, d, bpm, sr, 512, 30 * step16, false)) == 20, "rests stay rests"); }
    }

    printf("T13 seed: fixed = repeatable at any block size, off = new choices each start\n");
    {
        SeqSettings d = st; d.prob = 0.5f; d.direction = 3; d.seed = 11; d.gate = 0.5f;
        auto mk = [&](StepSequencer& q) { fill(q, {60, 62, 64, 65, 67, 69}); q.setProb(1, 60); };
        StepSequencer a, b, c; mk(a); mk(b); mk(c);
        const auto ea = run(a, d, bpm, sr, 64, 400 * step16, false), eb = run(b, d, bpm, sr, 997, 400 * step16, false), ec = run(c, d, bpm, sr, 4096, 400 * step16, false);
        CHECK(!ea.empty() && sameEvents(ea, eb) && sameEvents(ea, ec), "fixed seed gave different events at different block sizes (%zu %zu %zu)", ea.size(), eb.size(), ec.size());
        StepSequencer o; mk(o); SeqSettings d2 = d; d2.seed = 12;
        CHECK(!sameEvents(ea, run(o, d2, bpm, sr, 512, 400 * step16, false)), "seed 11 and seed 12 should differ");
        StepSequencer p, q; mk(p); mk(q); SeqSettings d0 = d; d0.seed = 0;
        const auto e1 = run(p, d0, bpm, sr, 512, 400 * step16, false);
        CHECK(!sameEvents(e1, run(q, d0, bpm, sr, 512, 400 * step16, false)), "seed off: two fresh sequencers should differ");
        p.resetTransport();
        CHECK(!sameEvents(e1, run(p, d0, bpm, sr, 512, 400 * step16, false)), "seed off: stopping and starting again should re-roll");
        // Logic sync: a cycle jump re-rolls with seed off, repeats exactly with a fixed seed. 8 quarters = 32 steps, jump back at t = 192000
        const long long pass = 192000;
        for (int fixedSeed = 0; fixedSeed < 2; ++fixedSeed)
        {
            StepSequencer l; fill(l, {60, 62, 64, 65}); SeqSettings f = st; f.prob = 0.5f; f.seed = fixedSeed ? 4 : 0;
            const auto e = run(l, f, bpm, sr, 512, 2 * pass, true, 0.0, {{pass, 0.0}});
            std::vector<Ev> p1, p2; for (auto& x : e) (x.pos < pass ? p1 : p2).push_back({x.pos < pass ? x.pos : x.pos - pass, x.on, x.note});
            const bool same = p1.size() == p2.size() && sameEvents(p1, p2);
            CHECK(fixedSeed ? same : !same, "Logic cycle pass with seed %s: passes %s", fixedSeed ? "fixed" : "off", same ? "identical" : "differ");
        }
    }

    printf("T14 Logic sync is bar-locked: a pendulum / random / probability pattern depends only on the host position\n");
    {
        SeqSettings d = st; d.followHost = true; d.play = false; d.direction = 2; d.pendRepeat = true; d.prob = 0.6f; d.seed = 9; d.loopLen = 5;
        StepSequencer a, b; fill(a, {60, 62, 64, 65, 67, 69, 71}); fill(b, {60, 62, 64, 65, 67, 69, 71});
        const auto full = run(a, d, bpm, sr, 512, 16 * 24000, true, 0.0);
        const auto late = run(b, d, bpm, sr, 700, 12 * 24000, true, 4.0); // starts 4 quarters (96000 samples) in
        std::vector<Ev> ref; for (auto& x : full) if (x.pos >= 96000) ref.push_back({x.pos - 96000, x.on, x.note});
        CHECK(!ref.empty() && sameEvents(ref, late), "starting at bar 2 should reproduce the same events (%zu vs %zu)", ref.size(), late.size());
        d.direction = 3; StepSequencer c, e2; fill(c, {60, 62, 64, 65, 67, 69, 71}); fill(e2, {60, 62, 64, 65, 67, 69, 71});
        const auto fullR = run(c, d, bpm, sr, 512, 16 * 24000, true, 0.0), lateR = run(e2, d, bpm, sr, 333, 12 * 24000, true, 4.0);
        std::vector<Ev> refR; for (auto& x : fullR) if (x.pos >= 96000) refR.push_back({x.pos - 96000, x.on, x.note});
        CHECK(!refR.empty() && sameEvents(refR, lateR), "random direction must also depend only on the host position");
    }

    printf("T15 per-step probability: save format, old (v0.3) patterns, clamping, editing\n");
    {
        StepSequencer s; s.recordNote(60, 100); s.setProb(0, 75); s.addRest(); s.recordNote(67, 80); s.setProb(2, 0);
        CHECK(s.serialize() == "60:100:75,r,67:80:0", "serialized '%s'", s.serialize().c_str());
        StepSequencer r; r.deserialize("60:100:75,r:50,67:80"); CHECK(r.serialize() == "60:100:75,r:50,67:80", "round trip '%s'", r.serialize().c_str());
        r.setProb(1, 20); CHECK(r.serialize() == "60:100:75,r:50,67:80", "setProb on a rest must be ignored, got '%s'", r.serialize().c_str());
        StepSequencer old; old.deserialize("60:100,r,67:80,64");
        SeqStep stp[32]; int len, idx; old.snapshot(stp, len, idx);
        CHECK(len == 4 && stp[0].prob == 100 && stp[1].prob == 100 && stp[2].prob == 100 && stp[3].prob == 100, "v0.3 pattern should load at 100%%");
        CHECK(old.serialize() == "60:100,r,67:80,64:100", "v0.3 pattern re-saves unchanged apart from filling in a velocity, got '%s'", old.serialize().c_str());
        StepSequencer cl; cl.deserialize("60:100:250,61:100:-5"); cl.snapshot(stp, len, idx);
        CHECK(stp[0].prob == 100 && stp[1].prob == 0, "probabilities clamp to 0..100 (%d, %d)", stp[0].prob, stp[1].prob);
        StepSequencer t; t.recordNote(60, 100); t.setProb(0, 40); t.toggleRest(0); t.toggleRest(0); t.snapshot(stp, len, idx);
        CHECK(!stp[0].rest && stp[0].prob == 40, "toggling rest and back keeps the probability");
        t.setProb(0, 500); t.snapshot(stp, len, idx); CHECK(stp[0].prob == 100, "setProb clamps high");
        t.setProb(0, -9); t.snapshot(stp, len, idx); CHECK(stp[0].prob == 0, "setProb clamps low");
        t.setProb(5, 10); t.deleteLast(); t.recordNote(61, 90); t.snapshot(stp, len, idx); CHECK(stp[0].prob == 100, "a fresh step starts at 100%%");
    }

    // ================================ v0.5: scale quantizing and transpose ================================
    const int kC = 0, kD = 2, kCMaj = 0, kCMajPent = 16, kChromatic = 27, kAeolian = 5;

    printf("T16 scale table and helpers: 28 scales, snapping (ties go down), editing steps, transpose then quantize\n");
    {
        CHECK(StepSequencer::kNumScales == 28, "28 scales");
        CHECK(std::string(StepSequencer::scaleName(0)) == "Major (Ionian)" && std::string(StepSequencer::scaleName(5)) == "Aeolian (Natural Minor)"
              && std::string(StepSequencer::scaleName(12)) == "Double Harmonic (Byzantine)" && std::string(StepSequencer::scaleName(27)) == "Chromatic", "scale names");
        // C major
        for (int n : {60, 62, 64, 65, 67, 69, 71, 72}) CHECK(StepSequencer::inScale(n, kC, kCMaj), "%d is in C major", n);
        for (int n : {61, 63, 66, 68, 70}) CHECK(!StepSequencer::inScale(n, kC, kCMaj), "%d is not in C major", n);
        // snapping: between two tones goes DOWN, otherwise to the nearest
        const struct { int in, out; } cmaj[] = {{60, 60}, {61, 60}, {63, 62}, {66, 65}, {68, 67}, {70, 69}};
        for (auto& c : cmaj) CHECK(StepSequencer::quantizeNote(c.in, kC, kCMaj) == c.out, "C major: %d -> %d (got %d)", c.in, c.out, StepSequencer::quantizeNote(c.in, kC, kCMaj));
        CHECK(StepSequencer::quantizeNote(65, kC, kCMajPent) == 64 && StepSequencer::quantizeNote(66, kC, kCMajPent) == 67, "C major pentatonic: 65 -> 64, 66 -> 67 (nearest, not always down)");
        CHECK(StepSequencer::quantizeNote(60, kD, kCMaj) == 59 && StepSequencer::quantizeNote(61, kD, kCMaj) == 61, "D major: 60 -> 59 (tie goes down), 61 is already in the scale");
        CHECK(StepSequencer::quantizeNote(0, 1, kCMajPent) == 1 && StepSequencer::quantizeNote(127, kC, kCMaj) == 127 && StepSequencer::quantizeNote(126, kC, kCMaj) == 125, "keyboard ends");
        CHECK(StepSequencer::quantizeNote(61, kC, -1) == 61 && StepSequencer::quantizeNote(61, kC, kChromatic) == 61, "scale off and Chromatic leave notes alone");
        CHECK(StepSequencer::quantizeNote(200, kC, -1) == 127 && StepSequencer::quantizeNote(-5, kC, -1) == 0, "out-of-range notes are clamped");
        // exhaustive: every scale x root x note must land on a scale tone that no other tone beats (ties down)
        int bad = 0;
        for (int sc = 0; sc < StepSequencer::kNumScales; ++sc)
            for (int rt = 0; rt < 12; ++rt)
                for (int n = 0; n < 128; ++n)
                {
                    int best = -1, bestDist = 1000;
                    for (int m = 0; m < 128; ++m)
                        if (StepSequencer::inScale(m, rt, sc) && (std::abs(m - n) < bestDist)) { bestDist = std::abs(m - n); best = m; } // first hit wins = lowest on a tie
                    if (StepSequencer::quantizeNote(n, rt, sc) != best) ++bad;
                }
        CHECK(bad == 0, "exhaustive snap check failed %d times", bad);
        // editing steps
        CHECK(StepSequencer::scaleStep(60, 1, kC, kCMaj) == 62 && StepSequencer::scaleStep(60, -1, kC, kCMaj) == 59, "scale step from 60");
        CHECK(StepSequencer::scaleStep(61, 1, kC, kCMaj) == 62 && StepSequencer::scaleStep(61, -1, kC, kCMaj) == 60, "scale step from a note outside the scale");
        CHECK(StepSequencer::scaleStep(127, 1, kC, kCMaj) == 127 && StepSequencer::scaleStep(0, -1, kC, kCMaj) == 0, "scale step stays put at the ends");
        CHECK(StepSequencer::scaleStep(60, 1, kC, -1) == 61 && StepSequencer::scaleStep(60, -1, kC, -1) == 59 && StepSequencer::scaleStep(127, 1, kC, -1) == 127, "scale off = semitone steps");
        // playedNote: transpose first, then snap
        CHECK(StepSequencer::playedNote(60, 0, kC, kCMaj) == 60 && StepSequencer::playedNote(60, 2, kC, kCMaj) == 62 && StepSequencer::playedNote(60, 1, kC, kCMaj) == 60, "playedNote in C major");
        CHECK(StepSequencer::playedNote(60, 7, kC, -1) == 67 && StepSequencer::playedNote(60, -12, kC, -1) == 48, "transpose without a scale is a plain shift");
        CHECK(StepSequencer::playedNote(120, 20, kC, -1) == 127 && StepSequencer::playedNote(5, -20, kC, -1) == 0, "transposed notes are kept inside 0..127");
        CHECK(StepSequencer::playedNote(57, 0, 9, kAeolian) == 57 && StepSequencer::playedNote(58, 0, 9, kAeolian) == 57, "A natural minor");
    }

    printf("T17 the sequencer plays transposed / quantized notes and leaves the stored pattern alone\n");
    {
        StepSequencer s; fill(s, {60, 62, 64, 65}); const std::string before = s.serialize();
        SeqSettings d = st; d.gate = 0.5f;
        auto play = [&](int scale, int root, int transpose) {
            StepSequencer q; fill(q, {60, 62, 64, 65}); SeqSettings x = d; x.scale = scale; x.root = root; x.transpose = transpose;
            return run(q, x, bpm, sr, 512, 4 * step16, false);
        };
        CHECK(onNotes(play(-1, 0, 0)) == std::vector<int>({60, 62, 64, 65}), "defaults play the pattern as recorded");
        CHECK(onNotes(play(kCMaj, kC, 0)) == std::vector<int>({60, 62, 64, 65}), "a pattern already in the scale is untouched");
        CHECK(onNotes(play(-1, 0, 3)) == std::vector<int>({63, 65, 67, 68}), "transpose +3, no scale");
        CHECK(onNotes(play(kCMaj, kC, 1)) == std::vector<int>({60, 62, 65, 65}), "transpose +1 then C major: 61->60, 63->62, 65, 66->65");
        CHECK(onNotes(play(kCMaj, kC, 2)) == std::vector<int>({62, 64, 65, 67}), "transpose +2 then C major: 62, 64, 66->65, 67");
        CHECK(onNotes(play(kCMaj, kC, -12)) == std::vector<int>({48, 50, 52, 53}), "transpose -12 (an octave down)");
        CHECK(onNotes(play(kCMajPent, kC, 0)) == std::vector<int>({60, 62, 64, 64}), "C major pentatonic: F (65) snaps down to E (64)");
        // every note-off carries the note that actually sounded
        const auto e = play(kCMaj, kC, 1); bool offsMatch = true; int last = -1;
        for (auto& x : e) { if (x.on) last = x.note; else offsMatch = offsMatch && x.note == last; }
        CHECK(offsMatch && !e.empty(), "note-offs use the transposed/quantized note");
        // nothing above touched the stored pattern
        CHECK(s.serialize() == before, "stored pattern unchanged");
        // same events at any block size, and relative to the host position in Logic sync
        SeqSettings x = d; x.scale = kAeolian; x.root = 9; x.transpose = 5; x.direction = 2;
        StepSequencer a, b, c; fill(a, {57, 60, 62, 64, 67}); fill(b, {57, 60, 62, 64, 67}); fill(c, {57, 60, 62, 64, 67});
        const auto ea = run(a, x, bpm, sr, 64, 30 * step16, false), eb = run(b, x, bpm, sr, 997, 30 * step16, false);
        CHECK(!ea.empty() && sameEvents(ea, eb), "block-size independence with scale + transpose");
        x.followHost = true; x.play = false;
        const auto fullL = run(a, x, bpm, sr, 512, 8 * 24000, true, 0.0), lateL = run(c, x, bpm, sr, 333, 4 * 24000, true, 4.0);
        std::vector<Ev> refL; for (auto& y : fullL) if (y.pos >= 96000) refL.push_back({y.pos - 96000, y.on, y.note});
        CHECK(!refL.empty() && sameEvents(refL, lateL), "Logic sync still depends only on the host position with scale + transpose");
    }

    printf("T18 changing transpose / scale while a note sounds: the note that is held is the one that gets released\n");
    {
        StepSequencer s; fill(s, {60, 62}); SeqEvent buf[64]; SeqHostInfo h; h.hostPlaying = true; h.havePpq = true; h.bpm = bpm;
        SeqSettings d = st; d.gate = 1.f; // legato: a note ends exactly when the next starts
        int c = s.process(sr, 3000, h, d, buf, 64);
        CHECK(c == 1 && buf[0].on && buf[0].note == 60, "step 1 sounds 60");
        d.transpose = 5; d.scale = kCMaj; // changed mid-note: nothing happens until the next step
        c = s.process(sr, 3000, h, d, buf, 64);
        CHECK(c == 0, "no event until the next step boundary (got %d)", c);
        c = s.process(sr, 3000, h, d, buf, 64);
        CHECK(c == 2 && !buf[0].on && buf[0].note == 60 && buf[1].on && buf[1].note == 67, "off for the note that was sounding (60), then on 62+5=67");
        d.transpose = 0; d.scale = -1; s.resetTransport();
        c = s.process(sr, 3000, h, d, buf, 64); CHECK(c >= 1 && buf[c - 1].on && buf[c - 1].note == 60, "back to the stored pattern after resetting");
    }

    printf("T19 isRunning(): follows PLAY / host transport, empty pattern and record\n");
    {
        StepSequencer s; SeqEvent buf[64]; SeqHostInfo h; h.hostPlaying = true; h.havePpq = true; h.bpm = bpm; SeqSettings d = st;
        CHECK(!s.isRunning(), "idle at the start");
        s.process(sr, 512, h, d, buf, 64); CHECK(!s.isRunning(), "an empty pattern never runs");
        fill(s, {60, 62});
        s.process(sr, 512, h, d, buf, 64); CHECK(s.isRunning(), "PLAY with a pattern runs");
        SeqSettings m = d; m.mute = true; s.process(sr, 512, h, m, buf, 64); CHECK(s.isRunning(), "mute keeps it running (it only silences the notes)");
        SeqSettings r = d; r.recording = true; s.process(sr, 512, h, r, buf, 64); CHECK(!s.isRunning(), "record armed: not running");
        s.process(sr, 512, h, d, buf, 64); CHECK(s.isRunning(), "running again");
        SeqSettings stop = d; stop.play = false; s.process(sr, 512, h, stop, buf, 64); CHECK(!s.isRunning(), "PLAY off: not running");
        SeqSettings f = d; f.followHost = true; f.play = false; h.hostPlaying = false; s.process(sr, 512, h, f, buf, 64); CHECK(!s.isRunning(), "Logic sync, host stopped");
        h.hostPlaying = true; s.process(sr, 512, h, f, buf, 64); CHECK(s.isRunning(), "Logic sync, host playing");
    }


    // ================================ v0.6: per-step expression, swing ================================
    auto sameNear = [](const std::vector<Ev>& a, const std::vector<Ev>& b, long long tol = 1) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::llabs(a[i].pos - b[i].pos) > tol || a[i].on != b[i].on || a[i].note != b[i].note || a[i].vel != b[i].vel) return false;
        return true;
    };
    auto posOf = [](const std::vector<Ev>& e, bool on) { std::vector<long long> v; for (auto& x : e) if (x.on == on) v.push_back(x.pos); return v; };
    auto nearVec = [](const std::vector<long long>& got, const std::vector<long long>& want, long long tol = 1) {
        if (got.size() != want.size()) return false;
        for (size_t i = 0; i < got.size(); ++i) if (std::llabs(got[i] - want[i]) > tol) return false;
        return true;
    };
    auto velsOf = [](const std::vector<Ev>& e) { std::vector<int> v; for (auto& x : e) if (x.on) v.push_back(x.vel); return v; };
    auto offsMatchOns = [](const std::vector<Ev>& e) { bool ok = !e.empty(); int last = -1; for (auto& x : e) { if (x.on) last = x.note; else ok = ok && x.note == last; } return ok; };

    printf("T20 v0.6 per-step values: save format, rests keep them, old patterns, clamping, editing, condition list\n");
    {
        StepSequencer s; s.recordNote(60, 100);
        CHECK(s.serialize() == "60:100", "untouched step saves as before ('%s')", s.serialize().c_str());
        s.setRatchet(0, 3); s.setStepGate(0, 50); s.toggleAccent(0); s.setOctChance(0, 30); s.setCondition(0, 2, 3);
        CHECK(s.serialize() == "60:100:x3:g50:a:o30:c2/3", "serialized '%s'", s.serialize().c_str());
        s.setProb(0, 75);
        CHECK(s.serialize() == "60:100:75:x3:g50:a:o30:c2/3", "with probability '%s'", s.serialize().c_str());
        StepSequencer r; r.deserialize(s.serialize()); CHECK(r.serialize() == s.serialize(), "round trip '%s'", r.serialize().c_str());
        SeqStep stp[32]; int len, idx; r.snapshot(stp, len, idx);
        CHECK(len == 1 && stp[0].note == 60 && stp[0].vel == 100 && stp[0].prob == 75 && stp[0].ratchet == 3 && stp[0].gate == 50 && stp[0].accent
              && stp[0].octChance == 30 && stp[0].condA == 2 && stp[0].condB == 3, "all fields restored");
        s.toggleRest(0);
        CHECK(s.serialize() == "r:75:x3:g50:a:o30:c2/3", "a rest keeps its values ('%s')", s.serialize().c_str());
        StepSequencer r2; r2.deserialize(s.serialize()); CHECK(r2.serialize() == s.serialize(), "rest with values round trip");
        s.toggleRest(0); CHECK(s.serialize() == "60:100:75:x3:g50:a:o30:c2/3", "note -> rest -> note keeps the values");
        StepSequencer q; q.recordNote(60, 100); q.addRest(); q.recordNote(64, 100);
        q.setRatchet(1, 4); q.setStepGate(1, 50); q.toggleAccent(1); q.setOctChance(1, 50); q.setCondition(1, 2, 4); // on a rest
        q.setRatchet(7, 4); q.setStepGate(-1, 50); q.toggleAccent(99); q.setOctChance(40, 50); q.setCondition(-3, 2, 4); // out of range
        CHECK(q.serialize() == "60:100,r,64:100", "setters ignore rests and out-of-range steps ('%s')", q.serialize().c_str());
        StepSequencer c;
        c.deserialize("60:100:x99,61:100:g2,62:100:g0,63:100:o250,64:100:c9/3,65:100:c2/20,66:100:c0/0,67:100:z7:x2,68:100:x");
        CHECK(c.serialize() == "60:100:x8,61:100:g5,62:100,63:100:o100,64:100:c3/3,65:100:c2/8,66:100,67:100:x2,68:100", "clamping on load: '%s'", c.serialize().c_str());
        StepSequencer v; v.deserialize("60:100:75,r:50,67:80,64"); // patterns from v0.3 / v0.4 / v0.5 have none of the new fields
        v.snapshot(stp, len, idx);
        bool plain = len == 4; for (int i = 0; i < 4; ++i) plain = plain && stp[i].ratchet == 1 && stp[i].gate == 0 && !stp[i].accent && stp[i].octChance == 0 && stp[i].condA == 1 && stp[i].condB == 1;
        CHECK(plain && v.serialize() == "60:100:75,r:50,67:80,64:100", "old patterns load with neutral v0.6 values ('%s')", v.serialize().c_str());
        StepSequencer t; t.recordNote(60, 100); t.setRatchet(0, 5); t.setStepGate(0, 2); t.deleteLast(); t.recordNote(61, 90); t.snapshot(stp, len, idx);
        CHECK(stp[0].ratchet == 1 && stp[0].gate == 0, "a fresh step starts neutral");
        t.setRatchet(0, 99); t.snapshot(stp, len, idx); CHECK(stp[0].ratchet == 8, "setRatchet clamps high");
        t.setRatchet(0, -2); t.snapshot(stp, len, idx); CHECK(stp[0].ratchet == 1, "setRatchet clamps low");
        t.setStepGate(0, 3); t.snapshot(stp, len, idx); CHECK(stp[0].gate == 5, "a step gate below 5 becomes 5");
        t.setStepGate(0, 0); t.snapshot(stp, len, idx); CHECK(stp[0].gate == 0, "0 = follow the GATE knob");

        const int expA[] = {1, 1, 2, 1, 2, 3, 1, 2, 3, 4}, expB[] = {1, 2, 2, 3, 3, 3, 4, 4, 4, 4};
        for (int i = 0; i < 10; ++i) { int a, b; StepSequencer::conditionFromIndex(i, a, b); CHECK(a == expA[i] && b == expB[i], "condition %d is %d:%d, got %d:%d", i, expA[i], expB[i], a, b); }
        { int a, b; StepSequencer::conditionFromIndex(35, a, b); CHECK(a == 8 && b == 8, "last condition 8:8");
          StepSequencer::conditionFromIndex(28, a, b); CHECK(a == 1 && b == 8, "condition 28 is 1:8");
          StepSequencer::conditionFromIndex(-4, a, b); CHECK(a == 1 && b == 1, "below the list = always");
          StepSequencer::conditionFromIndex(99, a, b); CHECK(a == 8 && b == 8, "above the list = last"); }
        for (int i = 0; i < StepSequencer::kNumConditions; ++i)
        { int a, b; StepSequencer::conditionFromIndex(i, a, b); CHECK(a >= 1 && a <= b && b <= 8 && StepSequencer::conditionToIndex(a, b) == i, "condition index %d round trip", i); }
        CHECK(StepSequencer::kNumConditions == 36 && StepSequencer::conditionToIndex(5, 3) == StepSequencer::conditionToIndex(3, 3) && StepSequencer::conditionToIndex(1, 1) == 0, "index helpers clamp");
        CHECK(StepSequencer::passIndex(-1, 4, 0, false) == -1 && StepSequencer::passIndex(-4, 4, 0, false) == -1 && StepSequencer::passIndex(-5, 4, 0, false) == -2
              && StepSequencer::passIndex(0, 4, 0, false) == 0 && StepSequencer::passIndex(3, 4, 1, false) == 0 && StepSequencer::passIndex(4, 4, 3, false) == 1, "pass index (forward/backward/random, negative counters)");
        CHECK(StepSequencer::passIndex(5, 4, 2, false) == 0 && StepSequencer::passIndex(6, 4, 2, false) == 1 && StepSequencer::passIndex(7, 4, 2, true) == 0
              && StepSequencer::passIndex(8, 4, 2, true) == 1 && StepSequencer::passIndex(5, 1, 2, false) == 5 && StepSequencer::passIndex(3, 2, 2, false) == 1, "pass index (pendulum periods 2N-2 and 2N)");
        CHECK(StepSequencer::conditionPasses(1, 3, 0) && StepSequencer::conditionPasses(1, 3, 3) && StepSequencer::conditionPasses(2, 3, 1) && StepSequencer::conditionPasses(3, 3, 2)
              && !StepSequencer::conditionPasses(1, 3, 1) && !StepSequencer::conditionPasses(2, 3, 0) && StepSequencer::conditionPasses(1, 1, 7) && StepSequencer::conditionPasses(1, 3, -3)
              && StepSequencer::conditionPasses(2, 3, -2), "condition logic");
        CHECK(StepSequencer::octaveShift(0, 0.9) == 12 && StepSequencer::octaveShift(1, 0.1) == -12 && StepSequencer::octaveShift(2, 0.2) == 12 && StepSequencer::octaveShift(2, 0.7) == -12
              && StepSequencer::octaveShift(3, 0.5) == 24 && StepSequencer::octaveShift(4, 0.5) == -24 && StepSequencer::octaveShift(5, 0.1) == 24 && StepSequencer::octaveShift(5, 0.9) == -24
              && std::string(StepSequencer::octModeName(0)) == "Up 1 octave" && std::string(StepSequencer::octModeName(5)) == "Up or down 2 octaves", "octave shift modes");
    }

    printf("T21 ratchets: repeat spacing and gates, block-size independence, cancelled by mute / record / stop, with swing\n");
    {
        SeqSettings d = st; // 1/16 = 6000 samples, gate 50 %
        { StepSequencer a; fill(a, {60}); a.setRatchet(0, 3);
          const auto e = run(a, d, bpm, sr, 512, 2 * step16, false);
          CHECK(nearVec(posOf(e, true), {0, 2000, 4000, 6000, 8000, 10000}), "ratchet 3: note-ons every 2000 samples");
          CHECK(nearVec(posOf(e, false), {1000, 3000, 5000, 7000, 9000, 11000}), "ratchet 3: each repeat is gated to 50 %% of its own length");
          CHECK(offsMatchOns(e), "note-offs carry the note that sounded"); }
        { StepSequencer a; fill(a, {60}); a.setRatchet(0, 8);
          const auto e = run(a, d, bpm, sr, 512, step16, false);
          CHECK(countOn(e) == 8 && nearVec(posOf(e, true), {0, 750, 1500, 2250, 3000, 3750, 4500, 5250}), "ratchet 8: eight repeats 750 samples apart"); }
        { SeqSettings leg = d; leg.gate = 1.f; StepSequencer a; fill(a, {60}); a.setRatchet(0, 3);
          const auto e = run(a, leg, bpm, sr, 512, 2 * step16, false);
          CHECK(nearVec(posOf(e, true), {0, 2000, 4000, 6000, 8000, 10000}) && nearVec(posOf(e, false), {2000, 4000, 6000, 8000, 10000}), "legato ratchet: each repeat ends exactly where the next begins");
          bool paired = true; for (size_t i = 0; i + 1 < e.size(); ++i) if (!e[i].on) paired = paired && e[i + 1].on && e[i].pos == e[i + 1].pos;
          CHECK(paired, "legato ratchet: off and next on on the same sample"); }
        { // a mixed pattern gives the same events at any block size, including blocks that cut a ratchet in half
          auto mk = [&](StepSequencer& q) { fill(q, {60, 62, -1, 65, 67, 69}); q.setRatchet(0, 3); q.setRatchet(1, 2); q.setRatchet(3, 4); q.setRatchet(5, 8); q.setStepGate(3, 90); };
          StepSequencer r0; mk(r0); const auto ref = run(r0, d, bpm, sr, 512, 24 * step16, false);
          for (int bs : {64, 997, 1500, 4096, 24000})
          { StepSequencer q; mk(q); const auto e = run(q, d, bpm, sr, bs, 24 * step16, false); CHECK(!ref.empty() && sameNear(ref, e), "ratchets at block size %d differ (%zu vs %zu events)", bs, ref.size(), e.size()); } }
        { StepSequencer a; fill(a, {60}); a.setRatchet(0, 4); a.setProb(0, 0);
          CHECK(run(a, d, bpm, sr, 512, 8 * step16, false).empty(), "a step that misses its roll makes no repeats either"); }
        { StepSequencer q; fill(q, {60}); q.setRatchet(0, 4); SeqEvent buf[64]; SeqHostInfo h; h.hostPlaying = true; h.havePpq = true; h.bpm = bpm;
          SeqSettings m = d; int c = q.process(sr, 2000, h, m, buf, 64); // on@0 off@750 on@1500
          CHECK(c == 3 && buf[0].on && !buf[1].on && buf[2].on && buf[2].offset == 1500, "start of a ratcheted step (%d events)", c);
          m.mute = true; c = q.process(sr, 6000, h, m, buf, 64);
          CHECK(c == 1 && !buf[0].on && buf[0].offset == 0, "MUTE releases the sounding repeat and cancels the rest of the ratchet (%d events)", c);
          StepSequencer q2; fill(q2, {60}); q2.setRatchet(0, 4); c = q2.process(sr, 2000, h, d, buf, 64); SeqSettings rc = d; rc.recording = true;
          c = q2.process(sr, 6000, h, rc, buf, 64); CHECK(c == 1 && !buf[0].on, "record armed cancels the ratchet (%d events)", c);
          StepSequencer q3; fill(q3, {60}); q3.setRatchet(0, 4); c = q3.process(sr, 2000, h, d, buf, 64); SeqSettings sp = d; sp.play = false;
          c = q3.process(sr, 6000, h, sp, buf, 64); CHECK(c == 1 && !buf[0].on, "stopping cancels the ratchet (%d events)", c);
          StepSequencer q4; fill(q4, {60}); q4.setRatchet(0, 4); c = q4.process(sr, 2000, h, d, buf, 64); q4.resetTransport();
          c = q4.process(sr, 1000, h, d, buf, 64); int ons = 0; for (int i = 0; i < c; ++i) ons += buf[i].on; CHECK(ons == 1 && buf[0].offset == 0, "after a transport reset the pattern starts again at step 1 (%d ons)", ons); }
        { // with swing: a step lasts until the next one starts, and its repeats are spread over that time
          SeqSettings w = d; w.swing = 50.f + 50.f / 3.f; StepSequencer a; fill(a, {60, 62}); a.setRatchet(0, 2);
          const auto e = run(a, w, bpm, sr, 512, 16000, false);
          CHECK(nearVec(posOf(e, true), {0, 4000, 8000, 12000}) && nearVec(posOf(e, false), {2000, 6000, 10000, 14000}), "ratchet inside a swung step (step 1 starts at 8000)");
          CHECK(onNotes(e) == std::vector<int>({60, 60, 62, 60}), "ratchet repeats keep the step's note"); }
    }

    printf("T22 swing: odd steps start late (50 %% straight, 66.7 %% triplet, 75 %% dotted), every step plays once, block-size independent, bar-locked\n");
    {
        auto pat = [&](StepSequencer& q) { fill(q, {60, 62, 64, 65}); };
        SeqSettings d = st;
        { StepSequencer a, b; pat(a); pat(b); SeqSettings w = d; w.swing = 50.f;
          CHECK(sameEvents(run(a, d, bpm, sr, 512, 16 * step16, false), run(b, w, bpm, sr, 512, 16 * step16, false)), "swing 50 %% = no swing"); }
        { StepSequencer a; pat(a); SeqSettings w = d; w.swing = 50.f + 50.f / 3.f;
          const auto e = run(a, w, bpm, sr, 512, 8 * step16, false);
          CHECK(nearVec(posOf(e, true), {0, 8000, 12000, 20000, 24000, 32000, 36000, 44000}), "66.7 %%: odd steps 2000 samples late");
          CHECK(nearVec(posOf(e, false), {4000, 10000, 16000, 22000, 28000, 34000, 40000, 46000}), "gate 50 %% of each step's real length (8000 for even steps, 4000 for odd)");
          CHECK(onNotes(e) == std::vector<int>({60, 62, 64, 65, 60, 62, 64, 65}), "swing keeps the order"); }
        { StepSequencer a; pat(a); SeqSettings w = d; w.swing = 75.f;
          CHECK(nearVec(posOf(run(a, w, bpm, sr, 512, 4 * step16, false), true), {0, 9000, 12000, 21000}), "75 %%: odd steps 3000 samples (half a step) late"); }
        { StepSequencer a; pat(a); SeqSettings w = d; w.swing = 99.f; // out of range is limited to 75
          CHECK(nearVec(posOf(run(a, w, bpm, sr, 512, 2 * step16, false), true), {0, 9000}), "swing above 75 %% is limited to 75 %%"); }
        { StepSequencer a; pat(a); SeqSettings w = d; w.swing = 66.f; w.gate = 1.f; // legato: each note runs into the next
          const auto e = run(a, w, bpm, sr, 512, 8 * step16, false); bool paired = true;
          for (size_t i = 0; i + 1 < e.size(); ++i) if (!e[i].on) paired = paired && e[i + 1].on && e[i].pos == e[i + 1].pos;
          CHECK(paired && countOn(e) == 8, "legato with swing: still off-then-on on the same sample"); }
        { SeqSettings w = d; w.swing = 62.f; w.division = 6; // triplet steps too
          StepSequencer r0; pat(r0); const auto ref = run(r0, w, bpm, sr, 512, 96000, false);
          CHECK(countOn(ref) == 24, "1/16T at 120 bpm: 24 steps in 96000 samples, each exactly once (%d)", countOn(ref));
          for (int bs : {64, 333, 997, 4096}) { StepSequencer q; pat(q); const auto e = run(q, w, bpm, sr, bs, 96000, false); CHECK(sameNear(ref, e), "swing at block size %d differs", bs); } }
        { // every step plays exactly once even when a delayed step lands in a later block than its nominal time
          SeqSettings w = d; w.swing = 75.f; StepSequencer q; fill(q, {60}); const auto e = run(q, w, bpm, sr, 3001, 64 * step16, false);
          CHECK(countOn(e) == 64, "64 steps at 75 %% swing in odd-sized blocks: %d note-ons", countOn(e)); }
        { // Logic sync: starting part-way through the delayed region still plays the step that is waiting
          SeqSettings w = d; w.followHost = true; w.play = false; w.swing = 50.f + 50.f / 3.f; StepSequencer q; pat(q);
          const auto e = run(q, w, bpm, sr, 512, 12000, true, 0.26); // step 1 is nominally at 0.25 quarter notes, really at 0.3333
          CHECK(!e.empty() && e[0].on && e[0].note == 62 && near(e[0].pos, 1760, 2), "first note is the waiting step 2 at ~1760 (got note %d at %lld)", e.empty() ? -1 : e[0].note, e.empty() ? -1 : e[0].pos);
          StepSequencer a, b; pat(a); pat(b);
          const auto full = run(a, w, bpm, sr, 512, 16 * 24000, true, 0.0), late = run(b, w, bpm, sr, 700, 12 * 24000, true, 4.0);
          std::vector<Ev> ref; for (auto& x : full) if (x.pos >= 96000) ref.push_back({x.pos - 96000, x.on, x.note, x.vel});
          CHECK(!ref.empty() && sameNear(ref, late), "Logic sync with swing depends only on the host position (%zu vs %zu events)", ref.size(), late.size()); }
    }

    printf("T23 per-step gate: its own length, 100 = legato, 0 = follow the GATE knob\n");
    {
        StepSequencer a; fill(a, {60, 62, 64}); a.setStepGate(1, 100); a.setStepGate(2, 25);
        auto e = run(a, st, bpm, sr, 512, 3 * step16, false);
        CHECK(nearVec(posOf(e, true), {0, 6000, 12000}) && nearVec(posOf(e, false), {3000, 12000, 13500}), "knob 50 %%, step 2 legato, step 3 at 25 %% (offs at 3000, 12000, 13500)");
        StepSequencer b; fill(b, {60, 62, 64}); b.setStepGate(2, 25);
        CHECK(nearVec(posOf(run(b, st, bpm, sr, 512, 3 * step16, false), false), {3000, 9000, 13500}), "step 2 follows the GATE knob again once its own gate is cleared");
        StepSequencer c; fill(c, {60, 62}); c.setStepGate(0, 80); SeqSettings g = st; g.gate = 0.2f;
        CHECK(nearVec(posOf(run(c, g, bpm, sr, 512, 2 * step16, false), false), {4800, 7200}), "a step's gate can be longer than the knob (80 %% vs 20 %%)");
    }

    printf("T24 accent: velocity boost by the ACCENT amount, capped at 127, all ratchet repeats\n");
    {
        StepSequencer a; a.recordNote(60, 64); a.recordNote(62, 64); a.toggleAccent(1);
        SeqSettings d = st; d.accent = 0.3f;
        CHECK(velsOf(run(a, d, bpm, sr, 512, 4 * step16, false)) == std::vector<int>({64, 102, 64, 102}), "64 + 0.3 x 127 = 102 on the accented step only");
        d.accent = 0.f; CHECK(velsOf(run(a, d, bpm, sr, 512, 2 * step16, false)) == std::vector<int>({64, 64}), "accent amount 0 = no change");
        d.accent = 0.5f; CHECK(velsOf(run(a, d, bpm, sr, 512, 2 * step16, false)) == std::vector<int>({64, 127}), "capped at 127");
        d.accent = 5.f; CHECK(velsOf(run(a, d, bpm, sr, 512, 2 * step16, false)) == std::vector<int>({64, 127}), "amount above 1 is limited to 1");
        a.toggleAccent(1); d.accent = 0.3f; CHECK(velsOf(run(a, d, bpm, sr, 512, 2 * step16, false)) == std::vector<int>({64, 64}), "accent can be toggled off again");
        StepSequencer r; r.recordNote(60, 64); r.toggleAccent(0); r.setRatchet(0, 3);
        CHECK(velsOf(run(r, d, bpm, sr, 512, step16, false)) == std::vector<int>({102, 102, 102}), "every repeat of an accented ratchet is accented");
    }

    printf("T25 octave jump: chance per step, direction modes, seeds, interplay with transpose / scale / clamping\n");
    {
        SeqSettings d = st; d.seed = 7;
        auto playOne = [&](int note, int chance, SeqSettings x, int steps) {
            StepSequencer q; q.recordNote(note, 100); q.setOctChance(0, chance); return run(q, x, bpm, sr, 4096, (long long) steps * step16, false);
        };
        CHECK(onNotes(playOne(60, 0, d, 50)) == std::vector<int>(50, 60), "chance 0: never jumps");
        { const int modes[] = {0, 1, 3, 4}, want[] = {72, 48, 84, 36};
          for (int i = 0; i < 4; ++i) { SeqSettings x = d; x.octMode = modes[i]; CHECK(onNotes(playOne(60, 100, x, 20)) == std::vector<int>(20, want[i]), "mode %d at chance 100 -> %d", modes[i], want[i]); } }
        { SeqSettings x = d; x.octMode = 2; const auto n = onNotes(playOne(60, 100, x, 1000)); int up = 0, dn = 0, other = 0;
          for (int v : n) { if (v == 72) ++up; else if (v == 48) ++dn; else ++other; }
          CHECK(other == 0 && up > 420 && up < 580 && dn > 420 && dn < 580, "up-or-down mode: %d up, %d down, %d other of 1000", up, dn, other); }
        { SeqSettings x = d; x.octMode = 5; const auto n = onNotes(playOne(60, 100, x, 400)); int up = 0, dn = 0, other = 0;
          for (int v : n) { if (v == 84) ++up; else if (v == 36) ++dn; else ++other; }
          CHECK(other == 0 && up > 150 && dn > 150, "up-or-down 2 octaves: %d up, %d down, %d other", up, dn, other); }
        { SeqSettings x = d; x.octMode = 0; const auto n = onNotes(playOne(60, 40, x, 2000)); int jumped = 0; for (int v : n) jumped += v == 72;
          CHECK(jumped > 700 && jumped < 900 && (int) n.size() == 2000, "chance 40 %%: %d of 2000 jumped", jumped); }
        { SeqSettings x = d; x.octMode = 0; const auto e = playOne(60, 50, x, 40); CHECK(offsMatchOns(e), "note-offs use the note that sounded (octave-jumped)"); }
        // the same choices every time with a fixed seed (any block size), different with another seed
        { SeqSettings x = d; x.octMode = 2; auto mk = [&](StepSequencer& q) { fill(q, {60, 62, 64, 65}); for (int i = 0; i < 4; ++i) q.setOctChance(i, 50); };
          StepSequencer a, b, c; mk(a); mk(b); mk(c); SeqSettings y = x; y.seed = 8;
          const auto ea = run(a, x, bpm, sr, 64, 400 * step16, false), eb = run(b, x, bpm, sr, 997, 400 * step16, false), ec = run(c, y, bpm, sr, 512, 400 * step16, false);
          CHECK(sameNear(ea, eb) && !sameNear(ea, ec), "fixed seed repeats the octave choices at any block size; another seed differs"); }
        // the octave jump leaves the pitch class alone, so a scale still fits; transpose is added; the keyboard ends clamp
        { SeqSettings x = d; x.octMode = 0; x.scale = kCMaj; x.root = kC;
          CHECK(onNotes(playOne(61, 100, x, 3)) == std::vector<int>(3, 72), "stored 61 + octave up in C major -> 72 (61 snaps to 60)"); }
        { SeqSettings x = d; x.octMode = 0; x.transpose = 2; CHECK(onNotes(playOne(60, 100, x, 3)) == std::vector<int>(3, 74), "transpose +2 and an octave up = 74"); }
        { SeqSettings x = d; x.octMode = 3; CHECK(onNotes(playOne(120, 100, x, 3)) == std::vector<int>(3, 127), "stored 120 + 2 octaves is kept at 127");
          x.octMode = 4; CHECK(onNotes(playOne(10, 100, x, 3)) == std::vector<int>(3, 0), "stored 10 - 2 octaves is kept at 0"); }
        { StepSequencer s; fill(s, {60, 62}); s.setOctChance(0, 100); const std::string before = s.serialize();
          SeqSettings x = d; run(s, x, bpm, sr, 512, 8 * step16, false);
          CHECK(s.serialize() == before, "playing does not change the stored pattern"); }
    }

    printf("T26 trigger conditions: pass a of every b, loop / pendulum passes, a step that is not due acts like a rest, bar-locked\n");
    {
        SeqSettings d = st;
        const long long pass4 = 4 * step16;
        { StepSequencer a; fill(a, {60, 62, 64, 65}); a.setCondition(0, 1, 3);
          const auto e = run(a, d, bpm, sr, 512, 12 * pass4, false);
          std::vector<long long> p60; for (auto& x : e) if (x.on && x.note == 60) p60.push_back(x.pos);
          CHECK(nearVec(p60, {0, 3 * pass4, 6 * pass4, 9 * pass4}), "1 of 3: plays on passes 1, 4, 7, 10 (%zu plays)", p60.size());
          CHECK(countOn(e, 62) == 12 && countOn(e, 64) == 12 && countOn(e, 65) == 12, "steps without a condition play every pass"); }
        { StepSequencer a; fill(a, {60, 62, 64, 65}); a.setCondition(0, 2, 3);
          std::vector<long long> p60; for (auto& x : run(a, d, bpm, sr, 512, 12 * pass4, false)) if (x.on && x.note == 60) p60.push_back(x.pos);
          CHECK(nearVec(p60, {pass4, 4 * pass4, 7 * pass4, 10 * pass4}), "2 of 3: passes 2, 5, 8, 11"); }
        { StepSequencer a; fill(a, {60, 62, 64, 65}); a.setCondition(0, 3, 3);
          std::vector<long long> p60; for (auto& x : run(a, d, bpm, sr, 512, 12 * pass4, false)) if (x.on && x.note == 60) p60.push_back(x.pos);
          CHECK(nearVec(p60, {2 * pass4, 5 * pass4, 8 * pass4, 11 * pass4}), "3 of 3: passes 3, 6, 9, 12"); }
        { StepSequencer a; fill(a, {60, 62, 64, 65}); a.setCondition(1, 2, 2); a.setCondition(2, 1, 2); // 62 on odd passes, 64 on even
          const auto e = run(a, d, bpm, sr, 512, 8 * pass4, false);
          CHECK(countOn(e, 62) == 4 && countOn(e, 64) == 4 && countOn(e, 60) == 8, "alternating steps 1:2 / 2:2"); }
        { StepSequencer a; fill(a, {60, 62}); a.setCondition(1, 2, 2); SeqSettings g = d; g.gate = 1.f; // pass 1: step 2 is not due
          const auto e = run(a, g, bpm, sr, 512, 2 * 2 * step16, false);
          CHECK(onNotes(e) == std::vector<int>({60, 60, 62}) && nearVec(posOf(e, false), {6000, 18000}), "a step that is not due is a rest: the previous note still ends on time"); }
        { StepSequencer a; fill(a, {60, 62, 64, 65}); a.setCondition(0, 1, 2); SeqSettings l = d; l.loopLen = 2; // passes of 2 steps
          std::vector<long long> p60; for (auto& x : run(a, l, bpm, sr, 512, 8 * 2 * step16, false)) if (x.on && x.note == 60) p60.push_back(x.pos);
          CHECK(nearVec(p60, {0, 4 * step16, 8 * step16, 12 * step16}), "loop of 2: a pass is 2 steps, so 1 of 2 plays every second trip"); }
        { StepSequencer a; fill(a, {60, 62, 64, 65}); a.setCondition(3, 1, 2); SeqSettings p = d; p.direction = 2; // pendulum, a pass = 6 steps; step 4 is the turning point
          std::vector<long long> p65; for (auto& x : run(a, p, bpm, sr, 512, 24 * step16, false)) if (x.on && x.note == 65) p65.push_back(x.pos);
          CHECK(nearVec(p65, {3 * step16, 15 * step16}), "pendulum: passes of 6 steps (the turning point is in steps 4, 10, 16, 22 and only the 1st and 3rd pass play it)"); }
        { StepSequencer a; fill(a, {60, 62, 64, 65}); a.setCondition(3, 1, 2); SeqSettings p = d; p.direction = 2; p.pendRepeat = true; // ends repeated: a pass = 8 steps, step 4 plays twice per pass
          const auto e = run(a, p, bpm, sr, 512, 32 * step16, false);
          CHECK(countOn(e, 65) == 4, "pendulum with the ends repeated: passes of 8 steps (%d plays of the end step in 4 passes, want 4: 2 per due pass)", countOn(e, 65)); }
        { // a condition together with probability: only the due passes roll
          StepSequencer a; fill(a, {60, 62, 64, 65}); a.setCondition(0, 1, 2); a.setProb(0, 50); SeqSettings x = d; x.seed = 3;
          const int n = countOn(run(a, x, bpm, sr, 4096, 1600 * step16, false), 60);
          CHECK(n > 70 && n < 130, "1 of 2 with 50 %%: %d plays in 400 passes (want about 100)", n); }
        { // bar-locked in Logic sync, and every cycle jump starts the pass count again
          SeqSettings x = d; x.followHost = true; x.play = false; x.direction = 2; x.pendRepeat = true; x.loopLen = 5;
          auto mk = [&](StepSequencer& q) { fill(q, {60, 62, 64, 65, 67, 69, 71}); q.setCondition(0, 2, 3); q.setCondition(2, 1, 4); q.setCondition(4, 3, 3); };
          StepSequencer a, b; mk(a); mk(b);
          const auto full = run(a, x, bpm, sr, 512, 16 * 24000, true, 0.0), late = run(b, x, bpm, sr, 700, 12 * 24000, true, 4.0);
          std::vector<Ev> ref; for (auto& y : full) if (y.pos >= 96000) ref.push_back({y.pos - 96000, y.on, y.note, y.vel});
          CHECK(!ref.empty() && sameNear(ref, late), "conditions depend only on the host position");
          const long long cyc = 192000; StepSequencer c; mk(c);
          const auto e = run(c, x, bpm, sr, 512, 2 * cyc, true, 0.0, {{cyc, 0.0}});
          std::vector<Ev> p1, p2; for (auto& y : e) (y.pos < cyc ? p1 : p2).push_back({y.pos < cyc ? y.pos : y.pos - cyc, y.on, y.note, y.vel});
          CHECK(!p1.empty() && sameNear(p1, p2), "a Logic cycle jump restarts the pass count: every cycle plays the same (%zu vs %zu events)", p1.size(), p2.size()); }
    }

    printf("T27 everything at once: notes always alternate on/off (no stuck notes), the event buffer is never full\n");
    {
        std::mt19937 rng(12345);
        auto rnd = [&](int lo, int hi) { return lo + (int) (rng() % (unsigned) (hi - lo + 1)); };
        int worstEvents = 0; long long totalOns = 0; bool allOk = true;
        for (int trial = 0; trial < 60; ++trial)
        {
            StepSequencer q; const int len = rnd(1, 12);
            for (int i = 0; i < len; ++i)
            {
                if (rnd(0, 5) == 0) q.addRest(); else q.recordNote(rnd(30, 100), rnd(20, 120));
                if (rnd(0, 1)) q.setRatchet(i, rnd(1, 8));
                if (rnd(0, 2) == 0) q.setStepGate(i, rnd(0, 100));
                if (rnd(0, 2) == 0) q.toggleAccent(i);
                if (rnd(0, 2) == 0) q.setOctChance(i, rnd(0, 100));
                if (rnd(0, 2) == 0) q.setCondition(i, rnd(1, 8), rnd(1, 8));
                if (rnd(0, 2) == 0) q.setProb(i, rnd(0, 100));
            }
            SeqSettings x; x.play = true; x.division = rnd(0, 7); x.gate = 0.05f + (float) rnd(0, 95) / 100.f; x.loopLen = rnd(0, 6); x.direction = rnd(0, 3);
            x.pendRepeat = rnd(0, 1); x.prob = (float) rnd(50, 100) / 100.f; x.seed = rnd(0, 3); x.swing = 50.f + (float) rnd(0, 25);
            x.accent = (float) rnd(0, 100) / 100.f; x.octMode = rnd(0, 5); x.scale = rnd(-1, 27); x.root = rnd(0, 11); x.transpose = rnd(-12, 12);
            const double tempo = rnd(0, 1) ? 300.0 : 90.0; const int bs = rnd(0, 1) ? 8192 : rnd(64, 1500);
            // run, then stop the transport: everything must be released
            std::vector<SeqEvent> all; static SeqEvent buf[2048]; SeqHostInfo h; h.hostPlaying = true; h.havePpq = true; h.bpm = tempo;
            for (long long t = 0; t < 96000; t += bs)
            {
                const int n = (int) std::min<long long>(bs, 96000 - t);
                const int c = q.process(sr, n, h, x, buf, 2048); worstEvents = std::max(worstEvents, c); if (c >= 2048) allOk = false;
                for (int i = 0; i < c; ++i) all.push_back(buf[i]);
            }
            SeqSettings stop = x; stop.play = false; const int c = q.process(sr, 512, h, stop, buf, 2048);
            for (int i = 0; i < c; ++i) all.push_back(buf[i]);
            bool sounding = false; int soundingNote = -1; int lastOffset = -1;
            for (auto& ev : all)
            {
                if (ev.on) { if (sounding) allOk = false; sounding = true; soundingNote = ev.note; ++totalOns; allOk = allOk && ev.vel >= 1 && ev.vel <= 127 && ev.note >= 0 && ev.note <= 127; }
                else { if (!sounding || ev.note != soundingNote) allOk = false; sounding = false; }
            }
            (void) lastOffset;
            if (sounding) allOk = false;
            if (!allOk) { printf("  (trial %d failed: division %d bs %d tempo %.0f)\n", trial, x.division, bs, tempo); break; }
        }
        { // the worst case a host could send: very fast tempo, huge block, smallest step, 8 repeats. The plugin keeps room for 2048 events per block.
          StepSequencer q; fill(q, {60, 62, 64}); for (int i = 0; i < 3; ++i) q.setRatchet(i, 8);
          SeqSettings x; x.play = true; x.division = 7; x.gate = 0.5f;
          static SeqEvent buf[2048]; SeqHostInfo h; h.hostPlaying = true; h.havePpq = true; h.bpm = 990.0;
          int worst = 0; bool ok = true;
          for (int blk = 0; blk < 12; ++blk) { const int c = q.process(sr, 8192, h, x, buf, 2048); worst = std::max(worst, c); ok = ok && c < 2048; for (int i = 0; i + 1 < c; ++i) ok = ok && buf[i].on != buf[i + 1].on; }
          printf("  (worst case: %d events in one block)\n", worst);
          CHECK(ok && worst > 64, "990 bpm, 1/32T, 8 repeats, 8192-sample blocks: at most %d events per block, far above the old 64-event buffer, still below 2048", worst); }
        CHECK(allOk, "60 random patterns / settings: on and off strictly alternate with matching notes, nothing left sounding, buffer never full");
        printf("  (%lld note-ons in total, at most %d events in one block)\n", totalOns, worstEvents);
    }

    // ---- v0.7 ring views: which ring / slot shows which step (display mapping only, JUCE-free) ----
    {
        printf("T28 2 RINGS layout: every step has its own slot, 1-9 + 26-32 on the left ring, 10-25 on the right, neighbours stay neighbours\n");
        bool distinct = true, roundTrip = true, inverseOk = true;
        int seen[2][RingLayout::kSlots] = {};
        for (int st = 0; st < RingLayout::kSteps; ++st)
        {
            int ring = -1, slot = -1; RingLayout::twoRingsSlot(st, ring, slot);
            if (ring < 0 || ring > 1 || slot < 0 || slot >= RingLayout::kSlots) { distinct = false; continue; }
            if (++seen[ring][slot] > 1) distinct = false;
            if (RingLayout::twoRingsStep(ring, slot) != st) roundTrip = false;
        }
        for (int r = 0; r < 2; ++r) for (int k = 0; k < RingLayout::kSlots; ++k)
        { int ring2 = -1, slot2 = -1; const int st = RingLayout::twoRingsStep(r, k); RingLayout::twoRingsSlot(st, ring2, slot2); if (st < 0 || st > 31 || ring2 != r || slot2 != k) inverseOk = false; }
        CHECK(distinct, "all 32 steps land on distinct slots (16 per ring)");
        CHECK(roundTrip && inverseOk, "step -> slot -> step and slot -> step -> slot both give back what they started with");
        int ringOf[32], slotOf[32];
        for (int st = 0; st < 32; ++st) RingLayout::twoRingsSlot(st, ringOf[st], slotOf[st]);
        bool leftOk = true, rightOk = true;
        for (int st = 0; st < 32; ++st) { const bool left = st <= 8 || st >= 25; if ((ringOf[st] == 0) != left) leftOk = false; if ((ringOf[st] == 1) != (st >= 9 && st <= 24)) rightOk = false; }
        CHECK(leftOk && rightOk, "steps 1-9 and 26-32 are on the left ring, steps 10-25 on the right ring");
        // the order along the path: the next step is the next slot clockwise on the same ring, except at the two hops
        int hops = 0; bool stepsOk = true;
        for (int st = 0; st < 32; ++st)
        {
            const int nx = (st + 1) % 32;
            if (ringOf[st] == ringOf[nx]) { if (slotOf[nx] != (slotOf[st] + 1) % RingLayout::kSlots) stepsOk = false; }
            else { ++hops; if (!(st == 8 || st == 24)) stepsOk = false; }
        }
        CHECK(stepsOk && hops == 2, "consecutive steps are clockwise neighbours on one ring; the only hops between rings are step 9 -> 10 and 25 -> 26 (found %d hops)", hops);
        CHECK(ringOf[0] == 0 && slotOf[0] == 12 && ringOf[8] == 0 && slotOf[8] == 4 && ringOf[9] == 1 && slotOf[9] == 12 &&
              ringOf[24] == 1 && slotOf[24] == 11 && ringOf[25] == 0 && slotOf[25] == 5 && ringOf[31] == 0 && slotOf[31] == 11,
              "step 1 at the left ring's 9 o'clock, step 9 at its 3 o'clock, step 10 at the right ring's 9 o'clock, step 25 at its 8 o'clock, step 26 at the left ring's 4 o'clock, step 32 at its 8 o'clock");
        int r1 = -1, s1 = -1; RingLayout::twoRingsSlot(-5, r1, s1); int r2 = -1, s2 = -1; RingLayout::twoRingsSlot(99, r2, s2);
        CHECK(r1 == ringOf[0] && s1 == slotOf[0] && r2 == ringOf[31] && s2 == slotOf[31], "a step outside 1..32 is clamped");
        CHECK(RingLayout::singleRingStep(0, 0) == 0 && RingLayout::singleRingStep(15, 0) == 15 && RingLayout::singleRingStep(0, 1) == 16 && RingLayout::singleRingStep(15, 1) == 31,
              "single ring: page 1 shows steps 1-16, page 2 steps 17-32");
    }

    printf("T29 pattern slots (v0.8): swapping the pattern while playing + restart(): new pattern from step 1, no stuck note, empty pattern is silent\n");
    {
        // the first run ends at sample 20000: step 4 (65) started at 18000 and its gate (3000 samples) is still open
        StepSequencer s; fill(s, {60, 62, 64, 65});
        auto before = run(s, st, bpm, sr, 512, 20000, false);
        int open = 0; for (auto& e : before) open += e.on ? 1 : -1;
        CHECK(open == 1 && before.back().on && before.back().note == 65, "set-up: note 65 is sounding when the pattern is swapped (open=%d)", open);
        s.deserialize("72:100,r,76:100"); s.restart();
        auto after = run(s, st, bpm, sr, 512, 30000, false);
        CHECK(after.size() >= 4, "events after the swap: %zu", after.size());
        if (after.size() >= 4)
        {
            CHECK(!after[0].on && after[0].note == 65 && after[0].pos == 0, "the sounding note is released at the swap (pos %lld note %d on=%d)", after[0].pos, after[0].note, (int) after[0].on);
            CHECK(after[1].on && after[1].note == 72 && after[1].pos == 0, "the new pattern starts at step 1 straight away (pos %lld note %d)", after[1].pos, after[1].note);
        }
        std::vector<Ev> ons; for (auto& e : after) if (e.on) ons.push_back(e);
        CHECK(ons.size() >= 2 && ons[1].note == 76 && near(ons[1].pos, 12000), "then the rest and step 3 (note 76 at 12000, got %lld)", ons.size() >= 2 ? ons[1].pos : -1LL);
        int bal = 1; bool alt = true; for (auto& e : after) { bal += e.on ? 1 : -1; if (bal < 0 || bal > 1) alt = false; }
        CHECK(alt, "notes after the swap alternate on / off");

        // an empty slot: swapping to nothing releases the note and plays nothing
        StepSequencer z; fill(z, {60, 62, 64, 65});
        run(z, st, bpm, sr, 512, 20000, false);
        z.deserialize(""); z.restart();
        auto none = run(z, st, bpm, sr, 512, 24000, false);
        CHECK(none.size() == 1 && !none[0].on && none[0].note == 65, "an empty pattern: only the note-off of the sounding note (%zu events)", none.size());
        CHECK(z.length() == 0, "empty pattern has length 0");

        // swap with a different length while Logic sync is running: still position-locked, no stuck note
        StepSequencer l; fill(l, {60, 62, 64, 65});
        auto a = run(l, st, bpm, sr, 512, 20000, true);
        l.deserialize("67,69,71,72,74,76,77"); l.restart();
        auto b = run(l, st, bpm, sr, 512, 40000, true, 20000.0 / 24000.0 * 1.0);
        int bal2 = 0; bool alt2 = true;
        for (auto& e : a) bal2 += e.on ? 1 : -1;
        for (auto& e : b) { bal2 += e.on ? 1 : -1; if (bal2 < 0 || bal2 > 1) alt2 = false; }
        CHECK(alt2, "Logic sync: swapping to a 7-step pattern keeps notes alternating on / off");
    }

    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures ? 1 : 0;
}
