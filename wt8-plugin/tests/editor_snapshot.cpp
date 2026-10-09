// Renders the plugin editor offscreen and writes a PNG:  ./editor_snapshot out.png [width] [lane] [view] [hover] [steps] [loop] [blocks] [slot] [preset] [copy]
// lane: pitch (default), prob, ratch, gate, accent, oct, cond.  view: grid (default), ring, ring2 (ring, page 17-32), 2rings.
// hover: step number (1-32) the ring's centre readout should describe, as if the mouse were over it (0 = none).
// steps: pattern length (default 12; more steps are added with varied notes and probabilities).  loop: the LOOP knob (default 10, 0 = ALL).
// blocks: extra 512-sample blocks to run, to move the playing step (about 12 blocks per 1/16 step at 120 bpm, 48 kHz).
// slot: pattern slot to select (1-16; it gets a note so it counts as used).  Slots 3 and 6 always hold a copy of slot 1, so their dots show.
// preset (v0.9): none (default), init, s<n> = starter preset n loaded, s<n>mod = starter n loaded and then CUTOFF moved (the "modified" marker).
// copy (v0.9): "copy" = press COPY, to show the armed state.
#include "PluginEditor.h"
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    WT8AudioProcessor proc;
    // Show a few non-default values so the arcs are visible
    auto set = [&](const char* id, float norm) { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(norm); };
    set("table", 0.33f); set("octave", 1.0f); set("pitch", 0.62f); set("cutoff", 0.7f); set("pan", 0.3f);
    proc.setPlayConfigDetails(0, 2, 48000.0, 512); proc.prepareToPlay(48000.0, 512);
    // v0.12: [12] = FRAME knob 0..32 (picture of the table), [13] = a .wav LOADed into the selected table slot (shows the USER label)
    if (argc > 12 && juce::String(argv[12]) != "-") set("cycle", (float) std::atoi(argv[12]) / 32.0f);
    if (argc > 13)
    {
        juce::String msg;
        const int slot = juce::jlimit(0, WT8AudioProcessor::kTableSlots - 1, juce::roundToInt(proc.apvts.getRawParameterValue("table")->load()) - 1);
        if (!proc.loadUserTable(slot, juce::File(argv[13]), msg)) { fprintf(stderr, "LOAD failed: %s\n", msg.toRawUTF8()); return 2; }
    }
    for (int n : {60, 64, 67, 72}) proc.sequencer().recordNote(n, 100);
    proc.sequencer().addRest(); for (int n : {65, 62, 59, 55, 57, 60, 64}) proc.sequencer().recordNote(n, 90);
    for (int i = 0; i < 12; ++i) proc.sequencer().setProb(i, i % 4 == 0 ? 100 : 100 - i * 7); // a few non-default step probabilities
    const int totalSteps = argc > 6 ? std::atoi(argv[6]) : 12;
    for (int i = 12; i < totalSteps && i < 32; ++i) { proc.sequencer().recordNote(55 + (i * 7) % 19, 90); proc.sequencer().setProb(i, 35 + (i * 13) % 66); }
    const int loopKnob = argc > 7 ? std::atoi(argv[7]) : 10;
    set("seq_dir", 2.0f / 3.0f); set("seq_prob", 0.8f); set("seq_loop", loopKnob / 32.0f); set("seq_seed", 0.0f);
    // v0.6: a few non-default per-step values and settings so every lane has something to show
    for (int i : {0, 4, 8, 12}) proc.sequencer().setRatchet(i, i == 0 ? 3 : (i == 4 ? 2 : (i == 8 ? 4 : 8)));
    proc.sequencer().setStepGate(2, 80); proc.sequencer().setStepGate(6, 25); proc.sequencer().setStepGate(9, 100);
    for (int i : {1, 3, 6, 10}) proc.sequencer().toggleAccent(i);
    proc.sequencer().setOctChance(5, 40); proc.sequencer().setOctChance(7, 100); proc.sequencer().setOctChance(11, 15);
    proc.sequencer().setCondition(3, 1, 3); proc.sequencer().setCondition(7, 2, 4); proc.sequencer().setCondition(9, 3, 8);
    set("seq_swing", 0.667f); set("seq_accent", 0.5f); set("seq_octmode", 2.0f / 5.0f);
    set("seq_scale", 6.0f / 28.0f); set("seq_root", 9.0f / 11.0f); set("seq_xpose", 1.0f); // Aeolian (Natural Minor), root A, MIDI XPOSE on
    proc.setSeqTranspose(2);
    proc.copyPatternSlot(0, 2); proc.copyPatternSlot(0, 5); // v0.9: slots 3 and 6 hold a pattern too
    proc.apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
    {
        juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m;
        const int blocks = 2 + (argc > 8 ? std::atoi(argv[8]) : 0);
        for (int i = 0; i < blocks; ++i) { m.clear(); proc.processBlock(b, m); }
    }
    if (argc > 9) { proc.selectPatternSlot(std::atoi(argv[9]) - 1); proc.sequencer().recordNote(60, 100); } // v0.8: show the PATTERN box on another slot (that slot gets one note so it counts as used)
    if (argc > 10)
    {
        const juce::String spec(argv[10]);
        if (spec.equalsIgnoreCase("init")) proc.loadInitPreset();
        else if (spec.startsWithIgnoreCase("s"))
        {
            proc.loadStarterPreset(spec.substring(1).getIntValue() - 1);
            if (spec.endsWithIgnoreCase("mod")) set("cutoff", 0.9f);
        }
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    const int w = argc > 2 ? std::atoi(argv[2]) : 840;
    ed->setSize(w, int(w * 898.0 / 840.0));
    if (argc > 3)
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c))
                if (b->getRadioGroupId() == 1001 && b->getButtonText().equalsIgnoreCase(argv[3])) b->setToggleState(true, juce::sendNotificationSync);
    if (argc > 4)
    {
        const juce::String view(argv[4]);
        for (auto* c : ed->getChildren())
        {
            if (auto* b = dynamic_cast<juce::TextButton*>(c))
                if (b->getRadioGroupId() == 1002 && b->getButtonText().equalsIgnoreCase(view.equalsIgnoreCase("2rings") ? "2 rings" : (view.startsWithIgnoreCase("ring") ? "ring" : "grid")))
                    b->setToggleState(true, juce::sendNotificationSync);
        }
        for (auto* c : ed->getChildren())
            if (auto* grid = dynamic_cast<StepGrid*>(c))
            {
                if (view.equalsIgnoreCase("ring2")) grid->setPage(1);
                if (argc > 5) grid->setHover(std::atoi(argv[5]) - 1); // 0 = none
            }
    }
    if (argc > 11 && juce::String(argv[11]).equalsIgnoreCase("copy"))
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c))
                if (b->getButtonText() == "COPY") b->setToggleState(true, juce::sendNotificationSync);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    juce::File f(argc > 1 ? argv[1] : "editor.png");
    f.deleteFile();
    juce::FileOutputStream out(f);
    juce::PNGImageFormat().writeImageToStream(img, out);
    return 0;
}
