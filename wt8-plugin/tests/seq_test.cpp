// Headless timing tests for StepSequencer (no JUCE, no audio).  Run: ./seq_test
#include "StepSequencer.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
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

    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures ? 1 : 0;
}
