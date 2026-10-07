// Drives the real WT8 AudioProcessor the way a host would (no GUI, no audio device).
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

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

    bool ok = seqOk && v4Ok && finite && peak > 0.02f && peak <= 1.5f && held > 0.005f && tail < held * 0.05f && std::fabs(cutoff - 0.2f) < 0.01f;
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
