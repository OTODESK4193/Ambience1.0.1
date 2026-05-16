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
            getPow2(static_cast<size_t>(fs * 1.0))
            + getPow2(static_cast<size_t>(fs * 0.05)) * 4
            + getPow2(static_cast<size_t>(fs * 0.5)) * FDN_ORDER
            + getPow2(static_cast<size_t>(fs * 0.1)) * FDN_ORDER;

        memoryPool.allocate(totalMemoryNeeded);

        int mask = 0;
        float* ptr = nullptr;

        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 1.0), mask);
        erDelay.init(ptr, mask);

        for (int i = 0; i < 4; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            inputDiffusers[i].init(ptr, mask);
        }

        for (int i = 0; i < FDN_ORDER; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
            fdnDelays[i].init(ptr, mask);
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.1), mask);
            nestedAllpassDelays[i].init(ptr, mask);
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
        const float depthSamples = activeParams.modAmount * 0.002f * static_cast<float>(fs);
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

        std::array<float, FDN_ORDER> lfoCoeffs;
        {
            const float fsf = static_cast<float>(fs);
            constexpr float twoPi = 6.28318530718f;
            for (int i = 0; i < FDN_ORDER; ++i) {
                const float fc = activeParams.modRate * lfos[i].rateMultiplier;
                lfoCoeffs[i] = juce::jlimit(0.0001f, 0.9999f,
                    1.0f - std::exp(-twoPi * fc / fsf));
            }
        }

        for (int n = 0; n < numSamples; ++n) {
            const float leftIn = inL[n];
            const float rightIn = inR[n];
            const float midIn = (leftIn + rightIn) * 0.5f;
            const float sideIn = (leftIn - rightIn) * 0.5f;
            float erOutL = 0.0f, erOutR = 0.0f;

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

            float fdnInputMid = midIn;
            if (!bypassInputDiffusers) {
                for (int i = 0; i < 4; ++i) {
                    float delaySmp = (3.0f + i * 2.0f) * 0.001f * static_cast<float>(fs);
                    float d = inputDiffusers[i].read(delaySmp);
                    float w = fdnInputMid + diffuserGain * d;
                    inputDiffusers[i].write(w);
                    fdnInputMid = d - diffuserGain * w;
                }
            }

            if (!bypassER) {
                erDelay.write(midIn);
                float erTotalL = 0.0f, erTotalR = 0.0f;
                for (int t = 0; t < currentERTapCount; ++t) {
                    float tapValue = erDelay.read(currentERDelaySamples[t]);
                    float tapGain = currentERGains[t] * 0.5f;
                    if (t % 2 == 0) {
                        erTotalL += tapValue * tapGain;
                        erTotalR += tapValue * tapGain * erLeakage;
                    }
                    else {
                        erTotalR += tapValue * tapGain;
                        erTotalL += tapValue * tapGain * erLeakage;
                    }
                }
                erOutL = erTotalL;
                erOutR = erTotalR;
            }

            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float fdnOutL = 0.0f, fdnOutR = 0.0f;
            std::array<float, 16> nextFb;

            for (int i = 0; i < FDN_ORDER; ++i) {
                const float lfoVal = lfos[i].tick(lfoCoeffs[i]);
                const float delaySmp = fdnBaseDelaySamples[i] + lfoVal * depthSamples;
                float d = fdnDelays[i].read(delaySmp);

#if AMBIENCE_USE_STAGE2_ABSORPTION
                for (int s = 0; s < ABSO_STAGES_S2; ++s)
                    d = absorptionFiltersS2[i][s].tick(d, currentAbsorptionCoeffsS2[i][s]);
#else
                d = absorptionFilters[i].tick(d, currentAbsorptionCoeffs[i]);
#endif

                d = processMicroSaturation(d);

                const float apfDelaySmp = (1.5f + i * 0.3f) * 0.001f * static_cast<float>(fs);
                float apfD = nestedAllpassDelays[i].read(apfDelaySmp);
                float apfW = d + effectiveApfGain * apfD;
                nestedAllpassDelays[i].write(apfW);
                float apfOut = apfD - effectiveApfGain * apfW;

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