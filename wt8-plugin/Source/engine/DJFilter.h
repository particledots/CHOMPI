// One-knob DJ filter: low-pass below center, high-pass above, with resonance.
// Derived from Electrosmith DSP source.

#pragma once
#include "BasicMMF.h"
#include "FilterMaps.h"

using namespace chompi;

/** TODO: replace Tone and ATone with something cheap with resonance*/
class DjFilter
{
  public:
    void Init(float samplerate)
    {
        sr_ = samplerate;

        feedback_filt_llp_.Init(samplerate);
        feedback_filt_rlp_.Init(samplerate);
        feedback_filt_lhp_.Init(samplerate);
        feedback_filt_rhp_.Init(samplerate);

        feedback_filt_llp_.SetMode(BasicMMF::Mode::Lowpass);
        feedback_filt_rlp_.SetMode(BasicMMF::Mode::Lowpass);
        feedback_filt_lhp_.SetMode(BasicMMF::Mode::Highpass);
        feedback_filt_rhp_.SetMode(BasicMMF::Mode::Highpass);

        feedback_filt_llp_.SetFreq(.99f);
        feedback_filt_rlp_.SetFreq(.99f);
        feedback_filt_lhp_.SetFreq(0.f);
        feedback_filt_rhp_.SetFreq(0.f);

        feedback_filt_llp_.SetRes(.6f);
        feedback_filt_rlp_.SetRes(.6f);
        feedback_filt_lhp_.SetRes(.6f);
        feedback_filt_rhp_.SetRes(.6f);

        // v0.19: the single-filter types
        single_.Init(samplerate);
        single_.SetRes(singleFilterResonance(.6f / .95f));
        SetType(0);
    }

    /** v0.19 filter type: 0 = the DJ filter (as always: low-pass below the middle of the cutoff, high-pass above it),
     *  1 = low-pass, 2 = high-pass, 3 = band-pass (one two-pole filter, the whole cutoff range sweeps it). */
    void SetType(int type)
    {
        type_ = type < 0 || type > 3 ? 0 : type;
        if (type_ == 1) single_.SetMode(BasicMMF::Mode::Lowpass);
        else if (type_ == 2) single_.SetMode(BasicMMF::Mode::Highpass);
        else if (type_ == 3) single_.SetMode(BasicMMF::Mode::Bandpass);
    }
    int GetType() const { return type_; }

    /** v0.19: multiplies the filter's frequency (key tracking, velocity). 1 = no change, and then nothing is touched. */
    void SetFreqScale(float s) { scale_ = s; }
    
    void Process(float in_l, float in_r, float *out_l, float* out_r)
    {
        if (type_ != 0)
        {
            // v0.19 single filter (the input is the same on both sides, so one filter serves both)
            daisysp::fonepole(single_freq_, single_target_, .01f);
            single_.SetFreq(single_freq_);
            const float o = single_.Process(in_l);
            *out_l = o;
            *out_r = o;
            return;
        }
        daisysp::fonepole(lp_, lp_target_, .0002f);
        daisysp::fonepole(hp_, hp_target_, .0002f);

        feedback_filt_llp_.SetFreq(lp_);
        feedback_filt_rlp_.SetFreq(lp_);

        feedback_filt_lhp_.SetFreq(hp_);
        feedback_filt_rhp_.SetFreq(hp_);

        if(hp_ > .8f)
        {
            const float param = 5.f * (1.f - cutoff_);
            feedback_filt_lhp_.SetRes(res_ * param);
            feedback_filt_rhp_.SetRes(res_ * param);
        }

        float filt_l = feedback_filt_llp_.Process(in_l);
        float filt_r = feedback_filt_rlp_.Process(in_r);
        
        filt_l = feedback_filt_lhp_.Process(filt_l);
        filt_r = feedback_filt_rhp_.Process(filt_r);
        
        *out_l = filt_l;
        *out_r = filt_r;
    }

    void SetControl(float cutoff)
    {
        cutoff_ = cutoff;
        lp_target_ = daisysp::fclamp(.01f + cutoff_ * 2.f, 0.f, .98f); //these have to be limited
        lp_target_ = lp_target_ * lp_target_ * lp_target_;

        hp_target_ = daisysp::fclamp((cutoff_ * 1.9f) - 1.f, 0.f, .9f);
        hp_target_ = hp_target_ * hp_target_ * hp_target_;

        if (type_ != 0) // v0.19: the whole knob range sweeps the single filter (cubic, about 15 Hz to 17 kHz at 48 kHz)
        {
            const float c = daisysp::fclamp(cutoff_, 0.f, 1.f);
            single_target_ = .002f + .9f * c * c * c;
            if (scale_ != 1.f) single_target_ = daisysp::fclamp(single_target_ * scale_, .0005f, .9f);
        }
        else if (scale_ != 1.f) // v0.19 key tracking / velocity: scales both stages; never past the limits they always had
        {
            lp_target_ = daisysp::fmin(lp_target_ * scale_, .98f * .98f * .98f);
            hp_target_ = daisysp::fmin(hp_target_ * scale_, .9f * .9f * .9f);
        }
    }
    float GetControl() { return cutoff_; }

    void SetRes(float res) 
    {
        res *= .95f;
        res_ = res;
        feedback_filt_llp_.SetRes(res);
        feedback_filt_rlp_.SetRes(res);

        feedback_filt_lhp_.SetRes(res);
        feedback_filt_rhp_.SetRes(res);
        single_.SetRes(singleFilterResonance(res / .95f)); // v0.20: a little more resonance in the new types (the DJ filter above is untouched)
    }

    float sr_;
    BasicMMF   feedback_filt_llp_, feedback_filt_rlp_;
    BasicMMF   feedback_filt_lhp_, feedback_filt_rhp_;
    float lp_min, lp_max, hp_min, hp_max;
    float cutoff_, res_;
    float lp_, lp_target_;
    float hp_, hp_target_;
    // v0.19
    BasicMMF single_;
    int   type_ = 0;
    float scale_ = 1.f;
    float single_freq_ = .1f, single_target_ = .1f;
};