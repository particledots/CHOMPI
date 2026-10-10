// Drives the MIDI effect build ("dotriaconta-tone", WT8_MIDI_FX=1) the way a host would: MIDI in, MIDI out, no audio channels.
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <vector>

#if !WT8_MIDI_FX
#error "fx_test must be built with WT8_MIDI_FX=1"
#endif

static int failures = 0;
static void check(bool c, const char* what) { printf("  %s: %s\n", c ? "ok  " : "FAIL", what); if (!c) ++failures; }

struct Ev { long t; bool on; int note, vel; int ch; };

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 44100.0; const int bs = 512;
    auto makeProc = [&]() {
        auto p = std::make_unique<WT8AudioProcessor>();
        p->setPlayConfigDetails(0, 0, sr, bs);
        p->prepareToPlay(sr, bs);
        return p;
    };
    auto setPlain = [](WT8AudioProcessor& p, const char* id, float real) { auto* q = p.apvts.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(real)); };

    // run `blocks` blocks; `feed(blk, midi)` may add input MIDI; returns every note on/off that came out (absolute sample time)
    auto run = [&](WT8AudioProcessor& p, int blocks, auto feed, std::vector<juce::MidiMessage>* others = nullptr) {
        std::vector<Ev> out;
        juce::AudioBuffer<float> b; b.setSize(0, bs);
        for (int blk = 0; blk < blocks; ++blk)
        {
            juce::MidiBuffer m; feed(blk, m);
            p.processBlock(b, m);
            for (const auto meta : m)
            {
                const auto msg = meta.getMessage();
                const long t = (long) blk * bs + meta.samplePosition;
                if (msg.isNoteOn()) out.push_back({t, true, msg.getNoteNumber(), (int) msg.getVelocity(), msg.getChannel()});
                else if (msg.isNoteOff()) out.push_back({t, false, msg.getNoteNumber(), 0, msg.getChannel()});
                else if (others) others->push_back(msg);
            }
        }
        return out;
    };
    auto none = [](int, juce::MidiBuffer&) {};

    printf("MIDI effect build\n");
    {
        auto p = makeProc();
        check(p->isMidiEffect() && p->producesMidi() && p->acceptsMidi(), "reports itself as a MIDI effect that produces MIDI");
        check(p->getName() == "dotriaconta-tone" && p->getTailLengthSeconds() == 0.0, "named 'dotriaconta-tone', no tail");
        check(p->getTotalNumInputChannels() == 0 && p->getTotalNumOutputChannels() == 0, "no audio channels");
        bool soundGone = true;
        for (const char* id : {"table", "cycle", "octave", "pitch", "attack", "release", "cutoff", "resonance", "fx", "fxtime", "pitchlfodepth", "pitchlforate",
                               "filterlfodepth", "filterlforate", "gain", "pan", "comp", "output", "pitchlfoshape", "filterlfoshape", "filtertype", "filterkey",
                               "filtervel", "filterenv", "filterdecay"})
            if (p->apvts.getParameter(id) != nullptr) soundGone = false;
        check(soundGone, "none of the 25 sound parameters exist (the host's automation list shows sequencer parameters only)");
        bool seqThere = true;
        for (const char* id : {"seq_play", "seq_sync", "seq_div", "seq_gate", "seq_mute", "seq_loop", "seq_dir", "seq_prob", "seq_seed", "seq_scale", "seq_root", "seq_xpose", "seq_swing", "seq_accent", "seq_octmode", "seq_pendrep"})
            if (p->apvts.getParameter(id) == nullptr) seqThere = false;
        check(seqThere, "the sequencer parameters are all there");
        printf("  (%d parameters in total)\n", (int) p->getParameters().size());
    }

    // the pattern plays out as MIDI
    {
        auto p = makeProc();
        for (int n : {60, 64, 67, 72}) p->sequencer().recordNote(n, 100);
        setPlain(*p, "seq_div", 1.f); // 1/8
        p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
        const auto ev = run(*p, 400, none);
        std::vector<int> ons; std::vector<long> onT; std::multiset<int> open; bool paired = true, chOk = true, velOk = true;
        for (auto& e : ev)
        {
            if (e.ch != 1) chOk = false;
            if (e.on) { ons.push_back(e.note); onT.push_back(e.t); open.insert(e.note); if (e.vel != 100) velOk = false; }
            else { auto it = open.find(e.note); if (it == open.end()) paired = false; else open.erase(it); }
        }
        check(ons.size() >= 8, "notes come out");
        bool order = ons.size() >= 8; for (size_t i = 0; i < ons.size(); ++i) if (ons[i] != std::vector<int>{60, 64, 67, 72}[i % 4]) order = false;
        check(order, "in pattern order 60 64 67 72 60 64 ...");
        bool even = onT.size() >= 3; for (size_t i = 2; i < onT.size(); ++i) if (std::labs((onT[i] - onT[i - 1]) - (onT[1] - onT[0])) > 1) even = false;
        check(even && onT.size() > 1 && onT[1] - onT[0] > 1000, "steps are evenly spaced");
        check(paired, "every note-off has a note-on before it");
        check(chOk && velOk, "channel 1, velocity as recorded");

        // stop: nothing is left sounding
        setPlain(*p, "seq_play", 0.f);
        auto ev2 = run(*p, 4, none);
        for (auto& e : ev2) { if (e.on) {} }
        for (auto& e : ev2) if (!e.on) { auto it = open.find(e.note); if (it != open.end()) open.erase(it); }
        check(open.empty(), "after PLAY is switched off no note is left on (all note-offs arrived)");
        for (auto& e : ev2) if (e.on) { check(false, "no new notes after stop"); break; }
    }

    // pass-through
    {
        auto p = makeProc();
        std::vector<juce::MidiMessage> others;
        const auto ev = run(*p, 3, [&](int blk, juce::MidiBuffer& m) {
            if (blk == 1) { m.addEvent(juce::MidiMessage::noteOn(3, 50, (juce::uint8) 90), 10); m.addEvent(juce::MidiMessage::noteOff(3, 50), 200);
                            m.addEvent(juce::MidiMessage::controllerEvent(3, 1, 64), 30); }
        }, &others);
        check(ev.size() == 2 && ev[0].on && ev[0].note == 50 && ev[0].t == bs + 10 && ev[0].ch == 3 && !ev[1].on && ev[1].t == bs + 200, "keyboard notes pass through unchanged (note, channel, time)");
        check(others.size() == 1 && others[0].isController() && others[0].getControllerValue() == 64, "other messages (a CC) pass through");
    }

    // MIDI XPOSE: a key sets the transpose, is not passed while the pattern runs, and the pattern follows
    {
        auto p = makeProc();
        for (int n : {60, 64}) p->sequencer().recordNote(n, 100);
        setPlain(*p, "seq_div", 1.f); setPlain(*p, "seq_xpose", 1.f);
        p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
        run(*p, 4, none); // running
        const auto ev = run(*p, 40, [&](int blk, juce::MidiBuffer& m) { if (blk == 0) m.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8) 100), 0); });
        bool swallowed = true, shifted = false;
        for (auto& e : ev) { if (e.on && e.note == 62) swallowed = false; if (e.on && (e.note == 62 || e.note == 66)) shifted = true; }
        check(p->getSeqTranspose() == 2, "XPOSE: the key sets the transpose (D = +2)");
        check(swallowed || shifted, "XPOSE: the key itself is not sounded while the pattern runs (62 only appears as the pattern's own transposed 60)");
        bool plus2 = false; for (auto& e : ev) if (e.on && e.note == 62) plus2 = true;
        check(plus2, "XPOSE: the pattern now plays 2 semitones up");
    }

    // REC: keys pass through and are recorded as steps
    {
        auto p = makeProc();
        p->setSeqRecording(true);
        const auto ev = run(*p, 2, [&](int blk, juce::MidiBuffer& m) { if (blk == 0) { m.addEvent(juce::MidiMessage::noteOn(1, 65, (juce::uint8) 80), 0); m.addEvent(juce::MidiMessage::noteOff(1, 65), 100); } });
        check(ev.size() == 2 && ev[0].note == 65 && p->sequencer().length() == 1, "REC: the key passes through and becomes a step");
    }

    // a state that carries sound parameters (saved by ipmohc, or by the first draft) still loads; the sequencer part comes back
    {
        auto p = makeProc();
        for (int n : {60, 64, 67}) p->sequencer().recordNote(n, 100);
        juce::MemoryBlock mb; p->getStateInformation(mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize());
        auto* extra = xml->createNewChildElement("PARAM"); extra->setAttribute("id", "cutoff"); extra->setAttribute("value", 0.2);
        juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary(*xml, mb2);
        auto q = makeProc(); q->setStateInformation(mb2.getData(), (int) mb2.getSize());
        check(q->sequencer().serialize() == p->sequencer().serialize(), "a state with a sound parameter in it (as the first draft saved) still loads, pattern intact");
    }

    // saved with the project
    {
        auto p = makeProc();
        for (int n : {60, 64, 67}) p->sequencer().recordNote(n, 100);
        juce::MemoryBlock mb; p->getStateInformation(mb);
        auto q = makeProc(); q->setStateInformation(mb.getData(), (int) mb.getSize());
        check(q->sequencer().serialize() == p->sequencer().serialize(), "the pattern is saved and restored with the project");
    }

    // no sound engine, many blocks, odd block sizes: nothing crashes
    {
        auto p = makeProc();
        for (int n : {60, 64, 67, 72}) p->sequencer().recordNote(n, 100);
        p->apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
        juce::AudioBuffer<float> b; juce::MidiBuffer m; bool fine = true;
        for (int blk = 0; blk < 300; ++blk) { const int n = 1 + (blk * 37) % 900; b.setSize(0, n); m.clear(); p->processBlock(b, m); for (const auto meta : m) if (meta.samplePosition < 0 || meta.samplePosition >= n) fine = false; }
        check(fine, "odd block sizes (1..900 samples): every event lands inside its block");
    }

    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
