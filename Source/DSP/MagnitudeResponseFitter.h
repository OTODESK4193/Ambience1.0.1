#pragma once
#include "DSPConstants.h"
#include "BiquadFilters.h"
#include "../AlgorithmPresets.h"
#include <array>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  MagnitudeResponseFitter
    // ─────────────────────────────────────────────────────────────────────────────
    //  10バンドのRT60ターゲットに対して、各遅延線の吸収フィルタを設計する。
    //
    //  設計モード:
    //    Stage 1 (Jot 1次直交化):
    //      Jot–Chaigne (AES Preprint 3030, 1991) の直交化1次フィルタ。
    //      DC と Nyquist の 2 点だけで設計する軽量版。
    //
    //    Stage 2 (Välimäki–Liski 累積バイカッドGEQ):
    //      Välimäki & Liski (IEEE SPL 2017) の Interaction Matrix + WLS による
    //      10バンド厳密フィッティング。
    //      Two-Stage Attenuation Filter (Välimäki/Prawda/Schlecht 2024) として
    //      プリシェルフ + 10段Biquad の二段構成。
    //
    //  重要:
    //    - dB スケールでなく T60 dB スケール (-60·m / (fs·T60)) でフィッティング
    //      Schlecht–Habets (DAFx-17) の「2 kHz で T60 が無限大に発散する罠」を回避
    //    - 設計はオフライン（メッセージスレッド）で行い、結果を Biquad 係数として
    //      オーディオスレッドに渡す
    // ─────────────────────────────────────────────────────────────────────────────
    class MagnitudeResponseFitter {
    public:
        enum class DesignMode {
            Stage1_Jot1stOrder,      // Jot 直交化1次 (DC/Nyquist 2点フィット)
            Stage2_BiquadGEQ          // Välimäki–Liski 累積バイカッドGEQ (10点厳密フィット)
        };

        // ─────────────────────────────────────────────────────────────────────────
        //  Stage 1 用設計結果（既存互換）
        // ─────────────────────────────────────────────────────────────────────────
        // ABSO_STAGES = 3 段の Biquad に格納される:
        //   coeffs[0] = ミッドゲイン（Jot 1次の直交化フィルタを Biquad 形式で）
        //   coeffs[1] = 低域補正（Low Shelf, LF Absorption ユーザー操作分）
        //   coeffs[2] = 高域補正（High Shelf, HF Damping ユーザー操作分）
        struct DesignResult {
            std::array<BiquadCoeffs, ABSO_STAGES> coeffs;
            float dcGain{ 1.0f };
            float nyquistGain{ 1.0f };
            float pole{ 0.0f };
        };

        // ─────────────────────────────────────────────────────────────────────────
        //  Stage 2 用設計結果（新規）
        // ─────────────────────────────────────────────────────────────────────────
        // Two-Stage 構成:
        //   preFilter = Two-Stage Attenuation Filter のプリシェルフ
        //               (DC と Nyquist 端の T60 を粗く当てる 1 次シェルフを Biquad 化)
        //               実装上は Low Shelf 1段 + High Shelf 1段の合成で表現するが、
        //               簡略化のため High Shelf 1段に集約 (DC は GEQ に任せる)
        //   geqStages = 10 オクターブバンド (31Hz〜16kHz) のシンメトリックBiquad GEQ
        //               Interaction Matrix + WLS で係数を決定
        //   midGain   = 全段共通の中域ループゲイン (周波数非依存項)
        //
        // フィルタ実行順序: midGain → preFilter → geqStages[0..9]
        struct DesignResultStage2 {
            float midGain{ 1.0f };                          // 中域ループゲイン (DC スカラー)
            BiquadCoeffs preFilter;                         // プリシェルフ (Nyquist 端補正)
            std::array<BiquadCoeffs, NUM_BANDS> geqStages;  // 10段 GEQ
            BiquadCoeffs lfUserShelf;                       // ユーザー LF Absorption 補正
            BiquadCoeffs hfUserShelf;                       // ユーザー HF Damping 補正

            // デバッグ・可視化用
            std::array<float, NUM_BANDS> targetDb;          // 各バンドの目標 dB
            std::array<float, NUM_BANDS> commandDb;         // WLS で求めた実コマンド dB
        };

        // ─────────────────────────────────────────────────────────────────────────
        //  Stage 1 設計関数（既存互換）
        // ─────────────────────────────────────────────────────────────────────────
        static DesignResult design(
            int delaySamples,
            double sampleRate,
            const std::array<float, NUM_BANDS>& rt60,
            float hfDamping,
            float lfAbsorption);

        // ─────────────────────────────────────────────────────────────────────────
        //  Stage 2 設計関数（新規）
        // ─────────────────────────────────────────────────────────────────────────
        static DesignResultStage2 designStage2(
            int delaySamples,
            double sampleRate,
            const std::array<float, NUM_BANDS>& rt60,
            float hfDamping,
            float lfAbsorption);

        // ─────────────────────────────────────────────────────────────────────────
        //  Interaction Matrix の事前計算（起動時に1回呼ぶ）
        // ─────────────────────────────────────────────────────────────────────────
        // サンプルレートが変わったら再計算する必要がある
        // 内部で 10x10 の double 行列をキャッシュ
        static void precomputeInteractionMatrix(double sampleRate);

        // 現在キャッシュされている Interaction Matrix のサンプルレート
        static double getCachedSampleRate() noexcept { return cachedSampleRate; }

    private:
        // ── Stage 1 ヘルパー ──
        static float t60ToLoopGain(float t60Seconds, int delaySamples, double sampleRate) noexcept;
        static float computeJotPole(float gDC, float alphaRatio) noexcept;
        static BiquadCoeffs orthogonalizedFirstOrderToBiquad(float gain, float pole) noexcept;
        static float getT60AtDC(const std::array<float, NUM_BANDS>& rt60) noexcept;
        static float getT60AtNyquist(const std::array<float, NUM_BANDS>& rt60, double sampleRate) noexcept;

        // ── Stage 2 ヘルパー ──

        // シンメトリックBiquad ピークフィルタの設計
        // (Välimäki–Liski 流: ゲインに対して対称な振幅応答を持つピークEQ)
        static BiquadCoeffs designSymmetricPeakBiquad(
            float fcHz, float gainDB, float Q, double sampleRate) noexcept;

        // バンド中心周波数の取得
        static const std::array<float, NUM_BANDS>& getBandFreqs() noexcept { return BAND_FREQ; }

        // バンド Q 値の取得（オクターブバンド用）
        static const std::array<float, NUM_BANDS>& getBandQs() noexcept;

        // フィルタの周波数応答振幅 (dB) を計算
        // 評価周波数 fEval [Hz] における Biquad の |H(e^jω)| (dB) を返す
        static float biquadMagnitudeDB(const BiquadCoeffs& c, float fEval, double sampleRate) noexcept;

        // 10x10 LDLT 分解 + 線形ソルバ
        // 入力: A (10x10, 対称正定値), b (10次)
        // 出力: x (10次) such that A·x = b
        static void solveLDLT10(
            const std::array<std::array<double, NUM_BANDS>, NUM_BANDS>& A,
            const std::array<double, NUM_BANDS>& b,
            std::array<double, NUM_BANDS>& x) noexcept;

        // ── Stage 2 静的キャッシュ ──
        // Interaction Matrix B[i][j] = 「j 番目のフィルタを 1 dB に設定したとき、
        //                              i 番目のバンド中心 fc[i] で観測される dB」
        // サンプルレートに依存するため変更時に再計算
        static std::array<std::array<double, NUM_BANDS>, NUM_BANDS> cachedB;
        static std::array<std::array<double, NUM_BANDS>, NUM_BANDS> cachedBtWB;  // B^T W B (LDLT 用)
        static std::array<double, NUM_BANDS> cachedW;  // 重み (対角行列の対角成分)
        static double cachedSampleRate;
        static bool cacheValid;
    };

} // namespace FDNReverb