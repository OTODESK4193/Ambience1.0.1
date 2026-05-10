#pragma once
#include "DelayMemory.h"
#include "BiquadFilters.h"
#include "../PluginParameters.h"
#include <array>
#include <random>

namespace FDNReverb {

    enum class ReverbTopology { Room, Hall, Plate, Spring, Goldfoil };

    // ─────────────────────────────────────────────────────────────────────────────
    // Random Walk LFO (非相関ノイズベースのモジュレーション)
    // ─────────────────────────────────────────────────────────────────────────────
    struct RandomWalkLFO {
        float value{ 0.0f };
        float target{ 0.0f };
        float coeff{ 0.005f };
        int stepsRemaining{ 0 };
        uint32_t state{ 12345 }; // 簡易Xorshift用シード

        inline float nextRandom() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (state * 2.3283064365386963e-10f) * 2.0f - 1.0f; // -1.0 to 1.0
        }

        inline float tick(float rateHz, float sampleRate) noexcept {
            if (stepsRemaining <= 0) {
                target = nextRandom();
                // LFOのレートに応じて新しいターゲットを設定する間隔を決定
                stepsRemaining = static_cast<int>(sampleRate / std::max(0.1f, rateHz));
                // スムージング係数の計算（ワンポール・ローパス）
                coeff = 1.0f - std::exp(-2.0f * 3.14159265f * rateHz / sampleRate);
            }
            stepsRemaining--;
            value += (target - value) * coeff;
            return value;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // ユニバーサルFDNエンジン (Super Structure)
    // ─────────────────────────────────────────────────────────────────────────────
    class UniversalEngine {
    public:
        UniversalEngine();
        void prepare(double sampleRate, int maxBlockSize);
        void reset();
        void setParams(const DSPParams& p);

        void processBlock(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

        std::array<float, NUM_BANDS> getEffectiveRT60() const noexcept { return effectiveRT60; }

    private:
        void updateTopologyAndRouting();
        void calculatePrimePowerDelays();

        inline void fastWalshHadamardTransform(std::array<float, 16>& v) noexcept;
        inline void applySignFlipping(std::array<float, 16>& v) noexcept;

        // ── メモリとコア ──
        DelayMemoryPool memoryPool;
        double fs{ 48000.0 };
        DSPParams activeParams;
        ReverbTopology currentTopology{ ReverbTopology::Room };

        // ── モジュール群 ──
        static constexpr int FDN_ORDER = 16;

        LinearDelayLine erDelay; // ER用タップディレイライン
        std::array<float, 16> erTaps; // ERのタップ位置(ms)

        std::array<LinearDelayLine, 4> inputDiffusers; // Plate/Goldfoil用

        std::array<LinearDelayLine, FDN_ORDER> fdnDelays;
        std::array<LinearDelayLine, FDN_ORDER> nestedAllpassDelays;
        std::array<BiquadState, FDN_ORDER> absorptionFilters;
        std::array<BiquadCoeffs, FDN_ORDER> currentAbsorptionCoeffs;
        std::array<RandomWalkLFO, FDN_ORDER> lfos;

        std::array<float, FDN_ORDER> fdnBaseDelaySamples;
        std::array<float, FDN_ORDER> fbVec; // フィードバック・ベクトル

        // トポロジー固有の内部パラメータ
        float apfGain{ 0.618f };
        bool bypassER{ false };
        bool bypassInputDiffusers{ true };
        float lateMixScale{ 1.0f };

        // ▼ 追加：動的メイクアップゲイン（Late Reverb専用）
        float lateMakeupGainLinear{ 1.0f };

        std::array<float, NUM_BANDS> effectiveRT60;
    };

} // namespace FDNReverb