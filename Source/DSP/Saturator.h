#pragma once
#include <cmath>
#include <algorithm>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  Saturator: 一次ADAA 対応 Soft Clipping Saturation
    // ─────────────────────────────────────────────────────────────────────────────
    //   Vicanek (2018) の効率的な非線形関数を使用:
    //     f(x)  = x / sqrt(1 + x²)     ← 出力関数 (soft clipper)
    //     F(x)  = sqrt(1 + x²)         ← 第一不定積分 (ADAA 用)
    //
    //   ADAA (一次):
    //     y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
    //   
    //   フォールバック (差分が極小の場合):
    //     y[n] = f((x[n] + x[n-1]) / 2)
    //
    //   特性:
    //     - Vicanek 関数は tanh より軽量 (sqrt 1回 + 除算 1回)
    //     - 自然な対称 soft clipping
    //     - ADAA でエイリアス劇的減少 (特に低域)
    //     - 数値安定性のためのフォールバック内蔵
    // ─────────────────────────────────────────────────────────────────────────────
    class Saturator {
    public:
        Saturator() = default;

        void reset() noexcept {
            prevInput = 0.0f;
            prevF = 1.0f;  // F(0) = sqrt(1+0) = 1
        }

        // ─── 設定 ───
        // amount: 0.0 (バイパス) ~ 1.0 (最大ドライブ)
        void setAmount(float amount) noexcept {
            amount = std::clamp(amount, 0.0f, 1.0f);
            currentAmount = amount;
            // ドライブ量: 0.0 ~ 4.0 の範囲で非線形マッピング
            // 低 amount では穏やかに、高 amount では強烈に
            drive = 1.0f + amount * amount * 3.0f;
            // ウェット/ドライミックス (バイパス時はドライ 100%)
            wetMix = amount;
            dryMix = 1.0f - amount * 0.5f;  // ウェット時もドライ成分を残す
        }

        // ─── サンプル単位の処理 (ADAA 1次) ───
        inline float processSample(float input) noexcept {
            if (currentAmount < 1e-4f) {
                return input;  // 完全バイパス
            }

            // ドライブ適用
            float x = input * drive;

            // ADAA 1次計算
            float F_x = std::sqrt(1.0f + x * x);
            float dx = x - prevInput;
            float saturated;

            // 数値安定性のためのトレランスチェック
            constexpr float kTolerance = 1e-5f;
            if (std::abs(dx) < kTolerance) {
                // フォールバック: 平均値で評価
                float xAvg = (x + prevInput) * 0.5f;
                saturated = xAvg / std::sqrt(1.0f + xAvg * xAvg);
            }
            else {
                // 通常の ADAA 計算
                saturated = (F_x - prevF) / dx;
            }

            // 状態更新
            prevInput = x;
            prevF = F_x;

            // ゲイン補正 (ドライブで上昇した分を戻す)
            saturated /= drive;

            // ウェット/ドライミックス
            return input * dryMix + saturated * wetMix;
        }

    private:
        // 状態
        float prevInput{ 0.0f };
        float prevF{ 1.0f };

        // 設定値
        float currentAmount{ 0.0f };
        float drive{ 1.0f };
        float wetMix{ 0.0f };
        float dryMix{ 1.0f };
    };

} // namespace FDNReverb