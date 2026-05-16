#pragma once
#include <cmath>
#include <algorithm>

namespace FDNReverb {

    enum class SaturationMode {
        Warm = 0,
        Tape = 1,
        Tube = 2,
        Hard = 3
    };

    class Saturator {
    public:
        Saturator() = default;

        void reset() noexcept {
            prevInput = 0.0f;
            switch (currentMode) {
            case SaturationMode::Warm: prevF = 1.0f; break;
            case SaturationMode::Tape: prevF = 0.0f; break;
            case SaturationMode::Tube: prevF = 1.0f; break;
            case SaturationMode::Hard: prevF = 0.0f; break;
            }
        }

        void setMode(SaturationMode mode) noexcept {
            if (mode != currentMode) {
                currentMode = mode;
                reset();
            }
        }

        void setMode(int modeIndex) noexcept {
            setMode(static_cast<SaturationMode>(std::clamp(modeIndex, 0, 3)));
        }

        // ─────────────────────────────────────────────────────────────────────────
        //  ★ Step B 修正: ドライブカーブのみ変更、ADAA 構造は完全保持
        // ─────────────────────────────────────────────────────────────────────────
        //   drive = 1 + amount³ × 1.0 (最大 2.0) → 1 + amount² × 2.5 (最大 3.5)
        //
        //   amount | drive(旧) | drive(新) | 差
        //   ───────|──────────|──────────|────────────────────
        //   0.30   | 1.027    | 1.225    | +約 7.5dB 強化
        //   0.50   | 1.125    | 1.625    | +約 3.2dB 強化
        //   0.70   | 1.343    | 2.225    | +約 4.4dB 強化
        //   1.00   | 2.000    | 3.500    | +約 4.9dB 強化
        //
        //   → プラグインドクターで 2次・4次倍音が可視化レベルになる
        // ─────────────────────────────────────────────────────────────────────────
        void setAmount(float amount) noexcept {
            amount = std::clamp(amount, 0.0f, 1.0f);
            currentAmount = amount;

            // ★ Step B: amount² × 2.5 に強化
            drive = 1.0f + amount * amount * 2.5f;
            wetMix = amount * amount * 0.7f;
            dryMix = 1.0f - amount * 0.25f;
        }

        inline float processSample(float input) noexcept {
            if (currentAmount < 1e-4f) return input;

            const float dryInput = input;
            const float driven = input * drive;
            float saturated = 0.0f;

            switch (currentMode) {
            case SaturationMode::Warm: saturated = processWarm(driven); break;
            case SaturationMode::Tape: saturated = processTape(driven); break;
            case SaturationMode::Tube: saturated = processTube(driven); break;
            case SaturationMode::Hard: saturated = processHard(driven); break;
            }

            saturated /= drive;
            return dryInput * dryMix + saturated * wetMix;
        }

    private:
        // ─── Warm: Vicanek x/√(1+x²) + ADAA 1次 ───
        inline float processWarm(float x) noexcept {
            const float F_x = std::sqrt(1.0f + x * x);
            const float dx = x - prevInput;
            float y;
            constexpr float kTol = 1e-5f;
            if (std::abs(dx) < kTol) {
                const float xAvg = (x + prevInput) * 0.5f;
                y = xAvg / std::sqrt(1.0f + xAvg * xAvg);
            }
            else {
                y = (F_x - prevF) / dx;
            }
            prevInput = x;
            prevF = F_x;
            return y;
        }

        // ─── Tape: Padé x(27+x²)/(27+9x²) (ADAA なし・意図的) ───
        inline float processTape(float x) noexcept {
            if (x > 3.0f) { prevInput = x; return  1.0f; }
            if (x < -3.0f) { prevInput = x; return -1.0f; }
            const float xsq = x * x;
            prevInput = x;
            return x * (27.0f + xsq) / (27.0f + 9.0f * xsq);
        }

        // ─────────────────────────────────────────────────────────────────────────
        //  Tube: 非対称 ADAA + ★ Step B: kNeg 1.5 → 2.0
        // ─────────────────────────────────────────────────────────────────────────
        //   正側: f(x) = x/√(1+x²)            F(x) = √(1+x²)
        //   負側: f(x) = x/√(1+(kNeg·x)²)     F(x) = (1/kNeg²)√(1+(kNeg·x)²) + fShift
        //
        //   C¹ 連続性: x=0 で F_pos(0) = F_neg(0) = 1 となるよう fShift を設計
        //     F_pos(0) = √1 = 1
        //     F_neg(0) = (1/kNeg²)·√1 + fShift = 1
        //              → fShift = 1 - 1/kNeg²
        //
        //   kNeg=2.0 の場合: fShift = 1 - 0.25 = 0.75
        //
        //   kNeg 強化の効果:
        //     負半波の圧縮が正半波の 2 倍 → 波形の非対称性が増大
        //     → 偶数次倍音 (2f, 4f) がプラグインドクターで可視化レベルに
        // ─────────────────────────────────────────────────────────────────────────
        inline float processTube(float x) noexcept {
            // ★ Step B: kNeg = 1.5f → 2.0f
            constexpr float kNeg = 2.0f;
            constexpr float kNeg2 = kNeg * kNeg;        // 4.0f
            constexpr float invKneg2 = 1.0f / kNeg2;      // 0.25f
            constexpr float fShift = 1.0f - invKneg2;   // 0.75f

            float F_x;
            if (x >= 0.0f) {
                F_x = std::sqrt(1.0f + x * x);
            }
            else {
                const float kx = kNeg * x;
                F_x = invKneg2 * std::sqrt(1.0f + kx * kx) + fShift;
            }

            const float dx = x - prevInput;
            const bool  signChanged = (x >= 0.0f) != (prevInput >= 0.0f);

            float y;
            constexpr float kTol = 1e-5f;
            if (std::abs(dx) < kTol || signChanged) {
                // 入力が微小変化またはゼロ交差 → 直接評価にフォールバック
                if (x >= 0.0f) {
                    y = x / std::sqrt(1.0f + x * x);
                }
                else {
                    const float kx = kNeg * x;
                    y = x / std::sqrt(1.0f + kx * kx);
                }
            }
            else {
                y = (F_x - prevF) / dx;
            }

            prevInput = x;
            prevF = F_x;
            return y;
        }

        // ─── Hard: ハードクリッピング + ADAA 1次 ───
        inline float processHard(float x) noexcept {
            float F_x;
            if (x > 1.0f) F_x = x - 0.5f;
            else if (x < -1.0f) F_x = -x - 0.5f;
            else                F_x = x * x * 0.5f;

            const float dx = x - prevInput;
            float y;
            constexpr float kTol = 1e-5f;
            if (std::abs(dx) < kTol) {
                y = std::clamp(x, -1.0f, 1.0f);
            }
            else {
                y = (F_x - prevF) / dx;
            }

            prevInput = x;
            prevF = F_x;
            return y;
        }

        float          prevInput{ 0.0f };
        float          prevF{ 1.0f };
        SaturationMode currentMode{ SaturationMode::Warm };
        float          currentAmount{ 0.0f };
        float          drive{ 1.0f };
        float          wetMix{ 0.0f };
        float          dryMix{ 1.0f };
    };

} // namespace FDNReverb