// Renders the plugin editor offscreen and writes a PNG:  ./editor_snapshot out.png [width]
#include "PluginEditor.h"
#include <cstdlib>

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
    proc.apvts.getParameter("seq_play")->setValueNotifyingHost(1.f);
    { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; proc.processBlock(b, m); proc.processBlock(b, m); }
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    const int w = argc > 2 ? std::atoi(argv[2]) : 840;
    ed->setSize(w, int(w * 620.0 / 840.0));
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    juce::File f(argc > 1 ? argv[1] : "editor.png");
    f.deleteFile();
    juce::FileOutputStream out(f);
    juce::PNGImageFormat().writeImageToStream(img, out);
    return 0;
}
