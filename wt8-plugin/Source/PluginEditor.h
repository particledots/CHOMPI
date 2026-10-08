#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "RingLayout.h"
#include <memory>
#include <vector>

// Look: dark matte panel, amber value arcs, small-caps labels. Pure JUCE drawing, no image assets.
class IpmohcLookAndFeel : public juce::LookAndFeel_V4
{
  public:
    IpmohcLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                          float startAngle, float endAngle, juce::Slider&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
};

// 32 clickable steps (2 rows of 16, or since v0.7 optionally one ring of 16 per page, or two rings side by side) with seven lanes.
// Which lane is shown and edited is chosen with the EDIT buttons:
//   PITCH:  click = note/rest, drag up/down or mouse wheel = pitch (in scale steps when a scale is set).
//   PROB:   drag up/down or wheel = that step's probability, double-click = back to 100%.
//   RATCH:  drag up/down or wheel = number of repeats (1-8), double-click = 1.
//   GATE:   drag up/down or wheel = that step's own gate, double-click = follow the GATE knob again.
//   ACCENT: click = accent on/off.
//   OCT:    drag up/down or wheel = chance (0-100 %) that the step jumps by the OCT JUMP setting, double-click = 0.
//   COND:   drag up/down or wheel = trigger condition ("pass 2 of every 3"), double-click = ALL (every pass).
class StepGrid : public juce::Component
{
  public:
    enum class Lane { Pitch, Prob, Ratchet, Gate, Accent, Oct, Cond };

    /** GRID = the 2 x 16 cells. RING (v0.7) = the same steps on one ring of 16 (page 1-16 / 17-32). 2 RINGS (v0.7) = all 32 steps
        on two rings side by side as a figure-eight (see RingLayout.h: 1-9 and 26-32 on the left ring, 10-25 on the right).
        In the ring views each step's value for the chosen lane is the fill level of its button, and the exact value of the step
        under the mouse is written in the middle of its ring. Display only: click / vertical drag / wheel / double-click edit the
        same things in every view, and switching views does not change the pattern. */
    enum class View { Grid, Ring, TwoRings };

    explicit StepGrid(StepSequencer& s);
    void refresh(); // pulls the pattern from the sequencer, repaints if anything changed
    void setLane(Lane l) { if (l != lane_) { lane_ = l; repaint(); } }
    void setView(View v);
    void setScale(int scale, int root) { scale_ = scale; root_ = root; } // scale -1 = off: pitch editing is chromatic
    void setGlobalGate(float g) { if (g != globalGate_) { globalGate_ = g; if (lane_ == Lane::Gate) repaint(); } } // 0.05..1, the GATE knob
    void setLoopLength(int n) { if (n != loopLen_) { loopLen_ = n; if (view_ != View::Grid) repaint(); } } // the LOOP knob (0 = whole pattern)
    void setPage(int p);     // ring view: 0 = steps 1-16, 1 = steps 17-32
    void setHover(int step); // the step the readout in the middle of its ring describes (-1 = none). The mouse sets it itself; editor_snapshot sets it too
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

  private:
    static constexpr int kRingSlots = 16;
    struct RingGeometry { juce::Point<float> centre; float outerR, buttonR, trackR; };
    int numRings() const { return view_ == View::TwoRings ? 2 : 1; }
    RingGeometry makeGeometry(juce::Point<float> centre, float outerR) const;
    RingGeometry singleGeometry() const;                 // the one big ring of the RING view (also where its page buttons are placed)
    RingGeometry ringGeometry(int ring) const;           // RING view: ring 0 only; 2 RINGS view: 0 = left, 1 = right
    juce::Point<float> ringSlotCentre(int ring, int slot) const;
    int slotToStep(int ring, int slot) const;            // the step a slot shows in the current view
    bool stepToSlot(int step, int& ring, int& slot) const; // where a step is shown; false when it is not on screen (RING view, other page)
    void paintGrid(juce::Graphics&);
    void paintRing(juce::Graphics&);
    void paintRingReadout(juce::Graphics&, int ring);
    /** What a lane shows for one step: its text, the bar fill 0..1, whether the value is a non-default one and whether the bar
        is dimmed (a gate that follows the GATE knob). Both views use this, so they always agree. */
    juce::String describe(const SeqStep&, float& fill, bool& bright, bool& dimBar) const;
    int cellAt(juce::Point<int>) const;      // global step index under the point (-1 = none); in the ring view the button under the point
    juce::Rectangle<float> cellBounds(int i) const;
    int effectiveGate(const SeqStep&) const; // the gate % the step really uses (its own, or the GATE knob's)
    int laneValue(const SeqStep&) const;     // the number a drag starts from in the current lane
    void setLaneValue(int index, int value); // writes `value` into the current lane (no-op for PITCH / ACCENT)
    static const char* laneName(Lane);

