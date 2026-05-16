#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "AmbienceUI.h"

class DecayCurveViz : public juce::Component, private juce::Timer {
public:
    DecayCurveViz();
    ~DecayCurveViz() override;

    void setProcessor(FDNReverbAudioProcessor* p) noexcept { processor = p; }
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    // ─── 時間軸変換ヘルパー ───────────────────────────────────────────────
    //   スプリット時間軸:
    //     0〜splitSec  : plotW × splitRatio の幅に拡大表示
    //     splitSec〜max: 残りの幅に表示
    //   これにより ER（0〜200ms）が 2 倍の解像度で見える
    float timeToX(float timeSec, float plotX, float plotW,
        float maxTimeSec) const noexcept;

    FDNReverbAudioProcessor* processor{ nullptr };

    float cachedRT60Mid{ 1.0f };
    int   cachedERTapCount{ 0 };
    bool  cachedERBypassed{ false };

    static constexpr int MAX_DISPLAY_TAPS = 12;
    std::array<float, MAX_DISPLAY_TAPS> cachedERDelayMs;
    std::array<float, MAX_DISPLAY_TAPS> cachedERGains;

    // ─── スプリット時間軸の設定 ───
    // splitSec 以下の時間を splitRatio の割合の幅に拡大表示する
    static constexpr float splitSec = 0.20f;  // 0〜200ms を拡大
    static constexpr float splitRatio = 0.30f;  // 全幅の 30% を ER ゾーンに割り当て

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecayCurveViz)
};