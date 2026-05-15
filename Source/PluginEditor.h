#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/AmbienceUI.h"
#include "GUI/DecayCurveViz.h"

class FDNReverbEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit FDNReverbEditor(FDNReverbAudioProcessor&);
    ~FDNReverbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updatePanelVisibility();

    FDNReverbAudioProcessor& audioProcessor;
    AmbienceLookAndFeel laf;

    // ─── 共通 ──
    AlgorithmSelector algoSelector;
    RT60Visualizer    rt60Viz;
    DecayCurveViz     decayCurveViz;
    VUMeter           vuIn, vuOut;
    juce::Label       titleLabel;

    juce::Label labelMetricsTitle;
    juce::Label labelD50Caption, labelD50Value;
    juce::Label labelC50Caption, labelC50Value;
    juce::Label labelC80Caption, labelC80Value;
    juce::Label labelEDTCaption, labelEDTValue;

    juce::TextButton proModeButton;
    juce::TextButton erSoloButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> proModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> erSoloAttachment;

    bool isProMode{ false };

    // ─── Normal Mode ノブ ──
    ArcKnob kPreDelay, kRoomSize, kDecay;
    ArcKnob kHFDamp, kLFAbsorb;
    ArcKnob kDiffusion, kModAmt, kModRate;
    ArcKnob kStereoW;
    ArcKnob kERLevel, kSaturation;
    ArcKnob kWet, kDry;
    ArcKnob kDuckAmt, kDuckThr, kDuckAtt, kDuckRel;

    // ─── ★ Phase 5: Output EQ ノブ (Normal Mode 用) ──
    ArcKnob kLoCutNorm;
    ArcKnob kHiCutNorm;

    // ─── ProMode パネル ──
    std::array<ArcKnob, 10> kRTBands;
    juce::Label    satTypeLabel;
    juce::ComboBox satTypeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satTypeAttachment;
    ArcKnob kTiltLow, kTiltMid, kTiltHigh;

    // ─── ★ Phase 5: Output EQ ノブ (ProMode 用) ──
    // Normal Mode 版と同じ APVTS パラメータに紐付くため値は同期する
    ArcKnob kLoCutPro;
    ArcKnob kHiCutPro;

    static constexpr int W = 900;
    static constexpr int H = 540;
    static constexpr int PAD = 8;
    static constexpr int KNOB_W = 64;
    static constexpr int KNOB_H = 72;
    static constexpr int KNOB_LBL_H = 14;
    static constexpr int UNIT_H = 88;
    static constexpr int ROW1_GAP = 18;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbEditor)
};