// Drives the real WT8 AudioProcessor the way a host would (no GUI, no audio device).
#include "PluginProcessor.h"
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

    bool ok = seqOk && v4Ok && v5Ok && v6Ok && finite && peak > 0.02f && peak <= 1.5f && held > 0.005f && tail < held * 0.05f && std::fabs(cutoff - 0.2f) < 0.01f;
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
