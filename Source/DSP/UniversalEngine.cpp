#include "UniversalEngine.h"

namespace FDNReverb {

    namespace {
        static bool isMathPrime(int n) noexcept {
            if (n < 2)  return false;
            if (n == 2) return true;
            if (n % 2 == 0) return false;
            for (int i = 3; i * i <= n; i += 2)
                if (n % i == 0) return false;
            return true;
        }

        static int findNearestUniquePrime(int target,
            const std::array<int, 16>& usedPrimes,
            int usedCount) noexcept {
            target = std::max(target, 2);
            for (int offset = 0; offset < 100000; ++offset) {
                int hi = target + offset;
                if (isMathPrime(hi)) {
                    bool used = false;
                    for (int k = 0; k < usedCount; ++k)
                        if (usedPrimes[k] == hi) { used = true; break; }
                    if (!used) return hi;
                }
                int lo = target - offset;
                if (offset > 0 && lo >= 2 && isMathPrime(lo)) {
                    bool used = false;
                    for (int k = 0; k < usedCount; ++k)
                        if (usedPrimes[k] == lo) { used = true; break; }
                    if (!used) return lo;
                }
            }
            return target;
        }
    } // anonymous namespace

    UniversalEngine::UniversalEngine() {
        fbVec.fill(0.0f);
        constexpr float phi = 1.6180339887f;
        for (int i = 0; i < FDN_ORDER; ++i) {
            lfos[i].state = 12345u + static_cast<uint32_t>(i) * 9876u;
            lfos[i].smoothed = 0.0f;
            const float angle = static_cast<float>(i) * phi;
            const float frac = angle - std::floor(angle);
            lfos[i].rateMultiplier = 0.80f + frac * 0.40f;

            // ★ コーラスLFO: ノイズLFOとは異なるオフセットで黄金比分布
            const float cAngle = static_cast<float>(i + 5) * phi;
            chorusLFOs[i].phase = cAngle - std::floor(cAngle);
            const float cRateAngle = static_cast<float>(i + 11) * phi;
            chorusLFOs[i].rateScale = 0.30f + (cRateAngle - std::floor(cRateAngle)) * 0.50f;
        }
    }

