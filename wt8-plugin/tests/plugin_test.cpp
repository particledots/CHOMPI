// Drives the real WT8 AudioProcessor the way a host would (no GUI, no audio device).
#include "PluginProcessor.h"
#include "WavetableImport.h"
#include <cstring>
#include <limits>
#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>

static float rms(const juce::AudioBuffer<float>& b, int ch)
{
    double s = 0; auto* d = b.getReadPointer(ch);
    for (int i = 0; i < b.getNumSamples(); ++i) s += d[i] * d[i];
    return (float) std::sqrt(s / b.getNumSamples());
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    WT8AudioProcessor proc;
    const double sr = 44100.0; const int bs = 512;
    proc.setPlayConfigDetails(0, 2, sr, bs);
    proc.prepareToPlay(sr, bs);

    juce::AudioBuffer<float> buf(2, bs);
    float peak = 0, held = 0, tail = 0;
    bool finite = true;

    // note on at sample 100 of block 0, held for 40 blocks, note off, then 120 more blocks
    for (int blk = 0; blk < 160; ++blk)
    {
        juce::MidiBuffer midi;
        if (blk == 0)  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 100);
        if (blk == 40) midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        buf.clear();
        proc.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < bs; ++i)
            {
                float v = buf.getSample(c, i);
                if (!std::isfinite(v)) finite = false;
                peak = std::fmax(peak, std::fabs(v));
            }
        if (blk == 30)  held = rms(buf, 0);
        if (blk == 159) tail = rms(buf, 0);
    }
    printf("44.1kHz/512: peak=%.3f held_rms=%.4f tail_rms=%.6f finite=%d\n", peak, held, tail, (int) finite);

    // state round-trip
    proc.apvts.getParameter("cutoff")->setValueNotifyingHost(0.2f);
    juce::MemoryBlock state; proc.getStateInformation(state);
    proc.apvts.getParameter("cutoff")->setValueNotifyingHost(0.9f);
    proc.setStateInformation(state.getData(), (int) state.getSize());
    float cutoff = *proc.apvts.getRawParameterValue("cutoff");
    printf("state restore: cutoff=%.3f (expect ~0.2*range)\n", cutoff);

    // --- sequencer through the real processor (no play head: free-run at the default 120 bpm) ---
    bool seqOk = true;
    {
        WT8AudioProcessor sp;
        sp.setPlayConfigDetails(0, 2, sr, bs);
        sp.prepareToPlay(sr, bs);
        sp.sequencer().recordNote(60, 100); sp.sequencer().addRest(); sp.sequencer().recordNote(67, 100);

        auto runBlocks = [&](WT8AudioProcessor& pr, int blocks, float& peakOut, float& minWindowRms, float& maxWindowRms) {
            peakOut = 0; minWindowRms = 1e9f; maxWindowRms = 0;
            juce::MidiBuffer none; juce::AudioBuffer<float> b(2, bs);
            for (int blk = 0; blk < blocks; ++blk)
            {
                b.clear(); pr.processBlock(b, none);
                for (int i = 0; i < bs; ++i) peakOut = std::fmax(peakOut, std::fabs(b.getSample(0, i)));
                const float r = rms(b, 0); minWindowRms = std::fmin(minWindowRms, r); maxWindowRms = std::fmax(maxWindowRms, r);
            }
        };
        float pk, mn, mx;
        runBlocks(sp, 40, pk, mn, mx);
        printf("sequencer stopped: peak=%.4f (expect silence)\n", pk);
        seqOk = seqOk && pk < 1e-4f;

        sp.apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
        runBlocks(sp, 80, pk, mn, mx);
        printf("sequencer playing: peak=%.3f quietest_block=%.5f loudest_block=%.4f (expect sound with gaps)\n", pk, mn, mx);
        seqOk = seqOk && pk > 0.02f && mx > 0.005f && mn < mx * 0.2f;

        sp.setSeqRecording(true);
        runBlocks(sp, 60, pk, mn, mx); // record armed: sequence must not play (after the current note's tail)
        runBlocks(sp, 20, pk, mn, mx);
        printf("record armed: peak=%.5f (expect silence)\n", pk);
        seqOk = seqOk && pk < 1e-3f;
        sp.setSeqRecording(false);

        juce::MemoryBlock st; sp.getStateInformation(st);
        WT8AudioProcessor rp; rp.setPlayConfigDetails(0, 2, sr, bs); rp.prepareToPlay(sr, bs);
        rp.setStateInformation(st.getData(), (int) st.getSize());
        printf("sequence restore: '%s' play=%.0f (expect 60:100,r,67:100 and play=0)\n", rp.sequencer().serialize().c_str(), (double) *rp.apvts.getRawParameterValue("seq_play"));
        seqOk = seqOk && rp.sequencer().serialize() == "60:100,r,67:100" && *rp.apvts.getRawParameterValue("seq_play") < 0.5f;
    }

    // --- v0.4 sequencer options through the real processor ---
    bool v4Ok = true;
    {
        auto setPlain = [](WT8AudioProcessor& pr, const char* id, float plain) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(pr.apvts.getParameter(id));
            p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        auto getPlain = [](WT8AudioProcessor& pr, const char* id) { return (float) *pr.apvts.getRawParameterValue(id); };
        auto check = [&](bool cond, const char* what) { printf("  %s: %s\n", cond ? "ok  " : "FAIL", what); v4Ok = v4Ok && cond; };

        // defaults reproduce v0.3 behaviour, and the new parameters sort after the old ones in Logic (version hint 2)
        WT8AudioProcessor d;
        check(getPlain(d, "seq_loop") == 0.f && getPlain(d, "seq_dir") == 0.f && getPlain(d, "seq_pendrep") == 0.f
              && getPlain(d, "seq_prob") == 1.f && getPlain(d, "seq_seed") == 0.f, "new parameters default to v0.3 behaviour");
        check(d.apvts.getParameter("seq_prob")->getVersionHint() == 2 && d.apvts.getParameter("seq_dir")->getVersionHint() == 2
              && d.apvts.getParameter("seq_loop")->getVersionHint() == 2 && d.apvts.getParameter("seq_seed")->getVersionHint() == 2
              && d.apvts.getParameter("seq_pendrep")->getVersionHint() == 2, "new parameters use version hint 2");
        check(d.apvts.getParameter("seq_gate")->getVersionHint() == 1 && d.apvts.getParameter("table")->getVersionHint() == 1,
              "existing parameters keep version hint 1");

        // probability 0 stops the sequence from sounding; direction/loop/seed settings run cleanly
        WT8AudioProcessor sp; sp.setPlayConfigDetails(0, 2, sr, bs); sp.prepareToPlay(sr, bs);
        for (int n : {60, 64, 67, 72}) sp.sequencer().recordNote(n, 100);
        auto runBlocks = [&](WT8AudioProcessor& pr, int blocks, bool& fin) {
            float pk = 0; juce::MidiBuffer none; juce::AudioBuffer<float> b(2, bs);
            for (int blk = 0; blk < blocks; ++blk)
            {
                b.clear(); pr.processBlock(b, none);
                for (int i = 0; i < bs; ++i) { const float v = b.getSample(0, i); if (!std::isfinite(v)) fin = false; pk = std::fmax(pk, std::fabs(v)); }
            }
            return pk;
        };
        bool fin = true;
        setPlain(sp, "seq_prob", 0.f);
        sp.apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
        const float silent = runBlocks(sp, 80, fin);
        check(silent < 1e-4f, "probability 0%: sequence is silent");
        setPlain(sp, "seq_prob", 1.f);
        const float loud = runBlocks(sp, 80, fin);
        check(loud > 0.02f, "probability back to 100%: sequence sounds");
        float worst = 0;
        for (int dir = 0; dir < 4; ++dir)
        {
            setPlain(sp, "seq_dir", (float) dir); setPlain(sp, "seq_pendrep", (float) (dir & 1)); setPlain(sp, "seq_loop", (float) (dir * 3));
            setPlain(sp, "seq_seed", (float) (dir * 7)); setPlain(sp, "seq_prob", 0.7f);
            worst = std::fmax(worst, runBlocks(sp, 60, fin));
        }
        check(fin && worst > 0.02f && worst <= 1.5f, "all directions / loop / seed settings play finite audio");

        // new settings and per-step probability survive a save and reopen
        WT8AudioProcessor src; src.sequencer().deserialize("60:100:75,r,67:100:30");
        setPlain(src, "seq_dir", 2.f); setPlain(src, "seq_pendrep", 1.f); setPlain(src, "seq_loop", 5.f); setPlain(src, "seq_prob", 0.3f); setPlain(src, "seq_seed", 7.f);
        juce::MemoryBlock saved; src.getStateInformation(saved);
        WT8AudioProcessor dst; dst.setStateInformation(saved.getData(), (int) saved.getSize());
        check(getPlain(dst, "seq_dir") == 2.f && getPlain(dst, "seq_pendrep") == 1.f && getPlain(dst, "seq_loop") == 5.f
              && std::fabs(getPlain(dst, "seq_prob") - 0.3f) < 1e-4f && getPlain(dst, "seq_seed") == 7.f, "v0.4 settings restore");
        check(dst.sequencer().serialize() == "60:100:75,r,67:100:30", "per-step probabilities restore");

        // a state saved by v0.3 (no v0.4 parameters, plain "note:vel" pattern) loads, and does not inherit this instance's settings
        auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int) saved.getSize());
        for (auto* id : {"seq_loop", "seq_dir", "seq_pendrep", "seq_prob", "seq_seed"})
            while (auto* ch = xml->getChildByAttribute("id", id)) xml->removeChildElement(ch, true);
        xml->setAttribute("sequence", "60:100,r,67:80");
        juce::MemoryBlock oldState; juce::AudioProcessor::copyXmlToBinary(*xml, oldState);
        WT8AudioProcessor target;
        setPlain(target, "seq_dir", 3.f); setPlain(target, "seq_prob", 0.2f); setPlain(target, "seq_seed", 5.f); setPlain(target, "seq_loop", 4.f); setPlain(target, "seq_pendrep", 1.f);
        target.setStateInformation(oldState.getData(), (int) oldState.getSize());
        check(getPlain(target, "seq_loop") == 0.f && getPlain(target, "seq_dir") == 0.f && getPlain(target, "seq_pendrep") == 0.f
              && getPlain(target, "seq_prob") == 1.f && getPlain(target, "seq_seed") == 0.f, "v0.3 state: missing v0.4 parameters go to their defaults");
        check(target.sequencer().serialize() == "60:100,r,67:80", "v0.3 pattern loads unchanged");
        check(*target.apvts.getRawParameterValue("seq_play") < 0.5f, "loading never starts playback");
        printf("v0.4 options: %s\n", v4Ok ? "ok" : "FAILED");
    }

    // --- v0.5: scale quantizing and pattern transpose from MIDI in, through the real processor ---
    bool v5Ok = true;
    {
        auto setPlain = [](WT8AudioProcessor& pr, const char* id, float plain) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(pr.apvts.getParameter(id));
            p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        auto getPlain = [](WT8AudioProcessor& pr, const char* id) { return (float) *pr.apvts.getRawParameterValue(id); };
        auto check = [&](bool cond, const char* what) { printf("  %s: %s\n", cond ? "ok  " : "FAIL", what); v5Ok = v5Ok && cond; };
        auto makeProc = [&]() {
            auto pr = std::make_unique<WT8AudioProcessor>();
            pr->setPlayConfigDetails(0, 2, sr, bs); pr->prepareToPlay(sr, bs);
            return pr;
        };
        // render `blocks` blocks; `midiAt` lists (block, message) pairs; returns the peak and optionally keeps the left channel
        auto renderBlocks = [&](WT8AudioProcessor& pr, int blocks, std::vector<float>* out,
                                const std::vector<std::pair<int, juce::MidiMessage>>& midiAt) {
            float pk = 0; juce::AudioBuffer<float> b(2, bs);
            for (int blk = 0; blk < blocks; ++blk)
            {
                juce::MidiBuffer m; for (auto& e : midiAt) if (e.first == blk) m.addEvent(e.second, 0);
                b.clear(); pr.processBlock(b, m);
                for (int i = 0; i < bs; ++i) { pk = std::fmax(pk, std::fabs(b.getSample(0, i))); if (out) out->push_back(b.getSample(0, i)); }
            }
            return pk;
        };
        // which MIDI note in [lo, hi] is the strongest frequency in x[a, b)  (MIDI 60 = 261.63 Hz in this plugin)
        auto heard = [&](const std::vector<float>& x, size_t a, size_t b, int lo, int hi) {
            int best = lo; double bestMag = 0;
            for (int n = lo; n <= hi; ++n)
            {
                const double f = 261.63 * std::pow(2.0, (n - 60) / 12.0), w = 2.0 * 3.14159265358979323846 * f / sr;
                double re = 0, im = 0;
                for (size_t i = a; i < b && i < x.size(); ++i) { re += x[i] * std::cos(w * (double) i); im -= x[i] * std::sin(w * (double) i); }
                const double m = std::sqrt(re * re + im * im);
                if (m > bestMag) { bestMag = m; best = n; }
            }
            return best;
        };
        const std::vector<std::pair<int, juce::MidiMessage>> noMidi;
        const size_t winA = (size_t) (1.0 * sr), winB = (size_t) (1.8 * sr);
        auto keyOn = [](int note) { return juce::MidiMessage::noteOn(1, note, (juce::uint8) 100); };

        // defaults reproduce v0.4, new parameters sort after the old ones in Logic (version hint 3)
        auto d = makeProc();
        check(getPlain(*d, "seq_scale") == 0.f && getPlain(*d, "seq_root") == 0.f && getPlain(*d, "seq_xpose") == 0.f && d->getSeqTranspose() == 0,
              "new parameters default to off / C / no transpose");
        check(d->apvts.getParameter("seq_scale")->getVersionHint() == 3 && d->apvts.getParameter("seq_root")->getVersionHint() == 3
              && d->apvts.getParameter("seq_xpose")->getVersionHint() == 3, "new parameters use version hint 3");
        check(d->apvts.getParameter("seq_prob")->getVersionHint() == 2 && d->apvts.getParameter("seq_seed")->getVersionHint() == 2
              && d->apvts.getParameter("seq_gate")->getVersionHint() == 1, "older parameters keep their version hints");
        if (auto* sc = dynamic_cast<juce::AudioParameterChoice*>(d->apvts.getParameter("seq_scale")))
            check(sc->choices.size() == 29 && sc->choices[0] == "Off" && sc->choices[1] == "Major (Ionian)" && sc->choices[28] == "Chromatic", "scale menu: Off + the 28 Scripter scales");
        else check(false, "seq_scale is a choice parameter");

        // a stored note that is not in the scale sounds at the nearest scale tone (61 in C major -> 60); with the scale off it is 61
        for (int pass = 0; pass < 2; ++pass)
        {
            auto p = makeProc(); p->sequencer().recordNote(61, 100);
            setPlain(*p, "seq_div", 0.f); setPlain(*p, "seq_gate", 1.f);
            if (pass == 1) { setPlain(*p, "seq_scale", 1.f); setPlain(*p, "seq_root", 0.f); } // item 1 = Major (Ionian), root C
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            std::vector<float> x; renderBlocks(*p, 130, &x, noMidi);
            const int h = heard(x, (size_t) (0.4 * sr), (size_t) (1.4 * sr), 57, 63);
            char what[96]; snprintf(what, sizeof what, "stored 61, scale %s: sounds as MIDI %d (want %d)", pass ? "C major" : "off", h, pass ? 60 : 61);
            check(h == (pass ? 60 : 61), what);
        }

        // MIDI XPOSE: a key played while the pattern runs sets the transpose (D3 = +2) and is itself silent; the pattern follows
        {
            auto p = makeProc(); p->sequencer().recordNote(60, 100);
            setPlain(*p, "seq_div", 0.f); setPlain(*p, "seq_gate", 1.f); setPlain(*p, "seq_xpose", 1.f);
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            std::vector<float> x; renderBlocks(*p, 160, &x, {{10, keyOn(62)}});
            check(p->getSeqTranspose() == 2, "key D3 sets the transpose to +2");
            const int h = heard(x, winA, winB, 57, 65);
            char what[96]; snprintf(what, sizeof what, "pattern note 60 now sounds as MIDI %d (want 62)", h);
            check(h == 62, what);
            p->setSeqTranspose(0);
            check(p->getSeqTranspose() == 0, "RESET (setSeqTranspose 0)");
        }
        // the transpose only counts while MIDI XPOSE is on
        {
            auto p = makeProc(); p->sequencer().recordNote(60, 100); p->setSeqTranspose(5);
            setPlain(*p, "seq_div", 0.f); setPlain(*p, "seq_gate", 1.f);
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            std::vector<float> x; renderBlocks(*p, 130, &x, noMidi);
            const int h = heard(x, (size_t) (0.4 * sr), (size_t) (1.4 * sr), 57, 66);
            check(h == 60, "XPOSE off: a stored transpose is ignored, the pattern plays as recorded");
        }
        // key sound: silent while the pattern runs (here muted, so the whole output is silent), normal when it is not running, normal when XPOSE is off
        {
            auto p = makeProc(); p->sequencer().recordNote(60, 100);
            setPlain(*p, "seq_mute", 1.f); setPlain(*p, "seq_xpose", 1.f);
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            renderBlocks(*p, 4, nullptr, noMidi); // let it start running
            const float silent = renderBlocks(*p, 40, nullptr, {{0, keyOn(64)}});
            check(silent < 1e-4f && p->getSeqTranspose() == 4, "pattern running: the key only sets the transpose (+4), no sound");
            setPlain(*p, "seq_xpose", 0.f);
            const float loud = renderBlocks(*p, 40, nullptr, {{0, keyOn(64)}});
            check(loud > 0.02f && p->getSeqTranspose() == 4, "XPOSE off: the key plays normally and leaves the transpose alone");
        }
        {
            auto p = makeProc(); setPlain(*p, "seq_xpose", 1.f);
            const float loud = renderBlocks(*p, 40, nullptr, {{0, keyOn(67)}});
            check(loud > 0.02f && p->getSeqTranspose() == 7, "pattern not running: the key sounds AND sets the transpose (+7)");
            const float lower = renderBlocks(*p, 1, nullptr, {{0, keyOn(48)}});
            juce::ignoreUnused(lower);
            check(p->getSeqTranspose() == -12, "a key below C3 gives a negative transpose (C2 = -12)");
        }
        // REC takes priority: keys record steps and leave the transpose alone
        {
            auto p = makeProc(); setPlain(*p, "seq_xpose", 1.f); p->setSeqRecording(true);
            renderBlocks(*p, 4, nullptr, {{0, keyOn(65)}, {2, keyOn(69)}});
            check(p->sequencer().serialize() == "65:100,69:100" && p->getSeqTranspose() == 0, "REC armed: keys record steps, transpose untouched");
        }

        // save / reopen: transpose, scale, root, XPOSE survive; a v0.4 state (none of them) loads with the defaults
        {
            auto src = makeProc(); setPlain(*src, "seq_scale", 6.f); setPlain(*src, "seq_root", 9.f); setPlain(*src, "seq_xpose", 1.f); src->setSeqTranspose(-5);
            src->sequencer().deserialize("60:100,r,67:100");
            juce::MemoryBlock saved; src->getStateInformation(saved);
            auto dst = makeProc(); dst->setStateInformation(saved.getData(), (int) saved.getSize());
            check(getPlain(*dst, "seq_scale") == 6.f && getPlain(*dst, "seq_root") == 9.f && getPlain(*dst, "seq_xpose") == 1.f && dst->getSeqTranspose() == -5,
                  "v0.5 settings and the transpose restore");
            auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int) saved.getSize());
            for (auto* id : {"seq_scale", "seq_root", "seq_xpose"})
                while (auto* ch = xml->getChildByAttribute("id", id)) xml->removeChildElement(ch, true);
            xml->removeAttribute("transpose");
            juce::MemoryBlock oldState; juce::AudioProcessor::copyXmlToBinary(*xml, oldState);
            auto target = makeProc(); setPlain(*target, "seq_scale", 3.f); setPlain(*target, "seq_root", 4.f); setPlain(*target, "seq_xpose", 1.f); target->setSeqTranspose(7);
            target->setStateInformation(oldState.getData(), (int) oldState.getSize());
            check(getPlain(*target, "seq_scale") == 0.f && getPlain(*target, "seq_root") == 0.f && getPlain(*target, "seq_xpose") == 0.f && target->getSeqTranspose() == 0,
                  "v0.4 state: missing v0.5 settings go to their defaults (no scale, no transpose)");
            check(target->sequencer().serialize() == "60:100,r,67:100", "v0.4 pattern loads unchanged");
        }
        printf("v0.5 scale + transpose: %s\n", v5Ok ? "ok" : "FAILED");
    }


    // --- v0.6: per-step expression and swing through the real processor ---
    bool v6Ok = true;
    {
        auto setPlain = [](WT8AudioProcessor& pr, const char* id, float plain) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(pr.apvts.getParameter(id));
            p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        auto getPlain = [](WT8AudioProcessor& pr, const char* id) { return (float) *pr.apvts.getRawParameterValue(id); };
        auto check = [&](bool cond, const char* what) { printf("  %s: %s\n", cond ? "ok  " : "FAIL", what); v6Ok = v6Ok && cond; };
        auto makeProc = [&]() {
            auto pr = std::make_unique<WT8AudioProcessor>();
            pr->setPlayConfigDetails(0, 2, sr, bs); pr->prepareToPlay(sr, bs);
            return pr;
        };
        auto render = [&](WT8AudioProcessor& pr, int blocks) { // returns the left channel
            std::vector<float> out; juce::AudioBuffer<float> b(2, bs); juce::MidiBuffer none;
            for (int blk = 0; blk < blocks; ++blk) { b.clear(); pr.processBlock(b, none); for (int i = 0; i < bs; ++i) out.push_back(b.getSample(0, i)); }
            return out;
        };
        // how many separate note starts are in a signal: windows of 64 samples whose level rises from below to above a threshold
        auto onsets = [&](const std::vector<float>& x, size_t from, size_t to) {
            const size_t w = 64; float peak = 0;
            for (float v : x) peak = std::fmax(peak, std::fabs(v)); // one reference level for the whole render
            const float hi = peak * 0.30f, lo = peak * 0.08f; int n = 0; bool armed = true;
            for (size_t a = from; a + w <= to && a + w <= x.size(); a += w)
            {
                float lvl = 0; for (size_t i = a; i < a + w; ++i) lvl = std::fmax(lvl, std::fabs(x[i]));
                if (armed && lvl > hi) { ++n; armed = false; } else if (lvl < lo) armed = true;
            }
            return n;
        };

        auto d = makeProc();
        check(getPlain(*d, "seq_swing") == 50.f && std::fabs(getPlain(*d, "seq_accent") - 0.3f) < 1e-5f && getPlain(*d, "seq_octmode") == 0.f, "new parameters default to straight / accent 0.3 / up 1 octave");
        check(d->apvts.getParameter("seq_swing")->getVersionHint() == 4 && d->apvts.getParameter("seq_accent")->getVersionHint() == 4 && d->apvts.getParameter("seq_octmode")->getVersionHint() == 4,
              "new parameters use version hint 4");
        check(d->apvts.getParameter("seq_scale")->getVersionHint() == 3 && d->apvts.getParameter("seq_prob")->getVersionHint() == 2 && d->apvts.getParameter("seq_gate")->getVersionHint() == 1,
              "older parameters keep their version hints");
        if (auto* oc = dynamic_cast<juce::AudioParameterChoice*>(d->apvts.getParameter("seq_octmode")))
            check(oc->choices.size() == 6 && oc->choices[0] == "Up 1 octave" && oc->choices[5] == "Up or down 2 octaves", "octave-jump menu: 6 modes");
        else check(false, "seq_octmode is a choice parameter");

        // a ratcheted step really comes out as separate notes: 1 step of 1/4 (22050 samples at 44.1 kHz / 120 bpm), 4 repeats, short gate
        for (int rep : {1, 4})
        {
            auto p = makeProc(); p->sequencer().recordNote(60, 100); p->sequencer().setRatchet(0, rep);
            setPlain(*p, "seq_div", 0.f); setPlain(*p, "seq_gate", 0.3f); setPlain(*p, "release", 0.f);
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            const auto x = render(*p, 44); // 44 x 512 = 22528 samples = one step plus a little
            const int n = onsets(x, 0, 22050);
            char what[96]; snprintf(what, sizeof what, "ratchet %d through the processor: %d separate note starts in the step (want %d)", rep, n, rep);
            check(n == rep, what);
        }
        // swing moves the odd step: pattern C C, 1/16 (5512.5 samples at 44.1 kHz). The first note after start-up fades in
        // slowly (engine smoothing), so the START of the second note is what is measured, straight vs swung 75 %
        // (step 2 half a step = 2756 samples later).
        {
            auto secondNoteStart = [&](float swing) {
                auto p = makeProc(); p->sequencer().recordNote(60, 100); p->sequencer().recordNote(60, 100);
                setPlain(*p, "seq_div", 2.f); setPlain(*p, "seq_gate", 0.25f); setPlain(*p, "seq_swing", swing);
                p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
                const auto x = render(*p, 30); // 15360 samples
                float peak = 0; for (float v : x) peak = std::fmax(peak, std::fabs(v));
                int found = 0; size_t second = 0; bool armed = true; const size_t w = 32;
                for (size_t a = 0; a + w <= x.size(); a += w)
                {
                    float lvl = 0; for (size_t i = a; i < a + w; ++i) lvl = std::fmax(lvl, std::fabs(x[i]));
                    if (armed && lvl > peak * 0.30f) { if (found == 1) second = a; ++found; armed = false; } else if (lvl < peak * 0.05f) armed = true;
                }
                return found >= 2 ? (double) second : -1.0;
            };
            const double straight = secondNoteStart(50.f), swung = secondNoteStart(75.f);
            char what[160]; snprintf(what, sizeof what, "second note starts at sample %.0f straight (want ~5512) and %.0f swung 75 %% (want ~8268): %.0f later (want ~2756)", straight, swung, swung - straight);
            check(straight > 0 && std::fabs(straight - 5512.5) < 200.0 && std::fabs((swung - straight) - 2756.25) < 100.0, what);
        }
        // accent makes the accented note louder; the amount is a parameter
        {
            auto loudness = [&](float accentAmount) {
                auto p = makeProc(); p->sequencer().recordNote(60, 50); p->sequencer().toggleAccent(0);
                setPlain(*p, "seq_div", 0.f); setPlain(*p, "seq_gate", 1.f); setPlain(*p, "seq_accent", accentAmount);
                p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
                const auto x = render(*p, 20); float pk = 0; for (float v : x) pk = std::fmax(pk, std::fabs(v)); return pk;
            };
            const float quiet = loudness(0.f), loud = loudness(0.8f);
            char what[96]; snprintf(what, sizeof what, "accent 0.8 is louder than accent 0 (peak %.3f vs %.3f)", loud, quiet);
            check(loud > quiet * 1.4f && quiet > 0.005f, what);
        }
        // octave jump: stored 60, up one octave. With chance 100 % the energy sits at MIDI 72 (the 2nd harmonic of MIDI 60 is there too,
        // so the same measurement at chance 0 % is the control: there the energy must sit at MIDI 60).
        {
            auto ratio72to60 = [&](int chance) {
                auto p = makeProc(); p->sequencer().recordNote(60, 100); p->sequencer().setOctChance(0, chance);
                setPlain(*p, "seq_div", 0.f); setPlain(*p, "seq_gate", 1.f); setPlain(*p, "seq_octmode", 0.f); setPlain(*p, "seq_seed", 3.f);
                p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
                const auto x = render(*p, 130);
                auto mag = [&](int note) { const double f = 261.63 * std::pow(2.0, (note - 60) / 12.0), w = 2.0 * 3.14159265358979323846 * f / sr; double re = 0, im = 0;
                    for (size_t i = (size_t) (0.4 * sr); i < (size_t) (1.4 * sr) && i < x.size(); ++i) { re += x[i] * std::cos(w * (double) i); im -= x[i] * std::sin(w * (double) i); } return std::sqrt(re * re + im * im); };
                return mag(72) / std::fmax(1e-9, mag(60));
            };
            const double jumped = ratio72to60(100), plain = ratio72to60(0);
            char what[128]; snprintf(what, sizeof what, "octave jump: energy at MIDI 72 vs 60 is %.1fx with chance 100 %% and %.2fx with chance 0 %% (want > 3 and < 1)", jumped, plain);
            check(jumped > 3.0 && plain < 1.0, what);
        }
        // trigger condition: step 1 of a 2-step pattern with "pass 2 of every 2": on the first pass it is silent, on the second it plays
        {
            auto p = makeProc(); p->sequencer().recordNote(60, 100); p->sequencer().setCondition(0, 2, 2);
            setPlain(*p, "seq_div", 0.f); setPlain(*p, "seq_gate", 0.3f); setPlain(*p, "seq_loop", 1.f); // loop of 1 step: every step is a new pass
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            const auto x = render(*p, 130); // 130 x 512 = 66560 samples: 3 steps of 22050 samples = 3 passes
            const int pass1 = onsets(x, 0, 22050), pass2 = onsets(x, 22050, 44100), pass3 = onsets(x, 44100, 66150);
            char what[96]; snprintf(what, sizeof what, "condition 2 of 2: note starts in passes 1, 2, 3 = %d, %d, %d (want 0, 1, 0)", pass1, pass2, pass3);
            check(pass1 == 0 && pass2 == 1 && pass3 == 0, what);
        }
        // everything runs finite and bounded, at a large block size and a fast tempo as well
        {
            auto p = makeProc(); for (int n : {60, 64, 67, 72}) p->sequencer().recordNote(n, 100);
            for (int i = 0; i < 4; ++i) { p->sequencer().setRatchet(i, 8); p->sequencer().setOctChance(i, 50); p->sequencer().toggleAccent(i); p->sequencer().setCondition(i, 1, 2); p->sequencer().setStepGate(i, 60); }
            setPlain(*p, "seq_div", 7.f); setPlain(*p, "seq_swing", 70.f); setPlain(*p, "seq_octmode", 5.f); setPlain(*p, "seq_scale", 6.f);
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            juce::AudioBuffer<float> big(2, 4096); juce::MidiBuffer none; float pk = 0; bool fin = true;
            for (int blk = 0; blk < 40; ++blk) { big.clear(); p->processBlock(big, none); for (int i = 0; i < 4096; ++i) { const float v = big.getSample(0, i); if (!std::isfinite(v)) fin = false; pk = std::fmax(pk, std::fabs(v)); } }
            check(fin && pk > 0.01f && pk <= 1.5f, "all v0.6 features at once, 4096-sample blocks: finite audio, sane level");
        }
        // save / reopen: settings and the per-step values survive; a v0.5 state loads with the defaults and an untouched pattern
        {
            auto src = makeProc(); setPlain(*src, "seq_swing", 62.5f); setPlain(*src, "seq_accent", 0.7f); setPlain(*src, "seq_octmode", 4.f);
            src->sequencer().deserialize("60:100:75:x3:g50:a:o30:c2/3,r:x2,67:100");
            juce::MemoryBlock saved; src->getStateInformation(saved);
            auto dst = makeProc(); dst->setStateInformation(saved.getData(), (int) saved.getSize());
            check(std::fabs(getPlain(*dst, "seq_swing") - 62.5f) < 1e-3f && std::fabs(getPlain(*dst, "seq_accent") - 0.7f) < 1e-3f && getPlain(*dst, "seq_octmode") == 4.f, "v0.6 settings restore");
            check(dst->sequencer().serialize() == "60:100:75:x3:g50:a:o30:c2/3,r:x2,67:100", "per-step v0.6 values restore");
            auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int) saved.getSize());
            for (auto* id : {"seq_swing", "seq_accent", "seq_octmode"})
                while (auto* ch = xml->getChildByAttribute("id", id)) xml->removeChildElement(ch, true);
            xml->setAttribute("sequence", "60:100:75,r,67:80");
            juce::MemoryBlock oldState; juce::AudioProcessor::copyXmlToBinary(*xml, oldState);
            auto target = makeProc(); setPlain(*target, "seq_swing", 70.f); setPlain(*target, "seq_accent", 0.9f); setPlain(*target, "seq_octmode", 3.f);
            target->setStateInformation(oldState.getData(), (int) oldState.getSize());
            check(getPlain(*target, "seq_swing") == 50.f && std::fabs(getPlain(*target, "seq_accent") - 0.3f) < 1e-5f && getPlain(*target, "seq_octmode") == 0.f,
                  "v0.5 state: missing v0.6 settings go to their defaults (straight, accent 0.3, up 1 octave)");
            check(target->sequencer().serialize() == "60:100:75,r,67:80", "v0.5 pattern loads unchanged");
        }
        printf("v0.6 per-step expression + swing: %s\n", v6Ok ? "ok" : "FAILED");
    }

    // ---- v0.8: presets (sound settings as files) and pattern slots (16 patterns inside the project) ----
    bool v8Ok = true;
    {
        auto setPlain = [](WT8AudioProcessor& pr, const char* id, float plain) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(pr.apvts.getParameter(id));
            p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        auto getPlain = [](WT8AudioProcessor& pr, const char* id) { return (float) *pr.apvts.getRawParameterValue(id); };
        auto check = [&](bool cond, const char* what) { printf("  %s: %s\n", cond ? "ok  " : "FAIL", what); v8Ok = v8Ok && cond; };
        auto makeProc = [&]() {
            auto pr = std::make_unique<WT8AudioProcessor>();
            pr->setPlayConfigDetails(0, 2, sr, bs); pr->prepareToPlay(sr, bs);
            return pr;
        };
        auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };

        // ---- presets ----
        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ipmohc_preset_test_" + juce::String(juce::Random::getSystemRandom().nextInt64()));
        {
            auto p = makeProc(); p->setPresetFolder(folder);
            check(p->listPresets().isEmpty(), "preset folder that does not exist yet: empty list");
            setPlain(*p, "cutoff", 0.2f); setPlain(*p, "table", 3.f); setPlain(*p, "comp", 0.7f); setPlain(*p, "pitch", -5.f); setPlain(*p, "octave", 1.f);
            setPlain(*p, "gain", 0.3f); setPlain(*p, "pan", 0.8f); setPlain(*p, "output", 5.f);
            juce::String err;
            check(p->savePreset("My Sound/1", err) && err.isEmpty(), "save a preset (the name has a character that is not allowed in file names)");
            check(p->listPresets().size() == 1 && folder.getNumberOfChildFiles(juce::File::findFiles, "*.ipmohcpreset") == 1, "one preset file in the folder");
            const auto listed = p->listPresets()[0];
            // change everything, then load it back
            setPlain(*p, "cutoff", 0.9f); setPlain(*p, "table", 6.f); setPlain(*p, "comp", 0.1f); setPlain(*p, "pitch", 7.f); setPlain(*p, "octave", -1.f);
            setPlain(*p, "gain", 0.6f); setPlain(*p, "pan", 0.1f); setPlain(*p, "output", -3.f); setPlain(*p, "seq_swing", 70.f); setPlain(*p, "seq_loop", 5.f);
            p->sequencer().recordNote(64, 100);
            check(p->loadPreset(listed), "load it back");
            check(near(getPlain(*p, "cutoff"), 0.2f) && getPlain(*p, "table") == 3.f && near(getPlain(*p, "comp"), 0.7f) && near(getPlain(*p, "pitch"), -5.f) && getPlain(*p, "octave") == 1.f,
                  "the sound settings come back (cutoff, table, comp, pitch, octave)");
            check(near(getPlain(*p, "gain"), 0.6f) && near(getPlain(*p, "pan"), 0.1f) && near(getPlain(*p, "output"), -3.f), "GAIN, PAN and BOOST are not part of a preset");
            check(near(getPlain(*p, "seq_swing"), 70.f) && getPlain(*p, "seq_loop") == 5.f && p->sequencer().length() == 1, "sequencer settings and the pattern are not touched");

            // overwrite
            setPlain(*p, "cutoff", 0.77f);
            check(p->savePreset("My Sound/1", err) && p->listPresets().size() == 1, "saving the same name again replaces the file");
            setPlain(*p, "cutoff", 0.1f); p->loadPreset(listed);
            check(near(getPlain(*p, "cutoff"), 0.77f), "...and the new value is what loads");

            // a file written by another version: a missing parameter -> default, an unknown one is ignored, an out-of-range value is clamped
            auto xml = juce::parseXML(folder.getChildFile(listed + ".ipmohcpreset"));
            bool removed = false;
            for (auto* e : xml->getChildWithTagNameIterator("P"))
                if (e->getStringAttribute("id") == "cutoff") { xml->removeChildElement(e, true); removed = true; break; }
            auto* extra = xml->createNewChildElement("P"); extra->setAttribute("id", "no_such_parameter"); extra->setAttribute("v", 3.0);
            auto* big = xml->createNewChildElement("P"); big->setAttribute("id", "resonance"); big->setAttribute("v", 7.0);
            check(removed && folder.getChildFile("odd.ipmohcpreset").replaceWithText(xml->toString()), "set-up: a hand-edited preset file");
            setPlain(*p, "cutoff", 0.1f); setPlain(*p, "resonance", 0.1f);
            check(p->loadPreset("odd"), "a preset with an unknown parameter loads");
            check(near(getPlain(*p, "cutoff"), 0.5f), "a parameter the file does not mention goes to its default (cutoff 0.5)");
            check(near(getPlain(*p, "resonance"), 1.f), "an out-of-range value is clamped to the parameter's range");

            // bad input
            folder.getChildFile("junk.ipmohcpreset").replaceWithText("this is not xml");
            folder.getChildFile("other.ipmohcpreset").replaceWithText("<something_else/>");
            setPlain(*p, "cutoff", 0.31f);
            check(!p->loadPreset("junk") && !p->loadPreset("other") && !p->loadPreset("does not exist") && !p->loadPreset("") && near(getPlain(*p, "cutoff"), 0.31f),
                  "a file that is not a preset, a missing file and an empty name all fail without changing anything");
            juce::String err2;
            check(!p->savePreset("   ", err2) && err2.isNotEmpty(), "saving without a name fails with a message");

            // INIT
            setPlain(*p, "table", 5.f); setPlain(*p, "gain", 0.55f);
            p->loadInitPreset();
            check(getPlain(*p, "table") == 1.f && near(getPlain(*p, "cutoff"), 0.5f) && near(getPlain(*p, "resonance"), 0.63f) && near(getPlain(*p, "fx"), 0.5f) && near(getPlain(*p, "comp"), 0.f),
                  "INIT puts the sound settings back to their defaults");
            check(near(getPlain(*p, "gain"), 0.55f), "...but leaves GAIN alone");
            check(WT8AudioProcessor::presetParameterIds().size() == 15, "a preset has 15 parameters");
            // every id in the list is a real parameter (a typo would silently store nothing)
            bool allReal = true; for (auto& id : WT8AudioProcessor::presetParameterIds()) if (p->apvts.getParameter(id) == nullptr) allReal = false;
            check(allReal, "every parameter a preset names exists");
            // the file really is small plain XML
            check(folder.getChildFile(listed + ".ipmohcpreset").loadFileAsString().contains("<ipmohcPreset"), "the file is readable XML");
        }
        folder.deleteRecursively();

        // ---- pattern slots ----
        {
            auto p = makeProc();
            check(p->getPatternSlot() == 0 && !p->patternSlotHasSteps(0), "starts on slot 1, empty");
            p->sequencer().recordNote(60, 100); p->sequencer().addRest(); p->sequencer().recordNote(67, 90);
            const auto patA = p->sequencer().serialize();
            check(p->patternSlotHasSteps(0), "the current slot counts what is in the sequencer");
            p->selectPatternSlot(4);
            check(p->getPatternSlot() == 4 && p->sequencer().length() == 0 && p->patternSlotHasSteps(0) && !p->patternSlotHasSteps(4), "slot 5 is empty; slot 1 kept its pattern");
            p->sequencer().recordNote(72, 100); p->sequencer().recordNote(74, 100);
            const auto patB = p->sequencer().serialize();
            p->selectPatternSlot(0);
            check(p->sequencer().serialize() == patA, "back on slot 1 the first pattern is exactly as it was");
            p->selectPatternSlot(4);
            check(p->sequencer().serialize() == patB, "slot 5 has the second one");
            p->selectPatternSlot(99);
            check(p->getPatternSlot() == 15, "a slot number past the end is clamped to 16");
            p->selectPatternSlot(-3);
            check(p->getPatternSlot() == 0 && p->sequencer().serialize() == patA, "...and one below 1 to slot 1");
            p->selectPatternSlot(4);

            // saved with the project
            juce::MemoryBlock saved; p->getStateInformation(saved);
            auto q = makeProc(); q->setStateInformation(saved.getData(), (int) saved.getSize());
            check(q->getPatternSlot() == 4 && q->sequencer().serialize() == patB, "reopened project: same slot, same pattern");
            check(q->patternSlotHasSteps(0) && !q->patternSlotHasSteps(1), "reopened project: slot 1 holds a pattern, slot 2 does not");
            q->selectPatternSlot(0);
            check(q->sequencer().serialize() == patA, "reopened project: slot 1 has the first pattern");

            // a project from before v0.8 has just `sequence`: it lands in slot 1 and the others are empty
            auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int) saved.getSize());
            for (int i = 0; i < 16; ++i) xml->removeAttribute("pat" + juce::String(i));
            xml->removeAttribute("patCur");
            xml->setAttribute("sequence", "60:100:75,r,67:80");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary(*xml, old);
            auto r = makeProc(); r->selectPatternSlot(7); r->sequencer().recordNote(50, 100);
            r->setStateInformation(old.getData(), (int) old.getSize());
            check(r->getPatternSlot() == 0 && r->sequencer().serialize() == "60:100:75,r,67:80", "pre-v0.8 project: the pattern is in slot 1");
            bool othersEmpty = true; for (int i = 1; i < 16; ++i) if (r->patternSlotHasSteps(i)) othersEmpty = false;
            check(othersEmpty, "pre-v0.8 project: slots 2-16 are empty (the previous session's slots do not leak in)");

            // a slot that is emptied does not come back from a stale saved copy
            r->selectPatternSlot(2); r->sequencer().recordNote(55, 100); r->selectPatternSlot(0);
            r->selectPatternSlot(2); r->sequencer().clear(); r->selectPatternSlot(0);
            juce::MemoryBlock saved2; r->getStateInformation(saved2);
            auto xml2 = juce::AudioProcessor::getXmlFromBinary(saved2.getData(), (int) saved2.getSize());
            check(!xml2->hasAttribute("pat2") && xml2->getStringAttribute("sequence") == "60:100:75,r,67:80", "an emptied slot leaves no saved copy; `sequence` is still the current pattern (so older versions can read the project)");
            // the other settings do not belong to a slot
            setPlain(*r, "seq_swing", 66.f); r->setSeqTranspose(5); r->selectPatternSlot(3);
            check(near(getPlain(*r, "seq_swing"), 66.f) && r->getSeqTranspose() == 5, "switching slot leaves swing / direction / transpose alone");
        }
        // a slot switch while the sequencer plays: no stuck note, the new pattern is heard, an empty slot is silent
        {
            auto p = makeProc();
            for (int n : {60, 64, 67, 72}) p->sequencer().recordNote(n, 100);
            p->selectPatternSlot(1); for (int n : {48, 50}) p->sequencer().recordNote(n, 100);
            p->selectPatternSlot(0);
            setPlain(*p, "seq_div", 1.f); // 1/8
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            juce::AudioBuffer<float> b(2, bs); juce::MidiBuffer none; bool fin = true; float pk = 0;
            auto runB = [&](int blocks) { pk = 0; for (int blk = 0; blk < blocks; ++blk) { b.clear(); p->processBlock(b, none); for (int i = 0; i < bs; ++i) { const float v = b.getSample(0, i); if (!std::isfinite(v)) fin = false; pk = std::fmax(pk, std::fabs(v)); } } };
            runB(30); const float sounding = pk;
            p->selectPatternSlot(1); runB(30); const float sounding2 = pk;
            p->selectPatternSlot(9); // empty
            runB(8);  // let the release of the last note and the 4 s tail settle quickly: only check later blocks
            runB(400); const float afterEmpty = pk;
            check(fin && sounding > 0.01f && sounding2 > 0.01f, "audio from slot 1 and from slot 2 after switching while playing");
            check(afterEmpty < 0.01f, "an empty slot is silent (after the release tail)");
            p->selectPatternSlot(0); runB(30);
            check(pk > 0.01f, "going back to a slot with a pattern plays again");
        }
        printf("v0.8 presets + pattern slots: %s\n", v8Ok ? "ok" : "FAILED");
    }

    // ---- v0.9: copy a pattern slot, starter presets, and the "modified" marker (which preset is current, has the sound changed) ----
    bool v9Ok = true;
    {
        auto setPlain = [](WT8AudioProcessor& pr, const char* id, float plain) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(pr.apvts.getParameter(id));
            p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        auto getPlain = [](WT8AudioProcessor& pr, const char* id) { return (float) *pr.apvts.getRawParameterValue(id); };
        auto check = [&](bool cond, const char* what) { printf("  %s: %s\n", cond ? "ok  " : "FAIL", what); v9Ok = v9Ok && cond; };
        auto makeProc = [&]() {
            auto pr = std::make_unique<WT8AudioProcessor>();
            pr->setPlayConfigDetails(0, 2, sr, bs); pr->prepareToPlay(sr, bs);
            return pr;
        };
        auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };

        // ---- copy a pattern slot ----
        {
            auto p = makeProc();
            auto& sq = p->sequencer();
            for (int n : {60, 64, 67, 72}) sq.recordNote(n, 100);
            sq.setProb(1, 70); sq.setRatchet(2, 3); sq.setStepGate(0, 50); sq.toggleAccent(3); sq.setOctChance(1, 30); sq.setCondition(2, 2, 3);
            const auto patA = sq.serialize();
            check(patA.find("x3") != std::string::npos && patA.find("c2/3") != std::string::npos, "set-up: a pattern that uses the per-step values");

            check(p->copyPatternSlot(0, 6), "copy slot 1 to slot 7");
            check(p->patternSlotHasSteps(6) && sq.serialize() == patA && p->getPatternSlot() == 0, "slot 7 now holds a pattern; slot 1 is still current and untouched");
            p->selectPatternSlot(6);
            check(sq.serialize() == patA, "slot 7 holds exactly the same pattern, with every per-step value");
            sq.recordNote(50, 100);
            p->selectPatternSlot(0);
            check(sq.serialize() == patA, "editing the copy does not change the original");

            // copy over a slot that holds something: it is replaced
            p->selectPatternSlot(3); sq.recordNote(40, 100); sq.recordNote(41, 100); p->selectPatternSlot(0);
            check(p->copyPatternSlot(0, 3), "copy onto a slot that holds a pattern");
            p->selectPatternSlot(3);
            check(sq.serialize() == patA, "...it replaced what was there");
            p->selectPatternSlot(0);

            // copy between two slots that are not the current one
            p->selectPatternSlot(6); const auto patA2 = sq.serialize(); p->selectPatternSlot(0);
            check(p->copyPatternSlot(6, 9) && p->getPatternSlot() == 0 && sq.serialize() == patA, "copy slot 7 to slot 10 while slot 1 is current: slot 1 untouched");
            p->selectPatternSlot(9); check(sq.serialize() == patA2, "slot 10 has slot 7's pattern"); p->selectPatternSlot(0);

            // copy into the current slot from another one
            p->selectPatternSlot(5); sq.recordNote(30, 90); const auto patC = sq.serialize(); p->selectPatternSlot(0);
            check(p->copyPatternSlot(5, 0) && p->getPatternSlot() == 0 && sq.serialize() == patC, "copy into the current slot: the sequencer has the copy");
            p->selectPatternSlot(5); check(sq.serialize() == patC, "...and the source slot still has its pattern"); p->selectPatternSlot(0);

            // refusals change nothing
            const auto before = sq.serialize();
            check(!p->copyPatternSlot(0, 0) && !p->copyPatternSlot(-1, 2) && !p->copyPatternSlot(2, 16) && !p->copyPatternSlot(99, 0) && !p->copyPatternSlot(0, -4),
                  "copying a slot onto itself, or from / to a slot that does not exist, is refused");
            check(sq.serialize() == before && p->getPatternSlot() == 0, "...and changes nothing");

            // copying an empty slot gives an empty slot
            check(!p->patternSlotHasSteps(12) && p->copyPatternSlot(12, 3) && !p->patternSlotHasSteps(3), "copying an empty slot leaves an empty slot");

            // saved with the project
            p->selectPatternSlot(0); sq.clear(); for (int n : {60, 64}) sq.recordNote(n, 100);
            const auto patD = sq.serialize();
            p->copyPatternSlot(0, 14);
            juce::MemoryBlock saved; p->getStateInformation(saved);
            auto q = makeProc(); q->setStateInformation(saved.getData(), (int) saved.getSize());
            q->selectPatternSlot(14);
            check(q->sequencer().serialize() == patD, "a copied slot comes back after saving and reopening the project");
        }
        // copying while the sequencer plays: no stuck note, no break in the sound
        {
            auto p = makeProc();
            for (int n : {60, 64, 67, 72}) p->sequencer().recordNote(n, 100);
            setPlain(*p, "seq_div", 1.f);
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            juce::AudioBuffer<float> b(2, bs); juce::MidiBuffer none; bool fin = true; float pk = 0;
            auto runB = [&](int blocks) { pk = 0; for (int blk = 0; blk < blocks; ++blk) { b.clear(); p->processBlock(b, none); for (int i = 0; i < bs; ++i) { const float v = b.getSample(0, i); if (!std::isfinite(v)) fin = false; pk = std::fmax(pk, std::fabs(v)); } } };
            runB(30); const float before = pk;
            p->copyPatternSlot(0, 8); runB(30); const float afterCopyAway = pk;
            p->selectPatternSlot(8); p->sequencer().recordNote(50, 100); p->selectPatternSlot(0);
            p->copyPatternSlot(8, 0); runB(30); const float afterCopyIn = pk;
            check(fin && before > 0.01f && afterCopyAway > 0.01f && afterCopyIn > 0.01f, "copy to another slot, and copy into the playing slot, while playing: still sounding, all finite");
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(0.f); runB(8); runB(400);
            check(pk < 0.01f, "...and silent after stopping (no stuck note)");
        }

        // ---- starter presets ----
        check(WT8AudioProcessor::numStarterPresets() == 14, "14 starter presets");
        {
            bool names = true;
            for (int i = 0; i < 14; ++i) names = names && WT8AudioProcessor::starterPresetName(i) == "Starter " + juce::String(i + 1).paddedLeft('0', 2);
            check(names && WT8AudioProcessor::starterPresetName(0) == "Starter 01" && WT8AudioProcessor::starterPresetName(13) == "Starter 14", "named Starter 01 .. Starter 14");
        }
        {
            // Cross-check the compiled-in table against the firmware's own presets.json, converted here a second way
            // (CI runs the tests from the repository root; if the file is not found from where this runs the check is skipped, loudly).
            juce::File json;
            for (const char* rel : {"firmware/card-profiles/wave-1.0/presets.json", "../firmware/card-profiles/wave-1.0/presets.json", "../../firmware/card-profiles/wave-1.0/presets.json"})
            {
                const auto f = juce::File::getCurrentWorkingDirectory().getChildFile(rel);
                if (f.existsAsFile()) { json = f; break; }
            }
            if (json == juce::File())
                printf("  SKIP: firmware/card-profiles/wave-1.0/presets.json not found from the current directory - starter table NOT cross-checked\n");
            else
            {
                const auto root = juce::JSON::parse(json);
                auto* rows = root.getArray();
                check(rows != nullptr && rows->size() == 15 && (int) rows->getLast() == 3, "the firmware preset file has 14 slots and format version 3 (as the table was made from)");
                bool all = rows != nullptr && rows->size() == 15;
                for (int i = 0; all && i < 14; ++i)
                {
                    auto* r = (*rows)[i].getArray();
                    if (r == nullptr || r->size() != 15 || !(bool) (*r)[14]) { all = false; break; }
                    auto v = [&](int k) { return (double) (*r)[k]; };
                    auto p = makeProc();
                    setPlain(*p, "gain", 0.37f); setPlain(*p, "pan", 0.2f); setPlain(*p, "output", 7.f); setPlain(*p, "octave", 1.f); setPlain(*p, "comp", 0.8f);
                    all = all && p->loadStarterPreset(i);
                    const float semis = (float) std::lround((v(0) / 1000.0 - 0.5) * 24.0);
                    const bool same = getPlain(*p, "table") == (float) (v(2) + 1) && getPlain(*p, "cycle") == (float) v(1) && near(getPlain(*p, "pitch"), semis)
                        && near(getPlain(*p, "attack"), (float) (v(3) / 1000)) && near(getPlain(*p, "pitchlfodepth"), (float) (v(4) / 1000)) && near(getPlain(*p, "release"), (float) (v(5) / 1000))
                        && near(getPlain(*p, "filterlfodepth"), (float) (v(6) / 1000)) && near(getPlain(*p, "cutoff"), (float) (v(7) / 1000)) && near(getPlain(*p, "pitchlforate"), (float) (v(8) / 1000))
                        && near(getPlain(*p, "fx"), (float) (v(9) / 1000)) && near(getPlain(*p, "resonance"), (float) (v(10) / 1000)) && near(getPlain(*p, "filterlforate"), (float) (v(11) / 1000))
                        && near(getPlain(*p, "fxtime"), (float) (v(12) / 1000)) && (int) v(13) == 3;
                    if (!same) printf("    starter %d differs from the firmware file\n", i + 1);
                    all = all && same
                        && getPlain(*p, "octave") == 0.f && near(getPlain(*p, "comp"), 0.f)                                   // not in a firmware preset: back to the defaults
                        && near(getPlain(*p, "gain"), 0.37f) && near(getPlain(*p, "pan"), 0.2f) && near(getPlain(*p, "output"), 7.f); // the level is never touched
                }
                check(all, "all 14 starter presets give the same sound parameters as firmware/card-profiles/wave-1.0/presets.json (second conversion written separately); octave / comp default, GAIN / PAN / BOOST untouched");
            }
        }
        check(!makeProc()->loadStarterPreset(-1) && !makeProc()->loadStarterPreset(14), "a starter number out of range fails");
        {
            // Every starter makes a sound that is finite and audible, through the real processor. The levels are printed (they are
            // the hardware's presets at the plugin's default BOOST; a peak above 1.0 would clip in a host).
            bool allFine = true, anyHot = false;
            for (int i = 0; i < 14; ++i)
            {
                auto p = makeProc();
                p->loadStarterPreset(i);
                juce::AudioBuffer<float> b(2, bs); float pk = 0, heldSq = 0; int heldN = 0; bool fin = true;
                for (int blk = 0; blk < 200; ++blk)
                {
                    juce::MidiBuffer m;
                    if (blk == 0) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
                    if (blk == 60) m.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
                    b.clear(); p->processBlock(b, m);
                    for (int k = 0; k < bs; ++k)
                    {
                        const float v = b.getSample(0, k);
                        if (!std::isfinite(v)) fin = false;
                        pk = std::fmax(pk, std::fabs(v));
                        if (blk >= 20 && blk < 60) { heldSq += v * v; ++heldN; }
                    }
                }
                const float heldRms = (float) std::sqrt(heldSq / heldN);
                printf("    %s: peak %.3f, held rms %.4f%s\n", WT8AudioProcessor::starterPresetName(i).toRawUTF8(), pk, heldRms, pk > 1.0f ? "  (over 1.0)" : "");
                if (pk > 1.0f) anyHot = true;
                allFine = allFine && fin && pk > 0.001f;
            }
            juce::ignoreUnused(anyHot);
            check(allFine, "every starter preset renders finite audio that is not silent (note C3, velocity 100, default output level)");
        }

        // ---- which preset is current, and has the sound changed since ----
        {
            auto p = makeProc();
            check(p->getCurrentPresetKind() == WT8AudioProcessor::PresetKind::None && p->getCurrentPresetName().isEmpty() && !p->isPresetModified(), "a fresh plugin: no preset is current, nothing is 'modified'");
            setPlain(*p, "cutoff", 0.9f);
            check(!p->isPresetModified(), "without a current preset there is nothing to be modified");

            p->loadStarterPreset(2);
            check(p->getCurrentPresetKind() == WT8AudioProcessor::PresetKind::Starter && p->getCurrentPresetName() == "Starter 03" && !p->isPresetModified(), "a starter preset loaded: it is current, not modified");
            const float cut = getPlain(*p, "cutoff");
            setPlain(*p, "cutoff", cut > 0.5f ? cut - 0.2f : cut + 0.2f);
            check(p->isPresetModified(), "moving a knob (cutoff) marks it modified");
            setPlain(*p, "cutoff", cut);
            check(!p->isPresetModified(), "putting the knob back removes the mark");
            auto* cp = p->apvts.getParameter("cutoff");
            cp->setValueNotifyingHost(cp->getValue() + 1.0e-5f);
            check(!p->isPresetModified(), "a change smaller than any knob step (1e-5 of the range) does not count");
            setPlain(*p, "cutoff", cut);
            for (const char* id : {"table", "cycle", "octave", "pitch", "attack", "release", "resonance", "fx", "fxtime", "pitchlfodepth", "pitchlforate", "filterlfodepth", "filterlforate", "comp"})
            {
                auto* par = p->apvts.getParameter(id);
                const float v0 = par->getValue();
                par->setValueNotifyingHost(v0 > 0.5f ? v0 - 0.3f : v0 + 0.3f);
                const bool flagged = p->isPresetModified();
                par->setValueNotifyingHost(v0);
                if (!flagged || p->isPresetModified()) { printf("    parameter %s does not behave as part of the preset\n", id); v9Ok = false; }
            }
            check(!p->isPresetModified(), "each of the other 14 sound parameters marks it modified, and restoring it clears the mark");

            // what is not part of a preset never counts
            setPlain(*p, "gain", 0.1f); setPlain(*p, "pan", 0.9f); setPlain(*p, "output", -9.f); setPlain(*p, "seq_swing", 70.f); setPlain(*p, "seq_loop", 4.f);
            p->sequencer().recordNote(60, 100); p->selectPatternSlot(3);
            check(!p->isPresetModified(), "GAIN, PAN, BOOST, sequencer settings and pattern slots do not mark it modified");

            p->loadInitPreset();
            check(p->getCurrentPresetKind() == WT8AudioProcessor::PresetKind::Init && p->getCurrentPresetName().isEmpty() && !p->isPresetModified(), "INIT loaded: current, not modified");
            setPlain(*p, "resonance", 0.2f);
            check(p->isPresetModified(), "...and a change marks INIT modified");
            p->loadStarterPreset(5); p->loadStarterPreset(5);
            check(!p->isPresetModified(), "loading the same preset again clears the mark (that is how changes are undone)");

            // file presets
            const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ipmohc_preset_test9_" + juce::String(juce::Random::getSystemRandom().nextInt64()));
            p->setPresetFolder(folder);
            setPlain(*p, "cutoff", 0.15f);
            juce::String err;
            check(p->savePreset("Mine", err), "set-up: save a preset");
            check(p->getCurrentPresetKind() == WT8AudioProcessor::PresetKind::User && p->getCurrentPresetName() == "Mine" && !p->isPresetModified(), "after SAVE the saved preset is current and not modified");
            setPlain(*p, "cutoff", 0.65f);
            check(p->isPresetModified(), "...changing a knob afterwards marks it");
            check(p->savePreset("Mine", err) && !p->isPresetModified(), "saving again (replacing) clears the mark");
            check(p->savePreset("Other", err) && p->getCurrentPresetName() == "Other" && !p->isPresetModified(), "saving under another name makes that one current");
            p->loadStarterPreset(0);
            check(p->loadPreset("Mine") && p->getCurrentPresetKind() == WT8AudioProcessor::PresetKind::User && p->getCurrentPresetName() == "Mine" && !p->isPresetModified(), "loading a file preset makes it current, not modified");
            setPlain(*p, "cutoff", 0.33f);
            check(!p->loadPreset("does not exist") && p->getCurrentPresetName() == "Mine" && p->isPresetModified(), "a failed load changes nothing: the same preset stays current and still counts as modified");

            // a project that is loaded brings its own sound: no preset is current
            juce::MemoryBlock saved; p->getStateInformation(saved);
            p->loadStarterPreset(4);
            p->setStateInformation(saved.getData(), (int) saved.getSize());
            check(p->getCurrentPresetKind() == WT8AudioProcessor::PresetKind::None && !p->isPresetModified(), "after a project is loaded no preset is current");
            folder.deleteRecursively();
        }
        printf("v0.9 copy slot + starter presets + modified marker: %s\n", v9Ok ? "ok" : "FAILED");
    }

    // ---- v0.10 user wavetables (LOAD): import / convert, engine hand-off, saved with the project ----
    bool v10Ok = true;
    {
        auto check = [&](bool cond, const char* what) { printf("  %s: %s\n", cond ? "ok  " : "FAIL", what); v10Ok = v10Ok && cond; };
        auto setPlain = [](WT8AudioProcessor& pr, const char* id, float plain) {
            auto* pp = dynamic_cast<juce::RangedAudioParameter*>(pr.apvts.getParameter(id));
            pp->setValueNotifyingHost(pp->convertTo0to1(plain));
        };
        // minimal WAV writers
        auto wavBytes = [](int code, int bits, int ch, const std::vector<double>& v, const char* extraChunk = nullptr) {
            std::vector<uint8_t> d;
            auto p16 = [&](int x) { d.push_back((uint8_t) (x & 255)); d.push_back((uint8_t) ((x >> 8) & 255)); };
            auto p32 = [&](uint32_t x) { for (int i = 0; i < 4; ++i) d.push_back((uint8_t) ((x >> (8 * i)) & 255)); };
            auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) d.push_back((uint8_t) t[i]); };
            std::vector<uint8_t> pay;
            for (double x : v)
            {
                if (code == 3) { float f = (float) x; uint8_t b4[4]; std::memcpy(b4, &f, 4); pay.insert(pay.end(), b4, b4 + 4); }
                else if (bits == 16) { int q = (int) std::lround(x * 32767); pay.push_back((uint8_t) (q & 255)); pay.push_back((uint8_t) ((q >> 8) & 255)); }
                else { int q = (int) std::lround(x * 8388607); pay.push_back((uint8_t) (q & 255)); pay.push_back((uint8_t) ((q >> 8) & 255)); pay.push_back((uint8_t) ((q >> 16) & 255)); }
            }
            tag("RIFF"); p32(0); tag("WAVE");
            tag("fmt "); p32(16); p16(code); p16(ch); p32(44100); p32((uint32_t) (44100 * ch * bits / 8)); p16(ch * bits / 8); p16(bits);
            if (extraChunk != nullptr) { tag("clm "); const uint32_t n = (uint32_t) std::strlen(extraChunk); p32(n); for (uint32_t i = 0; i < n; ++i) d.push_back((uint8_t) extraChunk[i]); if (n & 1) d.push_back(0); }
            tag("data"); p32((uint32_t) pay.size()); d.insert(d.end(), pay.begin(), pay.end());
            const uint32_t total = (uint32_t) d.size() - 8;
            for (int i = 0; i < 4; ++i) d[4 + (size_t) i] = (uint8_t) ((total >> (8 * i)) & 255);
            return d;
        };
        const double twoPi = 6.283185307179586;

        // ---- the importer on its own ----
        {
            std::vector<float> out; std::string note, err;
            auto stats = [&](const std::vector<float>& t, float& peak, float& maxDc, bool& fin) {
                peak = 0; maxDc = 0; fin = true;
                for (int f = 0; f < wtimport::kFrames; ++f) { double m = 0; for (int i = 0; i < wtimport::kFrameSize; ++i) { const float v = t[(size_t) f * wtimport::kFrameSize + (size_t) i]; if (!std::isfinite(v)) fin = false; peak = std::fmax(peak, std::fabs(v)); m += v; } maxDc = (float) std::fmax(maxDc, std::fabs(m / wtimport::kFrameSize)); }
            };
            float pk, dc; bool fin;

            std::vector<float> one(1024); for (size_t i = 0; i < one.size(); ++i) one[i] = (float) (std::sin(twoPi * (double) i / 1024.0) + 0.3 + 0.2 * std::sin(3 * twoPi * (double) i / 1024.0));
            check(wtimport::convertToTable(one, 0, out, note, err) && out.size() == (size_t) wtimport::kTableFloats, "a single 1024-sample cycle converts");
            stats(out, pk, dc, fin);
            check(fin && std::fabs(pk - 0.9f) < 1e-4f && dc < 1e-5f, "...peak 0.9, no DC in any frame, all finite");
            bool same = true; for (int i = 0; i < wtimport::kFrameSize; ++i) if (out[(size_t) i] != out[(size_t) 32 * wtimport::kFrameSize + (size_t) i]) same = false;
            check(same && note.find("single cycle") != std::string::npos, "...a single cycle is the same in all 33 frames, and the note says so");

            // 64 frames of 2048: frame f is a sine with f+1 periods (very different frames)
            std::vector<float> many(64 * 2048);
            for (int f = 0; f < 64; ++f) for (int i = 0; i < 2048; ++i) many[(size_t) f * 2048 + (size_t) i] = (float) (0.5 * std::sin(twoPi * (f + 1) * i / 2048.0));
            check(wtimport::convertToTable(many, 0, out, note, err), "64 frames of 2048 convert");
            auto corr = [&](const float* a, const float* b) { double ma = 0, mb = 0; for (int i = 0; i < 2048; ++i) { ma += a[i]; mb += b[i]; } ma /= 2048; mb /= 2048; double ab = 0, aa = 0, bb = 0; for (int i = 0; i < 2048; ++i) { ab += (a[i] - ma) * (b[i] - mb); aa += (a[i] - ma) * (a[i] - ma); bb += (b[i] - mb) * (b[i] - mb); } return ab / std::sqrt(aa * bb); };
            check(corr(&out[0], &many[0]) > 0.9999 && corr(&out[32 * 2048], &many[63 * 2048]) > 0.9999, "...the first and last frame of the file are the first and last frame of the table");
            stats(out, pk, dc, fin);
            check(fin && std::fabs(pk - 0.9f) < 1e-4f, "...peak 0.9");

            std::vector<float> exact33(33 * 2048); for (size_t i = 0; i < exact33.size(); ++i) exact33[i] = (float) (0.4 * std::sin(0.01 * (double) i));
            check(wtimport::convertToTable(exact33, 0, out, note, err) && corr(&out[5 * 2048], &exact33[5 * 2048]) > 0.9999 && corr(&out[20 * 2048], &exact33[20 * 2048]) > 0.9999, "exactly 33 frames: every frame is kept as it is (apart from level)");

            // frames of another size announced by a hint
            std::vector<float> f1024(10 * 1024); for (size_t i = 0; i < f1024.size(); ++i) f1024[i] = (float) std::sin(twoPi * (double) (i % 1024) / 1024.0 * (1 + (double) (i / 1024)));
            check(wtimport::convertToTable(f1024, 1024, out, note, err) && note.find("10 frames of 1024") != std::string::npos, "a frame-size hint (from a clm chunk) of 1024 reads 10 frames");

            // a recording is cut into 33 parts
            std::vector<float> rec(100000); for (size_t i = 0; i < rec.size(); ++i) rec[i] = (float) (std::sin(0.05 * (double) i) * std::sin(0.0002 * (double) i));
            check(wtimport::convertToTable(rec, 0, out, note, err) && note.find("33 equal parts") != std::string::npos, "an odd-length recording is cut into 33 parts");
            stats(out, pk, dc, fin);
            check(fin && std::fabs(pk - 0.9f) < 1e-4f && dc < 1e-5f, "...peak 0.9, no DC");

            std::vector<float> bad(5000, 0.f);
            check(!wtimport::convertToTable(bad, 0, out, note, err) && err.find("silent") != std::string::npos, "a silent file is refused");
            std::vector<float> tiny(10, 0.5f);
            check(!wtimport::convertToTable(tiny, 0, out, note, err), "a file with fewer than 64 samples is refused");
            std::vector<float> nan(8192, std::numeric_limits<float>::quiet_NaN()); nan[100] = 0.5f; nan[4000] = -0.5f;
            check(wtimport::convertToTable(nan, 0, out, note, err), "NaN samples in a file do not break the conversion");
            stats(out, pk, dc, fin);
            check(fin, "...and the table is finite");
        }

        // ---- reading WAV files ----
        {
            std::vector<float> m; int sr = 0, clm = 0; std::string err;
            std::vector<double> st; for (int i = 0; i < 100; ++i) { st.push_back(0.5); st.push_back(-0.25); }
            auto w16 = wavBytes(1, 16, 2, st);
            check(wtimport::readWav(w16.data(), w16.size(), m, sr, clm, err) && m.size() == 100 && std::fabs(m[0] - 0.125f) < 1e-3f && sr == 44100, "16-bit stereo is read and mixed to mono");
            auto w24 = wavBytes(1, 24, 1, {0.5, -0.5, 0.25});
            check(wtimport::readWav(w24.data(), w24.size(), m, sr, clm, err) && m.size() == 3 && std::fabs(m[1] + 0.5f) < 1e-5f, "24-bit mono is read");
            auto wf = wavBytes(3, 32, 1, {0.5, -0.5, 0.25, 1.5});
            check(wtimport::readWav(wf.data(), wf.size(), m, sr, clm, err) && m.size() == 4 && m[3] == 1.5f, "32-bit float is read as it is");
            auto wc = wavBytes(3, 32, 1, {0.1, 0.2}, "<!>2048 10000000 wavetable (test)");
            check(wtimport::readWav(wc.data(), wc.size(), m, sr, clm, err) && clm == 2048, "a Serum-style clm chunk gives the frame size");
            auto wn = wavBytes(3, 32, 1, {0.1, 0.2}, "no marker here");
            check(wtimport::readWav(wn.data(), wn.size(), m, sr, clm, err) && clm == 0, "a clm chunk without the marker is ignored");
            check(!wtimport::readWav(w16.data(), 20, m, sr, clm, err), "a truncated header is refused");
            std::vector<uint8_t> junk(500, 0x41);
            check(!wtimport::readWav(junk.data(), junk.size(), m, sr, clm, err) && err.find("not a WAV") != std::string::npos, "a file that is not a WAV is refused");
            auto wcut = w16; wcut.resize(wcut.size() - 7);
            check(wtimport::readWav(wcut.data(), wcut.size(), m, sr, clm, err) && m.size() >= 98, "a file cut short is read as far as it goes");
        }

        // ---- through the real processor ----
        {
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ipmohc_table_test_" + juce::String(juce::Random::getSystemRandom().nextInt64()));
            dir.createDirectory();
            // a table that sounds unlike any built-in one: frame f is a stack of f+1 harmonics falling as 1/h^2
            std::vector<double> tab; tab.reserve(33 * 2048);
            for (int f = 0; f < 33; ++f) for (int i = 0; i < 2048; ++i) { double v = 0; for (int h = 1; h <= 1 + f; ++h) v += std::sin(twoPi * h * i / 2048.0) / (h * h); tab.push_back(0.8 * v); }
            auto tb = wavBytes(3, 32, 1, tab);
            const auto goodFile = dir.getChildFile("My Table.wav");
            goodFile.replaceWithData(tb.data(), tb.size());
            const auto junkFile = dir.getChildFile("junk.wav");
            { std::vector<uint8_t> j(300, 7); junkFile.replaceWithData(j.data(), j.size()); }
            const auto silentFile = dir.getChildFile("silent.wav");
            { auto sb = wavBytes(3, 32, 1, std::vector<double>(4096, 0.0)); silentFile.replaceWithData(sb.data(), sb.size()); }

            auto makeProc = [&]() {
                auto pr = std::make_unique<WT8AudioProcessor>();
                pr->setPlayConfigDetails(0, 2, sr, bs); pr->prepareToPlay(sr, bs);
                setPlain(*pr, "table", 3); setPlain(*pr, "cycle", 20);
                return pr;
            };
            // renders `blocks` blocks (left channel), a C3 is struck in the first one when noteOn
            auto render = [&](WT8AudioProcessor& pr, int blocks, bool noteOn) {
                juce::AudioBuffer<float> b(2, bs);
                std::vector<float> out;
                for (int blk = 0; blk < blocks; ++blk)
                {
                    juce::MidiBuffer mb;
                    if (noteOn && blk == 0) mb.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
                    b.clear(); pr.processBlock(b, mb);
                    for (int i = 0; i < bs; ++i) out.push_back(b.getSample(0, i));
                }
                return out;
            };
            auto maxDiff = [](const std::vector<float>& a, const std::vector<float>& c) { float m = 0; for (size_t i = 0; i < a.size() && i < c.size(); ++i) m = std::fmax(m, std::fabs(a[i] - c[i])); return m; };
            auto peakOf = [](const std::vector<float>& a) { float m = 0; for (float v : a) m = std::fmax(m, std::fabs(v)); return m; };
            auto finiteAll = [](const std::vector<float>& a) { for (float v : a) if (!std::isfinite(v)) return false; return true; };
            juce::String msg;

            auto ref = makeProc();
            const auto builtin = render(*ref, 40, true);
            check(peakOf(builtin) > 0.01f, "set-up: the built-in table 3 sounds");
            juce::MemoryBlock stBuiltin; ref->getStateInformation(stBuiltin);

            auto p = makeProc();
            check(!p->slotHasUserTable(2) && p->userTableName(2).isEmpty(), "no user table at the start");
            check(!p->loadUserTable(2, dir.getChildFile("nothing.wav"), msg) && msg.isNotEmpty(), "a file that does not exist is refused, with a message");
            check(!p->loadUserTable(2, junkFile, msg) && !p->slotHasUserTable(2), "a file that is not a WAV is refused, nothing changes");
            check(!p->loadUserTable(2, silentFile, msg) && !p->slotHasUserTable(2), "a silent WAV is refused, nothing changes");
            check(!p->loadUserTable(9, goodFile, msg) && !p->loadUserTable(-1, goodFile, msg), "slot numbers out of range are refused");
            check(maxDiff(render(*p, 40, true), builtin) < 1e-6f, "...and the sound is still the built-in table's");

            auto q = makeProc();
            check(q->loadUserTable(2, goodFile, msg) && q->slotHasUserTable(2) && q->userTableName(2) == "My Table" && !q->slotHasUserTable(1), "a good file loads into slot 3 only; its name is kept");
            const auto loaded = render(*q, 40, true);
            check(finiteAll(loaded) && peakOf(loaded) > 0.01f && maxDiff(loaded, builtin) > 0.02f, "...table 3 now sounds different (and finite)");

            auto q2 = makeProc();
            q2->loadUserTable(2, goodFile, msg); q2->resetUserTable(2);
            check(!q2->slotHasUserTable(2) && q2->userTableName(2).isEmpty(), "RESET: the slot is built-in again");
            check(maxDiff(render(*q2, 40, true), builtin) < 1e-6f, "...and it sounds exactly as the built-in table did");

            auto h = makeProc();
            const auto first = render(*h, 20, true);
            h->loadUserTable(2, goodFile, msg);
            const auto second = render(*h, 20, false);
            check(finiteAll(first) && finiteAll(second) && peakOf(second) > 0.01f, "loading while a note is held: still sounding, all finite");

            // a new engine (prepareToPlay again, e.g. the host changes the sample rate) keeps the table
            q->prepareToPlay(sr, bs);
            check(q->slotHasUserTable(2) && maxDiff(render(*q, 40, true), loaded) < 1e-6f, "after prepareToPlay the loaded table is still used");

            // saved with the project
            juce::MemoryBlock st; q->getStateInformation(st);
            printf("    (project state with one loaded table: %.0f KB, without: %.0f KB)\n", (double) st.getSize() / 1024.0, (double) stBuiltin.getSize() / 1024.0);
            auto r = makeProc();
            r->setStateInformation(st.getData(), (int) st.getSize());
            check(r->slotHasUserTable(2) && r->userTableName(2) == "My Table" && !r->slotHasUserTable(0), "a project brings its table and its name back");
            check(maxDiff(render(*r, 40, true), loaded) < 1e-6f, "...and it sounds the same as when it was saved");

            auto s = makeProc();
            s->loadUserTable(2, goodFile, msg); s->loadUserTable(5, goodFile, msg);
            render(*s, 4, false);
            s->setStateInformation(stBuiltin.getData(), (int) stBuiltin.getSize());
            check(!s->slotHasUserTable(2) && !s->slotHasUserTable(5), "opening a project without loaded tables takes the loaded ones away");
            auto control = makeProc(); render(*control, 4, false);   // same history (4 idle blocks), but never had a loaded table
            check(maxDiff(render(*s, 40, true), render(*control, 40, true)) < 1e-6f, "...and the built-in tables sound again, exactly as in a plugin that never had one");

            // malformed saved tables are ignored (built-in table stays), and NaN data is made safe
            struct Access : WT8AudioProcessor { using juce::AudioProcessor::copyXmlToBinary; using juce::AudioProcessor::getXmlFromBinary; };
            auto stateWith = [&](const juce::String& value) {
                auto xml = Access::getXmlFromBinary(st.getData(), (int) st.getSize());
                xml->setAttribute("utab2", value);
                juce::MemoryBlock mb; Access::copyXmlToBinary(*xml, mb); return mb;
            };
            {
                auto bad = stateWith("AAAA");
                auto u = makeProc();
                u->setStateInformation(bad.getData(), (int) bad.getSize());
                check(!u->slotHasUserTable(2) && maxDiff(render(*u, 40, true), builtin) < 1e-6f, "a saved table of the wrong size is ignored");
                auto bad2 = stateWith("this is not base64 !!!");
                u->setStateInformation(bad2.getData(), (int) bad2.getSize());
                check(!u->slotHasUserTable(2), "a saved table that is not base64 is ignored");
                std::vector<float> nanTab((size_t) wtimport::kTableFloats, std::numeric_limits<float>::quiet_NaN());
                for (size_t i = 0; i < nanTab.size(); i += 2) nanTab[i] = 0.3f * (float) std::sin(0.02 * (double) i);
                auto nanState = stateWith(juce::Base64::toBase64(nanTab.data(), nanTab.size() * sizeof(float)));
                u->setStateInformation(nanState.getData(), (int) nanState.getSize());
                const auto nanOut = render(*u, 20, true);
                check(u->slotHasUserTable(2) && finiteAll(nanOut), "a saved table with NaN values is accepted but made finite");
            }
            // two loaded tables in different slots, both come back
            {
                auto two = makeProc();
                two->loadUserTable(0, goodFile, msg); two->loadUserTable(6, goodFile, msg);
                juce::MemoryBlock st2; two->getStateInformation(st2);
                auto three = makeProc();
                three->setStateInformation(st2.getData(), (int) st2.getSize());
                check(three->slotHasUserTable(0) && three->slotHasUserTable(6) && !three->slotHasUserTable(3), "tables in several slots all come back");
            }

            // ---- v0.11: frame-size option for LOAD ----
            {
                // 16 frames of 1024 samples, frame f = sine with f+1 periods; once without and once with a Serum-style marker
                std::vector<double> f1k; f1k.reserve(16 * 1024);
                for (int f = 0; f < 16; ++f) for (int i = 0; i < 1024; ++i) f1k.push_back(0.6 * std::sin(twoPi * (f + 1) * i / 1024.0) + 0.2 * std::sin(twoPi * 2 * (f + 1) * i / 1024.0));
                auto noMarker = wavBytes(3, 32, 1, f1k);
                auto withMarker = wavBytes(3, 32, 1, f1k, "<!>1024 10000000 wavetable (test)");
                const auto fNo = dir.getChildFile("k1024_nomarker.wav"), fClm = dir.getChildFile("k1024_marker.wav");
                fNo.replaceWithData(noMarker.data(), noMarker.size());
                fClm.replaceWithData(withMarker.data(), withMarker.size());

                // importer: the hint decides how the file is cut into frames
                std::vector<float> asFloat(f1k.begin(), f1k.end()), t1024, tAuto; std::string note, err;
                auto corrN = [](const float* a, const float* b, int n) { double ma = 0, mb = 0; for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n; double ab = 0, aa = 0, bb = 0; for (int i = 0; i < n; ++i) { ab += (a[i] - ma) * (b[i] - mb); aa += (a[i] - ma) * (a[i] - ma); bb += (b[i] - mb) * (b[i] - mb); } return ab / std::sqrt(aa * bb); };
                check(wtimport::convertToTable(asFloat, 1024, t1024, note, err) && note.find("16 frames of 1024") != std::string::npos, "frame size 1024 reads the 16384-sample file as 16 frames");
                // frame 0 / 32 of the table hold source frame 0 / 15 (resampled 1024 -> 2048, compare every second sample)
                std::vector<float> a0(1024), a32(1024);
                for (int i = 0; i < 1024; ++i) { a0[(size_t) i] = t1024[(size_t) (2 * i)]; a32[(size_t) i] = t1024[(size_t) 32 * 2048 + (size_t) (2 * i)]; }
                check(corrN(a0.data(), &asFloat[0], 1024) > 0.9999 && corrN(a32.data(), &asFloat[15 * 1024], 1024) > 0.9999, "...first and last table frame are the file's first and last frame");
                check(wtimport::convertToTable(asFloat, 0, tAuto, note, err) && note.find("8 frames of 2048") != std::string::npos, "without a hint the same file is read as 8 frames of 2048 (the mistake the option fixes)");
                check(maxDiff(t1024, tAuto) > 0.1f, "...and the two tables differ");
                std::vector<float> five(5000); for (size_t i = 0; i < five.size(); ++i) five[i] = (float) std::sin(0.03 * (double) i);
                check(wtimport::convertToTable(five, 1024, t1024, note, err) && note.find("4 frames of 1024") != std::string::npos && note.find("last 904 samples") != std::string::npos, "a length that is not a whole number of frames: the left-over samples are named in the note");
                std::vector<float> small(3000); for (size_t i = 0; i < small.size(); ++i) small[i] = (float) std::sin(0.03 * (double) i);
                check(!wtimport::convertToTable(small, 4096, t1024, note, err) && err.find("shorter than one frame") != std::string::npos, "a frame size larger than the file is refused");

                // processor
                auto base = makeProc(); base->loadUserTable(2, goodFile, msg);
                const auto baseSound = render(*base, 40, true);
                auto auto2048 = makeProc();
                check(auto2048->loadUserTable(2, goodFile, msg, 2048), "frame size 2048 on a 33 x 2048 file loads");
                check(maxDiff(render(*auto2048, 40, true), baseSound) < 1e-6f, "...and sounds exactly like Auto (v0.10 behaviour)");

                auto pn = makeProc(); pn->loadUserTable(2, fNo, msg, 0);
                const auto soundNoAuto = render(*pn, 40, true);
                auto pm = makeProc(); pm->loadUserTable(2, fNo, msg, 1024);
                const auto soundNo1024 = render(*pm, 40, true);
                auto pc = makeProc(); pc->loadUserTable(2, fClm, msg, 0);
                const auto soundClmAuto = render(*pc, 40, true);
                check(finiteAll(soundNo1024) && peakOf(soundNo1024) > 0.01f, "a 1024-frame file loaded with frame size 1024 sounds and is finite");
                check(maxDiff(soundNo1024, soundNoAuto) > 0.02f, "...and differs from the Auto reading of the same file");
                check(maxDiff(soundNo1024, soundClmAuto) < 1e-6f, "...and equals the file with a 1024 marker loaded on Auto");
                auto po = makeProc(); po->loadUserTable(2, fClm, msg, 2048);
                auto pq = makeProc(); pq->loadUserTable(2, fNo, msg, 2048);
                check(maxDiff(render(*po, 40, true), render(*pq, 40, true)) < 1e-6f, "a chosen frame size beats the marker in the file");

                auto bad = makeProc();
                check(!bad->loadUserTable(2, goodFile, msg, 63) && !bad->loadUserTable(2, goodFile, msg, 16385) && !bad->loadUserTable(2, goodFile, msg, -5) && !bad->slotHasUserTable(2), "frame sizes outside 64..16384 are refused, nothing changes");
                std::vector<double> shortWav(3000); for (size_t i = 0; i < shortWav.size(); ++i) shortWav[i] = 0.5 * std::sin(0.03 * (double) i);
                auto shortBytes = wavBytes(3, 32, 1, shortWav);
                const auto fShort = dir.getChildFile("short3000.wav"); fShort.replaceWithData(shortBytes.data(), shortBytes.size());
                check(!bad->loadUserTable(2, fShort, msg, 4096) && !bad->slotHasUserTable(2) && msg.contains("shorter than one frame"), "a frame size larger than the file is refused with a message, nothing changes");
                check(bad->loadUserTable(2, fNo, msg, 16384) && bad->slotHasUserTable(2), "a frame size equal to the whole file is one frame and loads (every table frame is then the same)");
                // the loaded table is saved and restored whatever frame size was used
                juce::MemoryBlock st3; pm->getStateInformation(st3);
                auto back = makeProc(); back->setStateInformation(st3.getData(), (int) st3.getSize());
                check(maxDiff(render(*back, 40, true), soundNo1024) < 1e-6f, "a table loaded with frame size 1024 comes back from the project unchanged");
            }
            // ---- v0.12: the table picture's data source (getTableData / tableRevision) ----
            {
                auto pr = makeProc();
                std::vector<float> t; bool allOk = true;
                for (int sl = 0; sl < WT8AudioProcessor::kTableSlots; ++sl)
                {
                    t.clear();
                    bool good = pr->getTableData(sl, t) && t.size() == (size_t) wtimport::kTableFloats;
                    for (float v : t) if (!std::isfinite(v) || std::fabs(v) > 1.0f) good = false;
                    allOk = allOk && good;
                }
                check(allOk, "getTableData: all 7 built-in slots give 33 x 2048 finite floats within +/-1");
                std::vector<float> junkOut(5, 1.f);
                check(!pr->getTableData(-1, junkOut) && !pr->getTableData(7, junkOut), "getTableData: slots out of range are refused");
                std::vector<float> b1, b2, b3;
                pr->getTableData(1, b1); pr->getTableData(2, b2);
                check(maxDiff(b1, b2) > 0.05f, "getTableData: different built-in slots give different data");

                const int r0 = pr->tableRevision();
                check(!pr->loadUserTable(2, junkFile, msg) && pr->tableRevision() == r0, "a refused LOAD leaves the revision alone");
                check(pr->loadUserTable(2, goodFile, msg) && pr->tableRevision() != r0, "a LOAD changes the revision");
                std::vector<float> u2, u1;
                pr->getTableData(2, u2); pr->getTableData(1, u1);
                check(u2.size() == (size_t) wtimport::kTableFloats && maxDiff(u2, b2) > 0.05f, "...and getTableData now gives the loaded table for that slot");
                check(maxDiff(u1, b1) == 0.f, "...while the other slots still give their built-in tables");
                const int r1 = pr->tableRevision();
                pr->resetUserTable(2);
                pr->getTableData(2, b3);
                check(pr->tableRevision() != r1 && maxDiff(b3, b2) == 0.f, "RESET changes the revision and gives the built-in data back, exactly");
                const int r2 = pr->tableRevision();
                pr->resetUserTable(2); pr->resetUserTable(4);
                check(pr->tableRevision() == r2, "RESET on slots without a loaded table does not change the revision");

                auto src = makeProc(); src->loadUserTable(4, goodFile, msg);
                juce::MemoryBlock stT; src->getStateInformation(stT);
                auto dst = makeProc(); const int rd = dst->tableRevision();
                dst->setStateInformation(stT.getData(), (int) stT.getSize());
                std::vector<float> fromSrc, fromDst; src->getTableData(4, fromSrc); dst->getTableData(4, fromDst);
                check(dst->tableRevision() != rd && maxDiff(fromSrc, fromDst) == 0.f, "a project that brings a table changes the revision, and the picture's data is the saved table");
                const int rd2 = dst->tableRevision();
                dst->setStateInformation(stBuiltin.getData(), (int) stBuiltin.getSize());
                std::vector<float> back, ref4; dst->getTableData(4, back); ref->getTableData(4, ref4);
                check(dst->tableRevision() != rd2 && maxDiff(back, ref4) == 0.f, "a project without tables changes the revision again and the built-in data is back");
            }
            dir.deleteRecursively();
        }
        printf("v0.10 user wavetables: %s\n", v10Ok ? "ok" : "FAILED");
    }

    // ---- v0.13: loop-end pattern switching (slot bookkeeping around the audio thread's swap) and STEP 1 ----
    bool v13Ok = true;
    {
        auto setPlain = [](WT8AudioProcessor& pr, const char* id, float plain) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(pr.apvts.getParameter(id));
            p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        auto check = [&](bool cond, const char* what) { printf("  %s: %s\n", cond ? "ok  " : "FAIL", what); v13Ok = v13Ok && cond; };
        auto makeProc = [&]() {
            auto pr = std::make_unique<WT8AudioProcessor>();
            pr->setPlayConfigDetails(0, 2, sr, bs); pr->prepareToPlay(sr, bs);
            return pr;
        };
        juce::AudioBuffer<float> b(2, bs); juce::MidiBuffer none; bool fin = true; float pk = 0;
        auto runB = [&](WT8AudioProcessor& pr, int blocks) {
            pk = 0;
            for (int blk = 0; blk < blocks; ++blk)
            {
                b.clear(); pr.processBlock(b, none);
                for (int i = 0; i < bs; ++i) { const float v = b.getSample(0, i); if (!std::isfinite(v)) fin = false; pk = std::fmax(pk, std::fabs(v)); }
            }
        };
        // runs until the audio thread has swapped a queued pattern in (without asking the slot functions, which would collect it); -1 = never
        auto runUntilSwapped = [&](WT8AudioProcessor& pr, int maxBlocks) {
            for (int blk = 0; blk < maxBlocks; ++blk) { runB(pr, 1); if (pr.sequencer().switchPending()) return blk + 1; }
            return -1;
        };
        // slot 1 = A (4 notes), slot 5 = B (3 notes), playing slot 1 at 1/16 (a pass is 4 steps = about 43 blocks)
        auto setUp = [&](bool loopEnd, bool play) {
            auto pr = makeProc();
            for (int n : {60, 64, 67, 72}) pr->sequencer().recordNote(n, 100);
            pr->selectPatternSlot(4); for (int n : {48, 50, 53}) pr->sequencer().recordNote(n, 100);
            pr->selectPatternSlot(0);
            if (loopEnd) pr->setSlotAtLoopEnd(true);
            if (play) { pr->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f); runB(*pr, 14); }
            return pr;
        };
        const std::string patA = [&] { StepSequencer t; for (int n : {60, 64, 67, 72}) t.recordNote(n, 100); return t.serialize(); }();
        const std::string patB = [&] { StepSequencer t; for (int n : {48, 50, 53}) t.recordNote(n, 100); return t.serialize(); }();

        // the switch is OFF by default and then a click on a slot switches at once, as in v0.12
        {
            auto p = setUp(false, true);
            check(!p->getSlotAtLoopEnd(), "the switch is off by default");
            p->requestPatternSlot(4);
            check(p->getPatternSlot() == 4 && p->getQueuedPatternSlot() == -1 && p->sequencer().serialize() == patB, "switch off: a click on a slot switches at once");
            p->requestPatternSlot(99);
            check(p->getPatternSlot() == 15, "...and a slot number past the end is clamped, as before");
        }
        // switch on, playing: the click waits, the playing slot keeps playing, the swap comes at a loop end
        {
            auto p = setUp(true, true);
            p->requestPatternSlot(4);
            check(p->getQueuedPatternSlot() == 4 && p->getPatternSlot() == 0 && p->sequencer().serialize() == patA, "the click queues slot 5; slot 1 is still the playing one");
            p->sequencer().setNote(0, 61); // an edit made while the switch is waiting must stay with slot 1
            StepSequencer t; t.deserialize(patA); t.setNote(0, 61); const std::string patA2 = t.serialize();
            const int blocks = runUntilSwapped(*p, 80);
            check(blocks > 0 && blocks <= 60, "the audio thread swaps the pattern in within one pass (about 43 blocks)");
            check(p->getPatternSlot() == 4 && p->getQueuedPatternSlot() == -1 && p->sequencer().serialize() == patB, "afterwards slot 5 is the current slot and its pattern is live");
            p->selectPatternSlot(0);
            check(p->sequencer().serialize() == patA2, "slot 1 kept its pattern, including the edit made while the switch was waiting");
            p->selectPatternSlot(4);
            runB(*p, 40);
            check(fin && pk > 0.01f, "audio after the swap is finite and the new pattern sounds");
        }
        // a project saved after the audio thread swapped, before anything asked the slot functions, files everything under the right slot
        {
            auto p = setUp(true, true);
            p->requestPatternSlot(4);
            check(runUntilSwapped(*p, 80) > 0 && p->sequencer().switchPending(), "set-up: the swap has happened and nobody has collected it");
            juce::MemoryBlock st; p->getStateInformation(st);
            auto q = makeProc(); q->setStateInformation(st.getData(), (int) st.getSize());
            check(q->getPatternSlot() == 4 && q->sequencer().serialize() == patB, "the saved project has slot 5 as the current slot with its own pattern");
            q->selectPatternSlot(0);
            check(q->sequencer().serialize() == patA, "...and slot 1 still holds the first pattern (nothing was filed under the wrong slot)");
            auto xml = juce::AudioProcessor::getXmlFromBinary(st.getData(), (int) st.getSize());
            check(xml->getStringAttribute("sequence") == juce::String(patB) && xml->getIntAttribute("patCur") == 4 && xml->getStringAttribute("pat0") == juce::String(patA),
                  "the file itself: `sequence` is the live pattern, patCur = 5, pat0 = the replaced pattern (older versions read it as usual)");
        }
        // a click on the playing slot cancels the wait; a second slot replaces the first; switching the switch off drops it
        {
            auto p = setUp(true, true);
            p->requestPatternSlot(4); p->requestPatternSlot(0);
            check(p->getQueuedPatternSlot() == -1, "a click on the playing slot cancels what was waiting");
            runB(*p, 100);
            check(p->getPatternSlot() == 0 && p->sequencer().serialize() == patA && !p->sequencer().switchPending(), "...and nothing switches later");

            auto r = setUp(true, true);
            r->selectPatternSlot(8); r->sequencer().recordNote(70, 100); r->selectPatternSlot(0);
            r->requestPatternSlot(4); r->requestPatternSlot(8);
            check(r->getQueuedPatternSlot() == 8, "a second click replaces the waiting slot");
            runUntilSwapped(*r, 80);
            check(r->getPatternSlot() == 8 && r->sequencer().length() == 1, "...and the second one is the one that plays");

            auto s = setUp(true, true);
            s->requestPatternSlot(4); s->setSlotAtLoopEnd(false);
            check(s->getQueuedPatternSlot() == -1 && !s->getSlotAtLoopEnd(), "switching AT LOOP END off drops the waiting switch");
            runB(*s, 100);
            check(s->getPatternSlot() == 0, "...it does not happen later");
            s->requestPatternSlot(4);
            check(s->getPatternSlot() == 4, "...and clicks switch at once again");
        }
        // nothing playing: no loop end to wait for
        {
            auto p = setUp(true, false);
            p->requestPatternSlot(4);
            check(p->getPatternSlot() == 4 && p->getQueuedPatternSlot() == -1 && p->sequencer().serialize() == patB, "stopped: the click switches at once even with the switch on");
            auto q = setUp(true, true); q->setSeqRecording(true); runB(*q, 4);
            q->requestPatternSlot(4);
            check(q->getPatternSlot() == 4, "record armed: switches at once as well");
        }
        // an empty slot is a valid target: silence from the loop end on
        {
            auto p = setUp(true, true);
            p->requestPatternSlot(9);
            check(runUntilSwapped(*p, 80) > 0 && p->getPatternSlot() == 9 && p->sequencer().length() == 0, "an empty slot waits for the loop end like any other, then the live pattern is empty");
            runB(*p, 8); runB(*p, 400);
            check(fin && pk < 0.01f, "...and it is silent (after the release tail)");
        }
        // COPY into the slot that is waiting: the waiting copy is the new one
        {
            auto p = setUp(true, true);
            p->requestPatternSlot(4);
            check(p->copyPatternSlot(0, 4), "set-up: copy slot 1 onto the slot that is waiting");
            runUntilSwapped(*p, 80);
            check(p->getPatternSlot() == 4 && p->sequencer().serialize() == patA, "when it comes in it is the copy (slot 1's pattern), not the old slot-5 pattern");
        }
        // saved with the project; the waiting switch is not; an older project has none
        {
            auto p = setUp(true, true);
            p->requestPatternSlot(4);
            juce::MemoryBlock st; p->getStateInformation(st);
            auto q = makeProc(); q->setSlotAtLoopEnd(false);
            q->setStateInformation(st.getData(), (int) st.getSize());
            check(q->getSlotAtLoopEnd() && q->getQueuedPatternSlot() == -1 && q->getPatternSlot() == 0 && q->sequencer().serialize() == patA, "the switch comes back with the project; the waiting switch does not (slot 1, its pattern)");
            auto xml = juce::AudioProcessor::getXmlFromBinary(st.getData(), (int) st.getSize());
            xml->removeAttribute("slotLoopEnd");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary(*xml, old);
            auto r = makeProc(); r->setSlotAtLoopEnd(true);
            r->setStateInformation(old.getData(), (int) old.getSize());
            check(!r->getSlotAtLoopEnd(), "a project from before v0.13 has no switch: clicks switch at once, as they always did");
        }
        // opening a project while a switch is waiting: the waiting switch is dropped and cannot land in the opened project
        {
            auto src = makeProc();
            for (int n : {30, 31}) src->sequencer().recordNote(n, 100);
            src->selectPatternSlot(2); src->sequencer().recordNote(35, 100);
            juce::MemoryBlock st; src->getStateInformation(st);
            const std::string wantLive = src->sequencer().serialize();
            auto p = setUp(true, true);
            p->requestPatternSlot(4);
            p->setStateInformation(st.getData(), (int) st.getSize());
            runB(*p, 120);
            check(p->getQueuedPatternSlot() == -1 && p->getPatternSlot() == 2 && p->sequencer().serialize() == wantLive && !p->sequencer().switchPending(), "a project opened while a switch was waiting: the opened project's slot and pattern, no late switch");
        }
        // STEP 1 through the processor: accepted while playing, harmless while stopped, audio stays finite
        {
            auto p = setUp(false, true);
            p->requestStep1(); runB(*p, 60);
            check(fin && pk > 0.01f, "STEP 1 while playing: the sequence goes on sounding, audio finite");
            auto q = setUp(false, false);
            q->requestStep1(); runB(*q, 10);
            q->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f); runB(*q, 40);
            check(fin && pk > 0.01f, "STEP 1 while stopped is forgotten: the next PLAY starts normally");
        }
        // a project loaded while the sequencer holds a note must not leave that note droning (found by Bryn in Logic, Oct 9: reopening a
        // saved project gave a drone that only PLAY cleared). The state is loaded into a processor whose sequence is sounding.
        {
            auto src = setUp(false, true); // saved with PLAY on (so the state says PLAY = on)
            juce::MemoryBlock mb; src->getStateInformation(mb);
            for (int variant = 0; variant < 24; ++variant)
            {
                auto p = setUp(false, true);
                if (variant >= 12) setPlain(*p, "seq_sync", 1.f); // loaded while following Logic
                runB(*p, variant % 12); // somewhere inside a step: a note may be sounding
                p->setStateInformation(mb.getData(), (int) mb.getSize());
                runB(*p, 1400); runB(*p, 100); // about 16 s: far longer than any release; only the last stretch is measured
                check(fin && pk < 0.001f, "loading a project while the sequence sounds leaves no drone (offset varied through a step)");
            }
        }
        printf("v0.13 loop-end slot switching + STEP 1: %s\n", v13Ok ? "ok" : "FAILED");
    }

    // ---- v0.14: RANDOM (randomizePattern through the real processor) ----
    bool v14Ok = true;
    {
        auto setPlain = [](WT8AudioProcessor& pr, const char* id, float plain) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(pr.apvts.getParameter(id));
            p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        auto check = [&](bool cond, const char* what) { printf("  %s: %s\n", cond ? "ok  " : "FAIL", what); v14Ok = v14Ok && cond; };
        auto makeProc = [&]() { auto pr = std::make_unique<WT8AudioProcessor>(); pr->setPlayConfigDetails(0, 2, sr, bs); pr->prepareToPlay(sr, bs); return pr; };
        auto snap = [](WT8AudioProcessor& pr, std::vector<SeqStep>& out) { out.assign(StepSequencer::kMaxSteps, SeqStep()); int len = 0, pi = 0; pr.sequencer().snapshot(out.data(), len, pi); return len; };
        std::vector<SeqStep> st;
        {   // empty slot, LOOP = ALL: 16 steps
            auto p = makeProc();
            const int made = p->randomizePattern(); const int len = snap(*p, st);
            check(made == 16 && len == 16, "an empty slot with LOOP = ALL gets 16 steps");
        }
        {   // LOOP = 7: 7 steps; a longer pattern is replaced
            auto p = makeProc(); for (int i = 0; i < 20; ++i) p->sequencer().recordNote(60 + i, 100);
            setPlain(*p, "seq_loop", 7.f);
            p->randomizePattern(); const int len = snap(*p, st);
            check(len == 7, "LOOP = 7 gives 7 steps, replacing the old 20-step pattern");
        }
        {   // LOOP = ALL with a pattern: its own length
            auto p = makeProc(); for (int i = 0; i < 11; ++i) p->sequencer().recordNote(60 + i, 100);
            p->randomizePattern(); const int len = snap(*p, st);
            check(len == 11, "LOOP = ALL keeps the pattern's length (11)");
        }
        {   // scale: every note is a scale tone (Major, root D = 2) and in C3..C5
            auto p = makeProc(); setPlain(*p, "seq_loop", 32.f); setPlain(*p, "seq_scale", 1.f); setPlain(*p, "seq_root", 2.f);
            bool ok2 = true; int notes = 0;
            for (int rep = 0; rep < 50; ++rep) { p->randomizePattern(); const int len = snap(*p, st); for (int i = 0; i < len; ++i) if (!st[i].rest) { ++notes; if (!StepSequencer::inScale(st[i].note, 2, 0) || st[i].note < 60 || st[i].note > 84) ok2 = false; } }
            check(ok2 && notes > 500, "with SCALE set, every random note is a tone of it, in C3..C5");
        }
        {   // two presses give different patterns; the other slots and the slot number do not change
            auto p = makeProc(); for (int n : {50, 52, 54}) p->sequencer().recordNote(n, 100);
            p->selectPatternSlot(4); for (int n : {40, 41}) p->sequencer().recordNote(n, 100);
            p->selectPatternSlot(0); setPlain(*p, "seq_loop", 16.f);
            p->randomizePattern(); const std::string a = p->sequencer().serialize();
            p->randomizePattern(); const std::string b = p->sequencer().serialize();
            check(a != b, "two presses give two different patterns");
            check(p->getPatternSlot() == 0, "the current slot does not change");
            p->selectPatternSlot(4);
            check(p->sequencer().serialize() == [&] { StepSequencer t; for (int n : {40, 41}) t.recordNote(n, 100); return t.serialize(); }(), "another slot is untouched");
            p->selectPatternSlot(0);
            check(p->sequencer().serialize() == b, "the random pattern is kept in its slot when the slot is left and entered again");
        }
        {   // saved with the project and loaded again
            auto p = makeProc(); setPlain(*p, "seq_loop", 12.f); p->randomizePattern();
            const std::string want = p->sequencer().serialize();
            juce::MemoryBlock mb; p->getStateInformation(mb);
            auto q = makeProc(); q->setStateInformation(mb.getData(), (int) mb.getSize());
            check(q->sequencer().serialize() == want, "a random pattern saves with the project and comes back unchanged");
        }
        {   // it sounds, finite, and pressing RANDOM while it plays leaves nothing droning after PLAY off
            auto p = makeProc(); setPlain(*p, "seq_loop", 8.f); p->randomizePattern();
            juce::AudioBuffer<float> b(2, bs); juce::MidiBuffer none; bool fin = true; float pk = 0, tailPk = 0;
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
            for (int blk = 0; blk < 200; ++blk) { b.clear(); p->processBlock(b, none); for (int i = 0; i < bs; ++i) { const float v = b.getSample(0, i); if (!std::isfinite(v)) fin = false; pk = std::fmax(pk, std::fabs(v)); } if (blk % 17 == 5) p->randomizePattern(); }
            p->apvts.getParameter("seq_play")->setValueNotifyingHost(0.f);
            for (int blk = 0; blk < 1500; ++blk) { b.clear(); p->processBlock(b, none); if (blk >= 1400) for (int i = 0; i < bs; ++i) tailPk = std::fmax(tailPk, std::fabs(b.getSample(0, i))); }
            check(fin && pk > 0.01f, "a random pattern sounds, and re-randomizing while it plays stays finite");
            check(tailPk < 0.001f, "after PLAY off nothing keeps droning");
        }
        printf("v0.14 RANDOM: %s\n", v14Ok ? "ok" : "FAILED");
    }

    bool ok = seqOk && v4Ok && v5Ok && v6Ok && v8Ok && v9Ok && v10Ok && v13Ok && v14Ok && finite && peak > 0.02f && peak <= 1.5f && held > 0.005f && tail < held * 0.05f && std::fabs(cutoff - 0.2f) < 0.01f;
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