    StepSequencer& seq_;
    SeqStep steps_[StepSequencer::kMaxSteps];
    int len_ = 0, playing_ = -1;
    int dragIdx_ = -1, dragStartY_ = 0, dragStartValue_ = 0;
    bool dragged_ = false;
    Lane lane_ = Lane::Pitch;
    View view_ = View::Grid;
    int scale_ = -1, root_ = 0;
    float globalGate_ = 0.5f;
    int loopLen_ = 0;  // 0 = whole pattern; the ring view dims the steps beyond the loop
    int page_ = 0;     // ring view page
    int hover_ = -1;
    juce::TextButton page1Btn_{"1-16"}, page2Btn_{"17-32"};
};

class WT8Editor : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit WT8Editor(WT8AudioProcessor&);
    ~WT8Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    void timerCallback() override;
    enum class Kind { Int, Octave, Pitch, Percent, Pan, Decibels, LoopLength, Seed, Swing };

    struct Knob
    {
        juce::Slider slider;
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    struct Group
    {
        juce::String title;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds; // filled in by resized()
    };

    // v0.8: presets (sound settings as files, chosen from the header) and pattern slots (row C)
    void refreshPresetList(const juce::String& select);
    void stepPreset(int dir);
    void promptSavePreset();
    void savePresetAs(const juce::String& name);
    void refreshPatternBox();
    void stepPatternSlot(int dir);

    Knob& addKnob(const juce::String& paramId, const juce::String& label, Kind kind, bool bipolar = false);
    void addGroup(const juce::String& title, std::initializer_list<Knob*> knobs);

    WT8AudioProcessor& proc_;
    IpmohcLookAndFeel laf_;

    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<Group> row1_, row2_;

    // sequencer strip
    StepGrid grid_;
    juce::TextButton recBtn_{"REC"}, restBtn_{"REST"}, delBtn_{"DEL"}, clearBtn_{"CLEAR"}, playBtn_{"PLAY"}, muteBtn_{"MUTE"};
    juce::TextButton pitchLaneBtn_{"PITCH"}, probLaneBtn_{"PROB"}, ratchLaneBtn_{"RATCH"}, gateLaneBtn_{"GATE"},
                     accentLaneBtn_{"ACCENT"}, octLaneBtn_{"OCT"}, condLaneBtn_{"COND"}, pendBtn_{"ENDS x2"};
    juce::TextButton gridViewBtn_{"GRID"}, ringViewBtn_{"RING"}, twoRingsViewBtn_{"2 RINGS"}; // v0.7: which view the steps are shown in (editor state only, not saved)
    juce::TextButton xposeBtn_{"MIDI XPOSE"}, xposeResetBtn_{"RESET"};
    // v0.8
    juce::ComboBox presetBox_, patBox_;
    juce::TextButton presetPrevBtn_{"<"}, presetNextBtn_{">"}, presetSaveBtn_{"SAVE"}, presetFolderBtn_{"FOLDER"}, patPrevBtn_{"<"}, patNextBtn_{">"};
    juce::Label patLabel_;
    juce::StringArray presetNames_;   // the files in the preset folder; combo item id = index + 2 (id 1 = INIT)
    juce::StringArray patTexts_;      // what the PATTERN box currently shows for each slot, so it is only touched when something changed
    juce::ComboBox syncBox_, divBox_, dirBox_, scaleBox_, rootBox_, octBox_;
    juce::Label syncLabel_, divLabel_, laneLabel_, dirLabel_, scaleLabel_, rootLabel_, octLabel_, xposeReadout_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playAtt_, muteAtt_, pendAtt_, xposeAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncAtt_, divAtt_, dirAtt_, scaleAtt_, rootAtt_, octAtt_;
    std::vector<Knob*> seqKnobs_; // GATE PROB / LOOP SEED / SWING ACCENT (2 columns x 3 rows at the right of the strip)
    juce::Rectangle<int> seqBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WT8Editor)
};
