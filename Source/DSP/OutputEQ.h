#pragma once

#include <cmath>
#include <algorithm>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  OutputEQ: Wet 出力段の Lo/Hi Cut (Linkwitz-Riley 12dB/oct)
    // ─────────────────────────────────────────────────────────────────────────────
    //   設計方針:
    //     - 1次 IIR (6dB/oct) × 2段カスケード = 12dB/oct
    //     - Linkwitz-Riley トポロジー: 同一カットオフの 2 段で位相整合性を保つ
    //     - 残響テイル専用の音楽的なロールオフ
    //
    //   フィルタ式 (1次 IIR):
    //     HPF: y[n] = R · (y[n-1] + x[n] - x[n-1])
    //     LPF: y[n] = (1 - R) · x[n] + R · y[n-1]
    //     where R = exp(-2π·fc/fs)
    //
    //   リアルタイム安全性:
    //     - メモリアロケーション: 完全になし
    //     - per-sample コスト: HPF 8 命令 + LPF 6 命令 (L/R 合計)
    //     - 係数更新はブロック単位で十分 (zipper noise は SmoothedValue 不要レベル)
    //
    //   バイパス挙動:
    //     - Lo Cut が 20Hz 以下 → HPF 完全スキップ
    //     - Hi Cut が 20kHz 以上 → LPF 完全スキップ
    //     これによりユーザーが「オフ」にしたとき、CPU 負荷ゼロかつ位相ズレなし。
    // ─────────────────────────────────────────────────────────────────────────────
    class OutputEQ {
    public:
        OutputEQ() = default;

        void prepare(double sampleRate) noexcept {
            fs = sampleRate;
            reset();
            setLoCutHz(20.0f);
            setHiCutHz(20000.0f);
        }

        void reset() noexcept {
            // HPF 状態 (1段目 + 2段目, L/R)
            hpfX1_L_1 = hpfY1_L_1 = 0.0f;
            hpfX1_L_2 = hpfY1_L_2 = 0.0f;
            hpfX1_R_1 = hpfY1_R_1 = 0.0f;
            hpfX1_R_2 = hpfY1_R_2 = 0.0f;
            // LPF 状態 (1段目 + 2段目, L/R)
            lpfY1_L_1 = 0.0f;
            lpfY1_L_2 = 0.0f;
            lpfY1_R_1 = 0.0f;
            lpfY1_R_2 = 0.0f;
        }

        // ─── カットオフ設定 (ブロック単位で呼ぶ) ───
        void setLoCutHz(float fcHz) noexcept {
            currentLoCutHz = fcHz;
            // 20Hz 以下はバイパス扱い (R が極小になり計算意味なし)
            if (fcHz <= 20.0f) {
                loCutActive = false;
                return;
            }
            loCutActive = true;
            constexpr float twoPi = 6.28318530718f;
            const float clamped = std::clamp(fcHz, 20.0f, 500.0f);
            loCutR = std::exp(-twoPi * clamped / static_cast<float>(fs));
        }

        void setHiCutHz(float fcHz) noexcept {
            currentHiCutHz = fcHz;
            // 20kHz 以上はバイパス扱い
            const float nyquist = static_cast<float>(fs) * 0.45f;
            const float clamped = std::clamp(fcHz, 1000.0f, std::min(20000.0f, nyquist));
            if (fcHz >= 20000.0f) {
                hiCutActive = false;
                return;
            }
            hiCutActive = true;
            constexpr float twoPi = 6.28318530718f;
            hiCutR = std::exp(-twoPi * clamped / static_cast<float>(fs));
        }

        // ─── サンプル単位の処理 (L/R を同時処理) ───
        inline void process(float& l, float& r) noexcept {
            // ── Lo Cut: 1次 HPF × 2段カスケード ──
            if (loCutActive) {
                // L 1段目
                const float l_in = l;
                const float l_1 = loCutR * (hpfY1_L_1 + l_in - hpfX1_L_1);
                hpfX1_L_1 = l_in;
                hpfY1_L_1 = l_1;
                // L 2段目
                const float l_2 = loCutR * (hpfY1_L_2 + l_1 - hpfX1_L_2);
                hpfX1_L_2 = l_1;
                hpfY1_L_2 = l_2;
                l = l_2;

                // R 1段目
                const float r_in = r;
                const float r_1 = loCutR * (hpfY1_R_1 + r_in - hpfX1_R_1);
                hpfX1_R_1 = r_in;
                hpfY1_R_1 = r_1;
                // R 2段目
                const float r_2 = loCutR * (hpfY1_R_2 + r_1 - hpfX1_R_2);
                hpfX1_R_2 = r_1;
                hpfY1_R_2 = r_2;
                r = r_2;
            }

            // ── Hi Cut: 1次 LPF × 2段カスケード ──
            if (hiCutActive) {
                const float oneMinusR = 1.0f - hiCutR;
                // L 1段目
                lpfY1_L_1 = oneMinusR * l + hiCutR * lpfY1_L_1;
                // L 2段目
                lpfY1_L_2 = oneMinusR * lpfY1_L_1 + hiCutR * lpfY1_L_2;
                l = lpfY1_L_2;

                // R 1段目
                lpfY1_R_1 = oneMinusR * r + hiCutR * lpfY1_R_1;
                // R 2段目
                lpfY1_R_2 = oneMinusR * lpfY1_R_1 + hiCutR * lpfY1_R_2;
                r = lpfY1_R_2;
            }
        }

        float getCurrentLoCutHz() const noexcept { return currentLoCutHz; }
        float getCurrentHiCutHz() const noexcept { return currentHiCutHz; }

    private:
        double fs{ 48000.0 };

        // ── Lo Cut (HPF) ──
        bool  loCutActive{ false };
        float loCutR{ 0.0f };
        float currentLoCutHz{ 20.0f };
        float hpfX1_L_1{}, hpfY1_L_1{}, hpfX1_L_2{}, hpfY1_L_2{};
        float hpfX1_R_1{}, hpfY1_R_1{}, hpfX1_R_2{}, hpfY1_R_2{};

        // ── Hi Cut (LPF) ──
        bool  hiCutActive{ false };
        float hiCutR{ 0.0f };
        float currentHiCutHz{ 20000.0f };
        float lpfY1_L_1{}, lpfY1_L_2{};
        float lpfY1_R_1{}, lpfY1_R_2{};
    };

} // namespace FDNReverb