// Renders the plugin editor offscreen and writes a PNG:  ./editor_snapshot out.png [width] [prob]
// (a third argument "prob" shows the grid in its PROB lane)
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
    for (int n : {60, 64, 67, 72}) proc.sequencer().recordNote(n, 100);
    proc.sequencer().addRest(); for (int n : {65, 62, 59, 55, 57, 60, 64}) proc.sequencer().recordNote(n, 90);
    for (int i = 0; i < 12; ++i) proc.sequencer().setProb(i, i % 4 == 0 ? 100 : 100 - i * 7); // a few non-default step probabilities
    set("seq_dir", 2.0f / 3.0f); set("seq_prob", 0.8f); set("seq_loop", 10.0f / 32.0f); set("seq_seed", 0.0f);
    set("seq_scale", 6.0f / 28.0f); set("seq_root", 9.0f / 11.0f); set("seq_xpose", 1.0f); // Aeolian (Natural Minor), root A, MIDI XPOSE on
    proc.setSeqTranspose(2);
    proc.apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
    { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; proc.processBlock(b, m); proc.processBlock(b, m); }
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    const int w = argc > 2 ? std::atoi(argv[2]) : 840;
    ed->setSize(w, int(w * 726.0 / 840.0));
    if (argc > 3 && std::string(argv[3]) == "prob")
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c))
                if (b->getButtonText() == "PROB") b->setToggleState(true, juce::sendNotificationSync);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    juce::File f(argc > 1 ? argv[1] : "editor.png");
    f.deleteFile();
    juce::FileOutputStream out(f);
    juce::PNGImageFormat().writeImageToStream(img, out);
    return 0;
}
