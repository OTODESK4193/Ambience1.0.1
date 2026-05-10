#include "MagnitudeResponseFitter.h"
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>
#include <complex>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  静的メンバの定義
    // ─────────────────────────────────────────────────────────────────────────────
    std::array<std::array<double, NUM_BANDS>, NUM_BANDS> MagnitudeResponseFitter::cachedB;
    std::array<std::array<double, NUM_BANDS>, NUM_BANDS> MagnitudeResponseFitter::cachedBtWB;
    std::array<double, NUM_BANDS> MagnitudeResponseFitter::cachedW;
    double MagnitudeResponseFitter::cachedSampleRate = 0.0;
    bool MagnitudeResponseFitter::cacheValid = false;

    // ─────────────────────────────────────────────────────────────────────────────
    //  バンド Q 値（オクターブバンド: Q ≈ √2 / (2^(1/2) - 2^(-1/2)) ≈ 1.414）
    // ─────────────────────────────────────────────────────────────────────────────
    // Välimäki–Liski 流のシンメトリックBiquad では Q を一定に保つのが基本
    // ただし両端 (31Hz, 16kHz) は近接バンドが片側にしかないので Q を上げて干渉を抑える
    static const std::array<float, NUM_BANDS> kBandQs = {
        1.7f,  // 31.25 Hz (端: Q 上昇)
        1.414f, // 62.5 Hz
        1.414f, // 125 Hz
        1.414f, // 250 Hz
        1.414f, // 500 Hz
        1.414f, // 1 kHz
        1.414f, // 2 kHz
        1.414f, // 4 kHz
        1.414f, // 8 kHz
        1.7f   // 16 kHz (端: Q 上昇)
    };

    const std::array<float, NUM_BANDS>& MagnitudeResponseFitter::getBandQs() noexcept {
        return kBandQs;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 1 ヘルパー（既存）
    // ─────────────────────────────────────────────────────────────────────────────

    float MagnitudeResponseFitter::t60ToLoopGain(float t60Seconds, int delaySamples, double sampleRate) noexcept {
        float t60Safe = std::max(0.01f, t60Seconds);
        float exponent = -3.0f * static_cast<float>(delaySamples) / (static_cast<float>(sampleRate) * t60Safe);
        return std::pow(10.0f, exponent);
    }

    float MagnitudeResponseFitter::computeJotPole(float gDC, float alphaRatio) noexcept {
        float alphaSafe = juce::jlimit(0.05f, 20.0f, alphaRatio);
        float gDCSafe = juce::jlimit(1e-6f, 0.99999f, gDC);
        constexpr float kLn10Over4 = 0.5756462732485f;
        float log10g = std::log10(gDCSafe);
        float alphaSqInv = 1.0f / (alphaSafe * alphaSafe);
        float pole = kLn10Over4 * log10g * (1.0f - alphaSqInv);
        return juce::jlimit(-0.98f, 0.98f, pole);
    }

    BiquadCoeffs MagnitudeResponseFitter::orthogonalizedFirstOrderToBiquad(float gain, float pole) noexcept {
        BiquadCoeffs c;
        c.b0 = gain * (1.0f - pole);
        c.b1 = 0.0f;
        c.b2 = 0.0f;
        c.a1 = -pole;
        c.a2 = 0.0f;
        return c;
    }

    float MagnitudeResponseFitter::getT60AtDC(const std::array<float, NUM_BANDS>& rt60) noexcept {
        return (rt60[0] + rt60[1]) * 0.5f;
    }

    float MagnitudeResponseFitter::getT60AtNyquist(const std::array<float, NUM_BANDS>& rt60, double sampleRate) noexcept {
        if (sampleRate <= 50000.0) {
            return rt60[9];
        }
        else {
            return (rt60[8] + rt60[9]) * 0.5f;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 1 メイン設計関数（既存）
    // ─────────────────────────────────────────────────────────────────────────────

    MagnitudeResponseFitter::DesignResult MagnitudeResponseFitter::design(
        int delaySamples,
        double sampleRate,
        const std::array<float, NUM_BANDS>& rt60,
        float hfDamping,
        float lfAbsorption)
    {
        DesignResult result;

        float t60DC = std::max(0.01f, getT60AtDC(rt60));
        float t60Nyq = std::max(0.01f, getT60AtNyquist(rt60, sampleRate));

        float gDC = t60ToLoopGain(t60DC, delaySamples, sampleRate);
        float gNyq = t60ToLoopGain(t60Nyq, delaySamples, sampleRate);

        float alpha = t60Nyq / t60DC;
        float pole = computeJotPole(gDC, alpha);

        result.coeffs[0] = orthogonalizedFirstOrderToBiquad(gDC, pole);

        float lfShelfDB = -lfAbsorption * 3.0f;
        result.coeffs[1] = FilterDesign::lowShelf(150.0f, lfShelfDB, sampleRate);

        float hfShelfDB = -hfDamping * 6.0f;
        result.coeffs[2] = FilterDesign::highShelf(4000.0f, hfShelfDB, sampleRate);

        result.dcGain = gDC;
        result.nyquistGain = gNyq;
        result.pole = pole;

        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2 ヘルパー: シンメトリックBiquad ピークフィルタ
    // ─────────────────────────────────────────────────────────────────────────────
    //  RBJ Cookbook の peakingEQ をベースに、ゲインに対して対称な応答を持つよう設計
    //  (Välimäki–Liski 2017 の "symmetric biquad" 形式)
    //
    //  ゲイン dB が正/負どちらでも、振幅応答が逆の対数関係になる:
    //      |H(e^jω)|_dB(g)  +  |H(e^jω)|_dB(-g)  =  0
    //  これは Interaction Matrix を線形系として扱うために重要な性質。
    BiquadCoeffs MagnitudeResponseFitter::designSymmetricPeakBiquad(
        float fcHz, float gainDB, float Q, double sampleRate) noexcept
    {
        // 周波数を Nyquist 内にクランプ
        float fcSafe = juce::jlimit(10.0f, static_cast<float>(sampleRate) * 0.49f, fcHz);

        // RBJ Cookbook peakingEQ (シンメトリック性は A の取り方で保証される)
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w0 = 2.0f * juce::MathConstants<float>::pi * fcSafe / static_cast<float>(sampleRate);
        float cosW0 = std::cos(w0);
        float sinW0 = std::sin(w0);
        float alpha = sinW0 / (2.0f * std::max(0.1f, Q));

        float a0 = 1.0f + alpha / A;

        BiquadCoeffs c;
        c.b0 = (1.0f + alpha * A) / a0;
        c.b1 = -2.0f * cosW0 / a0;
        c.b2 = (1.0f - alpha * A) / a0;
        c.a1 = -2.0f * cosW0 / a0;
        c.a2 = (1.0f - alpha / A) / a0;
        return c;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2 ヘルパー: Biquad 振幅応答 (dB) 計算
    // ─────────────────────────────────────────────────────────────────────────────
    //  H(e^jω) = (b0 + b1·e^{-jω} + b2·e^{-j2ω}) / (1 + a1·e^{-jω} + a2·e^{-j2ω})
    //
    //  注意: BiquadCoeffs の a1, a2 は標準形式 (1 + a1·z^{-1} + a2·z^{-2}) として扱う
    //        (BiquadState::tick の実装と整合)
    float MagnitudeResponseFitter::biquadMagnitudeDB(
        const BiquadCoeffs& c, float fEval, double sampleRate) noexcept
    {
        double w = 2.0 * juce::MathConstants<double>::pi * fEval / sampleRate;
        double cosW = std::cos(w);
        double sinW = std::sin(w);
        double cos2W = std::cos(2.0 * w);
        double sin2W = std::sin(2.0 * w);

        // 分子 B(e^jω) = b0 + b1·(cosω - j·sinω) + b2·(cos2ω - j·sin2ω)
        double bRe = c.b0 + c.b1 * cosW + c.b2 * cos2W;
        double bIm = -c.b1 * sinW - c.b2 * sin2W;

        // 分母 A(e^jω) = 1 + a1·(cosω - j·sinω) + a2·(cos2ω - j·sin2ω)
        double aRe = 1.0 + c.a1 * cosW + c.a2 * cos2W;
        double aIm = -c.a1 * sinW - c.a2 * sin2W;

        double bMag2 = bRe * bRe + bIm * bIm;
        double aMag2 = aRe * aRe + aIm * aIm;

        // |H|² = |B|² / |A|²
        double mag2 = bMag2 / std::max(1e-30, aMag2);

        // 20·log10(|H|) = 10·log10(|H|²)
        return static_cast<float>(10.0 * std::log10(std::max(1e-30, mag2)));
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2 ヘルパー: 10x10 LDLT 分解ソルバ
    // ─────────────────────────────────────────────────────────────────────────────
    //  対称正定値行列 A に対して LDL^T 分解を行い、A·x = b を解く。
    //
    //  LDLT は Cholesky の変種で平方根を使わないため数値安定性が高く、
    //  対角成分の符号も追跡できる。Eigen の .ldlt().solve() と同等の処理を
    //  10x10 専用に手書きしたもの。
    //
    //  アルゴリズム (Golub & Van Loan, "Matrix Computations" §4.1):
    //    For j = 0 to N-1:
    //      D[j] = A[j][j] - Σ_{k<j} L[j][k]² · D[k]
    //      For i = j+1 to N-1:
    //        L[i][j] = (A[i][j] - Σ_{k<j} L[i][k]·L[j][k]·D[k]) / D[j]
    //
    //  解 x の計算:
    //    L·z = b      (前進代入)
    //    D·y = z      (対角除算)
    //    L^T·x = y    (後退代入)
    void MagnitudeResponseFitter::solveLDLT10(
        const std::array<std::array<double, NUM_BANDS>, NUM_BANDS>& A,
        const std::array<double, NUM_BANDS>& b,
        std::array<double, NUM_BANDS>& x) noexcept
    {
        constexpr int N = NUM_BANDS;
        double L[N][N] = { 0 };  // 単位下三角
        double D[N] = { 0 };     // 対角

        // L の対角を 1 に初期化
        for (int i = 0; i < N; ++i) L[i][i] = 1.0;

        // LDLT 分解
        for (int j = 0; j < N; ++j) {
            double sum = A[j][j];
            for (int k = 0; k < j; ++k) {
                sum -= L[j][k] * L[j][k] * D[k];
            }
            D[j] = sum;

            // 対角の数値安定化 (悪条件に対する小さな正則化)
            if (std::abs(D[j]) < 1e-12) {
                D[j] = (D[j] < 0.0 ? -1e-12 : 1e-12);
            }

            for (int i = j + 1; i < N; ++i) {
                double s = A[i][j];
                for (int k = 0; k < j; ++k) {
                    s -= L[i][k] * L[j][k] * D[k];
                }
                L[i][j] = s / D[j];
            }
        }

        // 前進代入: L·z = b
        double z[N];
        for (int i = 0; i < N; ++i) {
            double s = b[i];
            for (int k = 0; k < i; ++k) s -= L[i][k] * z[k];
            z[i] = s;
        }

        // 対角除算: D·y = z
        double y[N];
        for (int i = 0; i < N; ++i) y[i] = z[i] / D[i];

        // 後退代入: L^T·x = y
        for (int i = N - 1; i >= 0; --i) {
            double s = y[i];
            for (int k = i + 1; k < N; ++k) s -= L[k][i] * x[k];
            x[i] = s;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2: Interaction Matrix の事前計算
    // ─────────────────────────────────────────────────────────────────────────────
    //  B[i][j] は「j 番目のフィルタを 1 dB に設定したときの、
    //             i 番目のバンド中心 fc[i] で観測される dB 値」
    //
    //  シンメトリックBiquad は線形性を持つので、g_cmd[j] dB に設定すれば
    //  バンド i での観測 dB は Σ_j B[i][j] · g_cmd[j] となる。
    //
    //  目標 dB ベクトル t に対して B·g_cmd = t を WLS で解く:
    //      g_cmd = (B^T·W·B)^(-1) · B^T·W·t
    //
    //  本関数は B, W, B^T·W·B を一括で計算しキャッシュする。
    //  サンプルレートが変わったときのみ呼び出せばよい。
    void MagnitudeResponseFitter::precomputeInteractionMatrix(double sampleRate) {
        if (cacheValid && std::abs(cachedSampleRate - sampleRate) < 0.5) {
            return;  // 既にキャッシュ済み
        }

        constexpr int N = NUM_BANDS;
        constexpr float kProbeGainDB = 1.0f;  // プローブゲイン (1 dB)

        // ── 各フィルタを 1 dB に設定したときの応答を測定 ──
        for (int j = 0; j < N; ++j) {
            BiquadCoeffs c = designSymmetricPeakBiquad(
                BAND_FREQ[j], kProbeGainDB, kBandQs[j], sampleRate);

            for (int i = 0; i < N; ++i) {
                // バンド i の中心周波数で応答を評価
                float dB = biquadMagnitudeDB(c, BAND_FREQ[i], sampleRate);
                cachedB[i][j] = static_cast<double>(dB);
            }
        }

        // ── 重み W の決定 ──
        // 中域 (250Hz〜4kHz) を最重要視し、両端は重みを下げて
        // 数値安定性を確保する (Stage 2 の重み付き設計の核心)
        const std::array<double, NUM_BANDS> weights = {
            0.5,   // 31.25 Hz
            0.7,   // 62.5 Hz
            0.85,  // 125 Hz
            1.0,   // 250 Hz
            1.0,   // 500 Hz
            1.0,   // 1 kHz
            1.0,   // 2 kHz
            1.0,   // 4 kHz
            0.85,  // 8 kHz
            0.6    // 16 kHz
        };
        for (int i = 0; i < N; ++i) cachedW[i] = weights[i];

        // ── B^T · W · B の計算 ──
        // (これを LDLT で分解しておけば、目標 t が変わるたびに解くのが高速になる)
        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < N; ++j) {
                double s = 0.0;
                for (int k = 0; k < N; ++k) {
                    s += cachedB[k][i] * cachedW[k] * cachedB[k][j];
                }
                cachedBtWB[i][j] = s;
            }
        }

        // ── 対角に小さな正則化項を加える (Tikhonov, ridge regression) ──
        // 行列が悪条件になるのを防ぎ、過剰なゲイン振動を抑える
        constexpr double kRidge = 1e-4;
        for (int i = 0; i < N; ++i) cachedBtWB[i][i] += kRidge;

        cachedSampleRate = sampleRate;
        cacheValid = true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2: メイン設計関数
    // ─────────────────────────────────────────────────────────────────────────────
    //  各遅延線につき:
    //    1. 10 バンドの目標 dB ベクトル t を作成
    //         t[i] = -60 · m / (fs · T60[i])  ... これが i 番目のバンド中心で
    //                                             ループ1周あたりに減衰すべき dB 値
    //    2. プリシェルフ (Two-Stage の第1段) を設計し、その応答を t から差し引く
    //         残差 t_residual を GEQ でフィットする
    //    3. 中域基準ゲイン midGain を抽出し、t を平均化 (DC 成分の分離)
    //    4. WLS で g_cmd = (B^T·W·B)^(-1)·B^T·W·t_residual を解く
    //    5. 各 g_cmd[j] dB を Biquad 係数に変換
    //    6. ユーザー LF/HF 補正を別段で配置
    MagnitudeResponseFitter::DesignResultStage2 MagnitudeResponseFitter::designStage2(
        int delaySamples,
        double sampleRate,
        const std::array<float, NUM_BANDS>& rt60,
        float hfDamping,
        float lfAbsorption)
    {
        // Interaction Matrix のキャッシュを保証
        precomputeInteractionMatrix(sampleRate);

        DesignResultStage2 result;

        constexpr int N = NUM_BANDS;
        const float fs = static_cast<float>(sampleRate);
        const float m = static_cast<float>(delaySamples);

        // ── Step 1: 各バンドのループ1周ゲインを dB 単位で目標化 ──
        // 目標: 20·log10(|H_i(e^jω_band)|) = -60·m / (fs·T60_band)
        std::array<float, NUM_BANDS> targetDb;
        for (int i = 0; i < N; ++i) {
            float t60Safe = std::max(0.01f, rt60[i]);
            targetDb[i] = -60.0f * m / (fs * t60Safe);
        }

        // ── Step 2: 中域基準ゲイン midGain を抽出 (DC 成分の分離) ──
        // 500Hz バンド (index 4) を基準とする
        float midDb = targetDb[4];
        float midGainLinear = std::pow(10.0f, midDb / 20.0f);
        result.midGain = midGainLinear;

        // 残差 dB ベクトル: 各バンドの偏差 (中域基準からの差)
        std::array<double, NUM_BANDS> residualDb;
        for (int i = 0; i < N; ++i) {
            residualDb[i] = static_cast<double>(targetDb[i] - midDb);
            result.targetDb[i] = targetDb[i];
        }

        // ── Step 3: プリシェルフ (Two-Stage の第1段) を設計 ──
        // Nyquist 端の急峻な減衰を 1 次ハイシェルフで粗く当てる
        // GEQ だけだと 16kHz バンドの両側 (8kHz と Nyquist) で発散しやすい
        float t60Nyq = std::max(0.01f, rt60[N - 1]);
        float gNyqDB = -60.0f * m / (fs * t60Nyq);
        float preShelfDB = (gNyqDB - midDb) * 0.4f;  // 40%だけプリシェルフに任せる
        result.preFilter = FilterDesign::highShelf(8000.0f, preShelfDB, sampleRate);

        // プリシェルフが各バンド中心で生成する dB を残差から差し引く
        for (int i = 0; i < N; ++i) {
            float preDb = biquadMagnitudeDB(result.preFilter, BAND_FREQ[i], sampleRate);
            residualDb[i] -= static_cast<double>(preDb);
        }

        // ── Step 4: WLS による GEQ 係数の決定 ──
        // B^T · W · t_residual を計算
        std::array<double, NUM_BANDS> rhs;
        for (int j = 0; j < N; ++j) {
            double s = 0.0;
            for (int k = 0; k < N; ++k) {
                s += cachedB[k][j] * cachedW[k] * residualDb[k];
            }
            rhs[j] = s;
        }

        // (B^T·W·B) · g_cmd = B^T·W·t を LDLT で解く
        std::array<double, NUM_BANDS> gCmd;
        solveLDLT10(cachedBtWB, rhs, gCmd);

        // ── Step 5: 各 g_cmd[j] dB を Biquad 係数に変換 ──
        for (int j = 0; j < N; ++j) {
            // dB 値を安全範囲にクランプ (±18 dB 以内)
            float gDb = static_cast<float>(juce::jlimit(-18.0, 18.0, gCmd[j]));
            result.commandDb[j] = gDb;
            result.geqStages[j] = designSymmetricPeakBiquad(
                BAND_FREQ[j], gDb, kBandQs[j], sampleRate);
        }

        // ── Step 6: ユーザー LF/HF 補正を別段で配置 ──
        float lfShelfDB = -lfAbsorption * 3.0f;
        result.lfUserShelf = FilterDesign::lowShelf(150.0f, lfShelfDB, sampleRate);

        float hfShelfDB = -hfDamping * 6.0f;
        result.hfUserShelf = FilterDesign::highShelf(4000.0f, hfShelfDB, sampleRate);

        return result;
    }

} // namespace FDNReverb