    void UniversalEngine::prepare(double sampleRate, int /*maxBlockSize*/) {
        fs = sampleRate;

#if AMBIENCE_USE_STAGE2_ABSORPTION
        MagnitudeResponseFitter::precomputeInteractionMatrix(sampleRate);
#endif

        auto getPow2 = [](size_t s) -> size_t {
            size_t p = 1;
            while (p < s) p *= 2;
            return p;
            };

        size_t totalMemoryNeeded =
            getPow2(static_cast<size_t>(fs * 0.5))              // ★ preDelay (max 500ms)
            + getPow2(static_cast<size_t>(fs * 1.0))
            + getPow2(static_cast<size_t>(fs * 0.05)) * 4
            + getPow2(static_cast<size_t>(fs * 0.5)) * FDN_ORDER
            + getPow2(static_cast<size_t>(fs * 0.05)) * FDN_ORDER * SERIAL_APF_STAGES;

        memoryPool.allocate(totalMemoryNeeded);

        int mask = 0;
        float* ptr = nullptr;

        // ★ PreDelay (max 500ms)
        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
        preDelayLine.init(ptr, mask);

        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 1.0), mask);
        erDelay.init(ptr, mask);

        for (int i = 0; i < 4; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            inputDiffusers[i].init(ptr, mask);
        }

        for (int i = 0; i < FDN_ORDER; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
            fdnDelays[i].init(ptr, mask);
            for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
                nestedAllpassDelays[i][s].init(ptr, mask);
            }
        }

        acousticMetrics.prepare(sampleRate, 2000.0f);
        currentERTapCount = 0;
        currentERDelaySamples.fill(0.0f);
        currentERGains.fill(0.0f);
        outputLimiter.prepare(sampleRate);
        outputEQ.prepare(sampleRate);

        duckingAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.010f));
        duckingReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.200f));
        duckingEnvelope = 0.0f;

        // ★ DCブロッカー係数: fc ≈ 5Hz の1次HPF
        dcBlockerCoeff = 1.0f - (6.28318530718f * 5.0f / static_cast<float>(fs));
        dcX1.fill(0.0f);
        dcY1.fill(0.0f);

        // ★ Soft-kneeコンプ: RMSエンベロープ係数 (~3ms窓)
        fdnRmsEnv.fill(0.0f);
        rmsCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.003f));

        reset();
    }

    void UniversalEngine::reset() {
        memoryPool.clear();
        fbVec.fill(0.0f);

#if AMBIENCE_USE_STAGE2_ABSORPTION
        for (auto& lineFilters : absorptionFiltersS2)
            for (auto& f : lineFilters) f.reset();
#else
        for (auto& f : absorptionFilters) f.reset();
#endif

        acousticMetrics.reset();
        saturatorL.reset();
        saturatorR.reset();
        outputLimiter.reset();
        outputEQ.reset();
        duckingEnvelope = 0.0f;
        dcX1.fill(0.0f);
        dcY1.fill(0.0f);
        fdnRmsEnv.fill(0.0f);
        for (auto& dl : fdnDelays) dl.resetState();  // ★ Thiran allpass状態リセット
        for (auto& lfo : lfos) lfo.smoothed = 0.0f;
    }

    void UniversalEngine::setParams(const DSPParams& p) {
        activeParams = p;

        switch (p.algorithmIndex) {
        case 0: case 1: currentTopology = ReverbTopology::Room;     break;
        case 2: case 3: currentTopology = ReverbTopology::Hall;     break;
        case 4:         currentTopology = ReverbTopology::Plate;    break;
        case 5:         currentTopology = ReverbTopology::Spring;   break;
        case 6:         currentTopology = ReverbTopology::Goldfoil; break;
        }

        const float attMs = juce::jmax(0.1f, p.duckingAttackMs);
        const float relMs = juce::jmax(0.1f, p.duckingRelMs);
        duckingAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * attMs * 0.001f));
        duckingReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * relMs * 0.001f));

        // ★ PreDelay: ms → サンプル数に変換
        preDelaySamples = p.preDelayMs * 0.001f * static_cast<float>(fs);

        outputEQ.setLoCutHz(p.loCutHz);
        outputEQ.setHiCutHz(p.hiCutHz);

        updateTopologyAndRouting();
    }

    void UniversalEngine::calculatePrimePowerDelays() {
        const float fsf = static_cast<float>(fs);
        const float sizeCoeff = juce::jlimit(0.5f, 2.0f, activeParams.roomSizeScale + 1.0f);
        const float minDelayMs = 15.0f + sizeCoeff * 7.5f;
        const float maxDelayMs = 50.0f + sizeCoeff * 75.0f;
        const int minDelaySamples = std::max(11, static_cast<int>(minDelayMs * 0.001f * fsf));
        const int maxDelaySamples = static_cast<int>(maxDelayMs * 0.001f * fsf);

        const float logMin = std::log(static_cast<float>(minDelaySamples));
        const float logMax = std::log(static_cast<float>(maxDelaySamples));

        std::array<int, FDN_ORDER> usedPrimes;
        usedPrimes.fill(0);

        for (int i = 0; i < FDN_ORDER; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(FDN_ORDER - 1);
            const float logTgt = logMin + t * (logMax - logMin);
            const int   target = static_cast<int>(std::round(std::exp(logTgt)));
            const int   prime = findNearestUniquePrime(target, usedPrimes, i);
            usedPrimes[i] = prime;
            fdnBaseDelaySamples[i] = static_cast<float>(prime);
        }
    }

    void UniversalEngine::updateTopologyAndRouting() {
        calculatePrimePowerDelays();

        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];

        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;

        // ─────────────────────────────────────────────────────────────────────────
        //  ★ ②修正: proMode フラグに関係なく常に Tilt / 帯域ノブを適用する
        // ─────────────────────────────────────────────────────────────────────────
        //   旧実装: if (activeParams.proMode) { ... }
        //   ProMode を OFF にするとユーザーが設定した Tilt/帯域が無視され、
        //   RT60 グラフが元のカーブに戻っていた。
        //
        //   新実装: 常に適用。デフォルト値が全て 1.0f なので、
        //   ユーザーが変更していなければ scaledRT60 に変化はなく、
        //   アルゴリズム切替時に loadPresetDefaults() が 1.0f にリセットする
        //   ため、副作用は一切ない。
        // ─────────────────────────────────────────────────────────────────────────
        scaledRT60[0] *= activeParams.tiltLow;
        scaledRT60[1] *= activeParams.tiltLow;
        scaledRT60[2] *= activeParams.tiltLow;
        scaledRT60[3] *= activeParams.tiltMid;
        scaledRT60[4] *= activeParams.tiltMid;
        scaledRT60[5] *= activeParams.tiltMid;
        scaledRT60[6] *= activeParams.tiltMid;
        scaledRT60[7] *= activeParams.tiltHigh;
        scaledRT60[8] *= activeParams.tiltHigh;
        scaledRT60[9] *= activeParams.tiltHigh;
        for (int b = 0; b < NUM_BANDS; ++b)
            scaledRT60[b] *= activeParams.rtBands[b];

