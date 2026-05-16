#pragma once

#include <JuceHeader.h>
#include <array>

namespace FDNReverb {

    namespace ParamID {
        inline const juce::String Algorithm = "algorithm";
        inline const juce::String PreDelay = "predelay";
        inline const juce::String RoomSize = "roomsize";
        inline const juce::String DecayTime = "decaytime";
        inline const juce::String HFDamping = "hfdamping";
        inline const juce::String LFAbsorption = "lfabsorption";
        inline const juce::String Diffusion = "diffusion";
        inline const juce::String ModAmount = "modamount";
        inline const juce::String ModRate = "modrate";
        inline const juce::String StereoWidth = "stereowidth";
        inline const juce::String ERLevel = "erlevel";
        inline const juce::String Saturation = "saturation";
        inline const juce::String SatType = "sattype";
        inline const juce::String WetLevel = "wetlevel";
        inline const juce::String DryLevel = "drylevel";
        inline const juce::String DuckAmount = "duckamount";
        inline const juce::String DuckAttack = "duckattack";
        inline const juce::String DuckRelease = "duckrelease";
        inline const juce::String DuckThresh = "duckthresh";
        inline const juce::String ERSolo = "ersolo";
        inline const juce::String ProMode = "promode";
        inline const juce::String TiltLow = "tiltlow";
        inline const juce::String TiltMid = "tiltmid";
        inline const juce::String TiltHigh = "tilthigh";
        inline const juce::String RTBand0 = "rtband0";
        inline const juce::String RTBand1 = "rtband1";
        inline const juce::String RTBand2 = "rtband2";
        inline const juce::String RTBand3 = "rtband3";
        inline const juce::String RTBand4 = "rtband4";
        inline const juce::String RTBand5 = "rtband5";
        inline const juce::String RTBand6 = "rtband6";
        inline const juce::String RTBand7 = "rtband7";
        inline const juce::String RTBand8 = "rtband8";
        inline const juce::String RTBand9 = "rtband9";
        inline const juce::String LoCut = "locut";
        inline const juce::String HiCut = "hicut";
    }

    struct DSPParams {
        int   algorithmIndex{ 0 };
        float decayScale{ 1.0f };
        float roomSizeScale{ 1.0f };
        float hfDamping{ 0.0f };
        float lfAbsorption{ 0.0f };
        float diffusion{ 0.70f };
        float preDelayMs{ 10.0f };
        float modAmount{ 0.25f };
        float modRate{ 0.5f };
        float stereoWidth{ 0.80f };
        float erLevel{ 0.6f };
        float lateLevel{ 1.0f };
        // ★ Step B: デフォルト Wet -4dB / Dry 0dB
        float wetDB{ -4.0f };
        float dryDB{ 0.0f };
        float saturation{ 0.0f };
        int   satTypeIdx{ 0 };
        float duckingAmount{ 0.0f };
        float duckingAttackMs{ 10.0f };
        float duckingRelMs{ 200.0f };
        float duckingThreshDB{ -20.0f };
        bool  erSolo{ false };
        bool  proMode{ false };
        float tiltLow{ 1.0f };
        float tiltMid{ 1.0f };
        float tiltHigh{ 1.0f };
        std::array<float, 10> rtBands{ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                         1.0f, 1.0f, 1.0f, 1.0f, 1.0f } };
        float loCutHz{ 20.0f };
        float hiCutHz{ 20000.0f };

        bool operator==(const DSPParams& o) const noexcept {
            return algorithmIndex == o.algorithmIndex
                && decayScale == o.decayScale
                && roomSizeScale == o.roomSizeScale
                && hfDamping == o.hfDamping
                && lfAbsorption == o.lfAbsorption
                && diffusion == o.diffusion
                && preDelayMs == o.preDelayMs
                && modAmount == o.modAmount
                && modRate == o.modRate
                && stereoWidth == o.stereoWidth
                && erLevel == o.erLevel
                && lateLevel == o.lateLevel
                && wetDB == o.wetDB
                && dryDB == o.dryDB
                && saturation == o.saturation
                && satTypeIdx == o.satTypeIdx
                && duckingAmount == o.duckingAmount
                && duckingAttackMs == o.duckingAttackMs
                && duckingRelMs == o.duckingRelMs
                && duckingThreshDB == o.duckingThreshDB
                && erSolo == o.erSolo
                && proMode == o.proMode
                && tiltLow == o.tiltLow
                && tiltMid == o.tiltMid
                && tiltHigh == o.tiltHigh
                && rtBands == o.rtBands
                && loCutHz == o.loCutHz
                && hiCutHz == o.hiCutHz;
        }
        bool operator!=(const DSPParams& o) const noexcept { return !(*this == o); }
    };

    class ParameterHelper {
    public:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    };

} // namespace FDNReverb