#if AMBIENCE_USE_STAGE2_ABSORPTION
        std::array<float, NUM_BANDS> targetDbAccum;
        targetDbAccum.fill(0.0f);

        for (int i = 0; i < FDN_ORDER; ++i) {
            auto s2 = MagnitudeResponseFitter::designStage2(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            for (int b = 0; b < NUM_BANDS; ++b) {
                currentAbsorptionCoeffsS2[i][b] = s2.geqStages[b];
                targetDbAccum[b] += s2.targetDb[b];
            }
        }

        const float representativeDelay = fdnBaseDelaySamples[FDN_ORDER / 2];
        for (int b = 0; b < NUM_BANDS; ++b) {
            const float avgTargetDb = targetDbAccum[b] / static_cast<float>(FDN_ORDER);
            if (avgTargetDb < -0.001f) {
                effectiveRT60[b] = -60.0f * representativeDelay
                    / (static_cast<float>(fs) * avgTargetDb);
            }
            else {
                effectiveRT60[b] = scaledRT60[b];
            }
            effectiveRT60[b] = juce::jlimit(0.05f, 30.0f, effectiveRT60[b]);
        }
#else
        effectiveRT60 = scaledRT60;
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto absoStages = FilterDesign::designAbsorption(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            currentAbsorptionCoeffs[i] = absoStages[0];
        }
#endif

        // ─────────────────────────────────────────────────────────────────────────
        //  ★ EDT 修正: 全帯域平均化で LF/HF 補正を反映
        // ─────────────────────────────────────────────────────────────────────────
        //   旧実装: effectiveRT60[4] (500Hz) の単一バンドのみ使用
        //   → HF Damping が高域を下げても EDT に反映されない
        //   → LF Absorption が低域を下げても EDT に反映されない
        //
        //   新実装: 中域バンド（125Hz〜4kHz = band 2〜7）の平均値を使用
        //   → 耳に聴こえやすい帯域を重視しつつ LF/HF 補正の影響を取り込む
        //   → 両端（31Hz, 63Hz, 8kHz, 16kHz）は除外（心理音響的に EDT への
        //     寄与が少ないため、外れ値による不安定化を防ぐ）
        // ─────────────────────────────────────────────────────────────────────────
        float rt60Mid = 0.0f;
        for (int b = 2; b <= 7; ++b)
            rt60Mid += effectiveRT60[b];
        rt60Mid = std::max(0.1f, rt60Mid / 6.0f);

        // ─────────────────────────────────────────────────────────────────────────
        //  ★ 金属音対策 (1): Decay依存マイクロサチュレーション
        // ─────────────────────────────────────────────────────────────────────────
        //  FDN ループ内の processMicroSaturation() は、短い残響では温かみを加えるが、
        //  長い残響では非線形歪みが多数回蓄積し、コムフィルタ構造の共鳴周波数を
        //  強調して金属的なキーン音を引き起こす。
        //
        //  対策: RT60 中域平均が 2.0s 以下なら従来通り適用、2.0s〜6.0s で漸減、
        //        6.0s 以上で完全バイパス。
        // ─────────────────────────────────────────────────────────────────────────
        microSatBlend = juce::jlimit(0.0f, 1.0f, 1.0f - (rt60Mid - 2.0f) / 4.0f);

        // ─────────────────────────────────────────────────────────────────────────
        //  ★ 金属音対策 (2): Decay依存モジュレーション深さスケーリング
        // ─────────────────────────────────────────────────────────────────────────
        //  長い残響ほど、コムフィルタのピークをぼかすために深いモジュレーションが必要。
        //  Lexicon / Strymon 等の高品位リバーブの標準手法。
        //
        //  ★ モジュレーション深さスケーリング（抑制版）
        //  RT60 ≤ 1.0s → 1.0x (変化なし)
        //  RT60 = 3.0s → 2.0x
        //  RT60 ≥ 5.0s → 3.0x (上限)
        // ─────────────────────────────────────────────────────────────────────────
        modDepthScale = 1.0f + juce::jlimit(0.0f, 2.0f, (rt60Mid - 1.0f) * 0.5f);

        constexpr float baseDB = 16.0f;
        float decayCompDB = 7.0f * std::log10(rt60Mid);

        static constexpr std::array<float, 7> algorithmOffsetDB = {
            +0.8f, +0.9f, +0.5f, +0.5f, +1.5f, +0.6f, +0.6f
        };
        float algoOffset = algorithmOffsetDB[juce::jlimit(0, 6, activeParams.algorithmIndex)];

        switch (currentTopology) {
        case ReverbTopology::Room:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.3f;   diffusionSensitivity = 1.0f;
            break;
        case ReverbTopology::Hall:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.618f; diffusionSensitivity = 1.0f;
            break;
        case ReverbTopology::Plate:
            bypassER = true;  bypassInputDiffusers = false;
            apfGain = 0.7f;   diffusionSensitivity = 0.7f;
            break;
        case ReverbTopology::Spring:
            bypassER = true;  bypassInputDiffusers = false;
            apfGain = 0.5f;   diffusionSensitivity = 0.5f;
            break;
        case ReverbTopology::Goldfoil:
            bypassER = true;  bypassInputDiffusers = false;
            apfGain = 0.75f;  diffusionSensitivity = 0.8f;
            break;
        }

        const auto& erPattern = PRESET_ER_PATTERNS[
            juce::jlimit(0, 6, activeParams.algorithmIndex)];
        currentERTapCount = erPattern.numTaps;
        float erSizeScale = 0.5f + activeParams.roomSizeScale;
        for (int i = 0; i < erPattern.numTaps; ++i) {
            currentERDelaySamples[i] = erPattern.taps[i].delayMs * 0.001f
                * static_cast<float>(fs) * erSizeScale;
            currentERGains[i] = erPattern.taps[i].gain;
        }
        if (erPattern.numTaps == 0) bypassER = true;

        float edtCoeff = 0.7f;
        switch (currentTopology) {
        case ReverbTopology::Room:     edtCoeff = 0.70f; break;
        case ReverbTopology::Hall:     edtCoeff = 0.95f; break;
        case ReverbTopology::Plate:    edtCoeff = 0.60f; break;
        case ReverbTopology::Spring:   edtCoeff = 0.50f; break;
        case ReverbTopology::Goldfoil: edtCoeff = 0.85f; break;
        }
        theoreticalEDT = rt60Mid * edtCoeff;

        float satMultiplier = 1.0f;
        switch (currentTopology) {
        case ReverbTopology::Room:     satMultiplier = 0.90f; break;
        case ReverbTopology::Hall:     satMultiplier = 0.93f; break;
        case ReverbTopology::Plate:    satMultiplier = 1.00f; break;
        case ReverbTopology::Spring:   satMultiplier = 1.05f; break;
        case ReverbTopology::Goldfoil: satMultiplier = 1.02f; break;
        }
        float effectiveSatAmount = juce::jlimit(0.0f, 1.0f,
            activeParams.saturation * satMultiplier);
        saturatorL.setAmount(effectiveSatAmount);
        saturatorR.setAmount(effectiveSatAmount);
        saturatorL.setMode(activeParams.satTypeIdx);
        saturatorR.setMode(activeParams.satTypeIdx);

        lateMakeupGainLinear = juce::Decibels::decibelsToGain(baseDB + decayCompDB + algoOffset);
    }

    inline void UniversalEngine::fastWalshHadamardTransform(
        std::array<float, 16>& v) noexcept
    {
        for (int h = 1; h < 16; h *= 2) {
            for (int i = 0; i < 16; i += h * 2) {
                for (int j = i; j < i + h; ++j) {
                    float x = v[j], y = v[j + h];
                    v[j] = x + y;
                    v[j + h] = x - y;
                }
            }
        }
        for (int i = 0; i < 16; ++i) v[i] *= 0.25f;
    }

    inline void UniversalEngine::applySignFlipping(
        std::array<float, 16>& v) noexcept
    {
        static constexpr std::array<float, 16> flip = {
             1.f, -1.f,  1.f, -1.f, -1.f,  1.f, -1.f,  1.f,
             1.f,  1.f, -1.f, -1.f, -1.f, -1.f,  1.f,  1.f
        };
        for (int i = 0; i < 16; ++i) v[i] *= flip[i];
    }

    void UniversalEngine::processBlock(const float* inL, const float* inR,
        float* outL, float* outR,
        int numSamples) noexcept
    {
        // ★ CPU最適化: fs を float にキャッシュ（processBlock 全域で使用）
        const float fsf = static_cast<float>(fs);

        // ★ モジュレーション深さ: 二乗カーブ + ベース係数抑制
        //   modAmount² でノブ低域を緩やかに、0.001f で全体深さを半減
        //   旧: modAmt=0.5 → 48smp(1ms) / 新: modAmt=0.5 → 12smp(0.25ms)
        const float modAmtCurved = activeParams.modAmount * activeParams.modAmount;
        const float depthSamples = modAmtCurved * 0.001f * fsf * modDepthScale;
        const float wetGain = juce::Decibels::decibelsToGain(activeParams.wetDB);
        const float stereoWidth = activeParams.stereoWidth;
        const float erLevel = activeParams.erLevel;
        const float lateLevel = activeParams.lateLevel;
        const bool  erSolo = activeParams.erSolo;
        const float duckThreshLin = juce::Decibels::decibelsToGain(activeParams.duckingThreshDB);
        const float duckAmountDB = activeParams.duckingAmount;

        const float effectiveDiffusion = activeParams.diffusion * diffusionSensitivity;
        const float diffuserGain = 0.25f + effectiveDiffusion * 0.55f;
        const float effectiveApfGain = apfGain * (0.60f + effectiveDiffusion * 0.40f);

        const float sideBoost = stereoWidth * 1.5f;
        const float erLeakage = (1.0f - stereoWidth) * 0.7f;

        // ★ CPU最適化: apfGainStage はループ不変 → 事前計算
        const float apfGainStage = effectiveApfGain * 0.78f;

        // ★ CPU最適化: freqModScale を事前計算（16ch分）
        std::array<float, FDN_ORDER> freqModScales;
        constexpr float invFdnM1 = 1.0f / static_cast<float>(FDN_ORDER - 1);
        for (int i = 0; i < FDN_ORDER; ++i)
            freqModScales[i] = 0.5f + (1.0f - static_cast<float>(i) * invFdnM1) * 1.0f;

        // ★ CPU最適化: 入力ディフューザのディレイ時間を事前計算
        std::array<float, 4> diffuserDelaySmp;
        for (int i = 0; i < 4; ++i)
            diffuserDelaySmp[i] = (3.0f + i * 2.0f) * 0.001f * fsf;

        // ★ CPU最適化: Allpassベースディレイを事前計算（16ch × 3段）
        constexpr float apfBaseMs[SERIAL_APF_STAGES]   = { 1.5f, 2.3f, 3.7f };
        constexpr float apfSpreadMs[SERIAL_APF_STAGES] = { 0.30f, 0.37f, 0.47f };
        constexpr float apfModFrac[SERIAL_APF_STAGES]  = { 0.15f, 0.10f, 0.07f };
        const float msToSmp = 0.001f * fsf;
        std::array<std::array<float, SERIAL_APF_STAGES>, FDN_ORDER> apfBaseDelaySmp;
        for (int i = 0; i < FDN_ORDER; ++i)
            for (int s = 0; s < SERIAL_APF_STAGES; ++s)
                apfBaseDelaySmp[i][s] = (apfBaseMs[s] + i * apfSpreadMs[s]) * msToSmp;

        // ★ CPU最適化: ER tapGain の * 0.5f を事前計算
        std::array<float, MAX_ER_TAPS> erTapGainsHalf;
        for (int t = 0; t < currentERTapCount; ++t)
            erTapGainsHalf[t] = currentERGains[t] * 0.5f;

        // ★ CPU最適化: soft-knee 閾値の二乗を事前計算（sqrt 回避）
        constexpr float compThresh = 0.35f;
        constexpr float compThreshSq = compThresh * compThresh;

        std::array<float, FDN_ORDER> lfoCoeffs;
        {
            constexpr float twoPi = 6.28318530718f;
            for (int i = 0; i < FDN_ORDER; ++i) {
                const float fc = activeParams.modRate * lfos[i].rateMultiplier;
                lfoCoeffs[i] = juce::jlimit(0.0001f, 0.9999f,
                    1.0f - std::exp(-twoPi * fc / fsf));
                // ★ コーラスLFOレート更新
                chorusLFOs[i].phaseInc = activeParams.modRate * chorusLFOs[i].rateScale / fsf;
            }
        }

        for (int n = 0; n < numSamples; ++n) {
            const float leftIn = inL[n];
            const float rightIn = inR[n];
            const float midIn = (leftIn + rightIn) * 0.5f;
            const float sideIn = (leftIn - rightIn) * 0.5f;
            float erOutL = 0.0f, erOutR = 0.0f;

            // ★ PreDelay: 原音とリバーブの時間的分離
            //   ER・FDN 両方の入力をプリディレイで遅延させる。
            //   これにより原音のアタック直後にリバーブが始まらず、
            //   ミックスの明瞭度 (D50/C50) が大幅に向上する。
            preDelayLine.write(midIn);
            const float delayedMid = (preDelaySamples > 0.5f)
                ? preDelayLine.read(preDelaySamples)
                : midIn;

            const float inputPeak = juce::jmax(std::abs(leftIn), std::abs(rightIn));
            const float envCoeff = (inputPeak > duckingEnvelope)
                ? duckingAttackCoeff : duckingReleaseCoeff;
            duckingEnvelope += (inputPeak - duckingEnvelope) * envCoeff;

            float duckGainLinear = 1.0f;
            if (duckAmountDB > 0.001f && duckingEnvelope > duckThreshLin) {
                const float envDB = 20.0f * std::log10(juce::jmax(duckingEnvelope, 1e-6f));
                const float overDB = envDB - activeParams.duckingThreshDB;
                const float gainRedDB = -juce::jmin(overDB, duckAmountDB);
                duckGainLinear = juce::Decibels::decibelsToGain(gainRedDB);
            }

            float fdnInputMid = delayedMid;
            if (!bypassInputDiffusers) {
                for (int i = 0; i < 4; ++i) {
                    float d = inputDiffusers[i].read(diffuserDelaySmp[i]);
                    float w = fdnInputMid + diffuserGain * d;
                    inputDiffusers[i].write(w);
                    fdnInputMid = d - diffuserGain * w;
                }
            }

            if (!bypassER) {
                erDelay.write(delayedMid);
                float erTotalL = 0.0f, erTotalR = 0.0f;
                for (int t = 0; t < currentERTapCount; ++t) {
                    const float tapValue = erDelay.read(currentERDelaySamples[t]);
                    const float tapGain = erTapGainsHalf[t];
                    const float tg = tapValue * tapGain;
                    const float tgLeak = tg * erLeakage;
                    if (t % 2 == 0) {
                        erTotalL += tg;
                        erTotalR += tgLeak;
                    }
                    else {
                        erTotalR += tg;
                        erTotalL += tgLeak;
                    }
                }
                erOutL = erTotalL;
                erOutR = erTotalR;
            }

            // ★ ER→Late遷移スムージング: ER出力をFDN入力にフィード
            //   実空間では初期反射が壁面で反射を繰り返しLate Reverbを生成する。
            //   この自然な遷移を模擬し、ERとLateの境界を滑らかにする。
            if (!bypassER) {
                fdnInputMid += (erOutL + erOutR) * 0.5f * 0.15f;
            }

            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float fdnOutL = 0.0f, fdnOutR = 0.0f;
            std::array<float, 16> nextFb;

            for (int i = 0; i < FDN_ORDER; ++i) {
                const float lfoVal = lfos[i].tick(lfoCoeffs[i]);
                // ★ コーラス型ピッチモジュレーション: 正弦波LFOをノイズLFOに加算
                //   ノイズ = ランダムな揺らぎ（金属音抑制）
                //   コーラス = 滑らかなピッチシフト蓄積（リッチなテール）
                const float chorusVal = chorusLFOs[i].tick();
                const float combinedLfo = lfoVal + chorusVal * 0.6f;

                // ★ 周波数依存モジュレーション: 高域チャンネルを深く、低域を浅く
                const float freqModScale = freqModScales[i];
                const float delaySmp = fdnBaseDelaySamples[i]
                    + combinedLfo * depthSamples * freqModScale;
                float d = fdnDelays[i].read(delaySmp);

#if AMBIENCE_USE_STAGE2_ABSORPTION
                for (int s = 0; s < ABSO_STAGES_S2; ++s)
                    d = absorptionFiltersS2[i][s].tick(d, currentAbsorptionCoeffsS2[i][s]);
#else
                d = absorptionFilters[i].tick(d, currentAbsorptionCoeffs[i]);
#endif

                // ★ 金属音対策 (3): DCブロッカー (1次HPF, fc≈5Hz)
                //   FDNループ内で吸収フィルタやマイクロサチュレーションが生成する
                //   DC成分の蓄積を防止。蓄積DCは低域のうなりや非対称歪みの原因になる。
                {
                    const float dcIn = d;
                    const float dcOut = dcIn - dcX1[i] + dcBlockerCoeff * dcY1[i];
                    dcX1[i] = dcIn;
                    dcY1[i] = dcOut;
                    d = dcOut;
                }

                // ★ Soft-kneeコンプレッション (FDNフィードバックループ)
                //   RMSエンベロープでレベルを追従し、閾値超過分をソフトに圧縮。
                //   ★ CPU最適化: sqrt を閾値超過時のみ実行（二乗比較でゲート）
                {
                    fdnRmsEnv[i] += (d * d - fdnRmsEnv[i]) * rmsCoeff;
                    if (fdnRmsEnv[i] > compThreshSq) {
                        const float env = std::sqrt(fdnRmsEnv[i]);
                        const float over = env - compThresh;
                        d *= compThresh / (compThresh + over * 0.65f);
                    }
                }

                // ★ 金属音対策 (1): Decay依存マイクロサチュレーション
                //   microSatBlend=1.0 → 従来通り適用 (短残響)
                //   microSatBlend=0.0 → 完全バイパス (長残響)
                if (microSatBlend > 0.001f) {
                    const float sat = processMicroSaturation(d);
                    d = d + (sat - d) * microSatBlend;
                }

                // ★ 3段シリアルAllpassチェーン (レイトフィールド密度改善)
                //   ★ CPU最適化: ベースディレイ・apfGainStage を事前計算済み
                float apfOut = d;
                {
                    for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                        const float apfModDepth = depthSamples * apfModFrac[s];
                        const float apfDelaySmp = apfBaseDelaySmp[i][s]
                            + combinedLfo * apfModDepth * freqModScale;
                        float apfD = nestedAllpassDelays[i][s].read(apfDelaySmp);
                        float apfW = apfOut + apfGainStage * apfD;
                        nestedAllpassDelays[i][s].write(apfW);
                        apfOut = apfD - apfGainStage * apfW;
                    }
                }

                nextFb[i] = apfOut;

                const float sideForCh = (i % 2 == 0 ? +sideIn : -sideIn) * sideBoost;
                const float fdnInputForThisCh = (fdnInputMid + sideForCh) * 0.25f;
                fdnDelays[i].write(fdnInputForThisCh + currentFb[i]);

                const float crossLeak = 1.0f - stereoWidth;
                if (i % 2 == 0) {
                    fdnOutL += apfOut;
                    fdnOutR += apfOut * crossLeak;
                }
                else {
                    fdnOutR += apfOut;
                    fdnOutL += apfOut * crossLeak;
                }
            }

            fdnOutL *= 0.125f;
            fdnOutR *= 0.125f;
            fbVec = nextFb;

            const float erMixL = bypassER ? 0.0f : erOutL * erLevel;
            const float erMixR = bypassER ? 0.0f : erOutR * erLevel;
            const float lateMixL = fdnOutL * lateMakeupGainLinear * lateLevel;
            const float lateMixR = fdnOutR * lateMakeupGainLinear * lateLevel;

            acousticMetrics.processSample((lateMixL + lateMixR) * 0.5f);

            float satL = saturatorL.processSample(lateMixL);
            float satR = saturatorR.processSample(lateMixR);

            if (erSolo) { satL = 0.0f; satR = 0.0f; }

            float wetL = erMixL + satL;
            float wetR = erMixR + satR;
            outputEQ.process(wetL, wetR);

            const float finalWetGain = wetGain * duckGainLinear;
            outL[n] = wetL * finalWetGain;
            outR[n] = wetR * finalWetGain;

            outputLimiter.process(outL[n], outR[n]);
        }
    }

} // namespace FDNReverb