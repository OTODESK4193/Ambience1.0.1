#include "UniversalEngine.h"

namespace FDNReverb {

    UniversalEngine::UniversalEngine() {
        fbVec.fill(0.0f);
        // 各LFOのシードをばらけさせて非相関化（Decorrelation）する
        for (int i = 0; i < FDN_ORDER; ++i) lfos[i].state = 12345 + i * 9876;
    }

    void UniversalEngine::prepare(double sampleRate, int /*maxBlockSize*/) {
        fs = sampleRate;

        // 2のべき乗を計算するラムダ式
        auto getPow2 = [](size_t s) -> size_t {
            size_t p = 1;
            while (p < s) p *= 2;
            return p;
            };

        // 各ディレイラインが「実際に消費する（2のべき乗に切り上げられた）サイズ」を厳密に合計する
        size_t totalMemoryNeeded =
            getPow2(static_cast<size_t>(fs * 1.0))
            + getPow2(static_cast<size_t>(fs * 0.05)) * 4
            + getPow2(static_cast<size_t>(fs * 0.5)) * FDN_ORDER
            + getPow2(static_cast<size_t>(fs * 0.1)) * FDN_ORDER;

        // 厳密な合計メモリ量を確保
        memoryPool.allocate(totalMemoryNeeded);

        int mask = 0;
        float* ptr = nullptr;

        // ERディレイ (最大1秒)
        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 1.0), mask);
        erDelay.init(ptr, mask);

        // Input Diffusers (最大50ms × 4)
        for (int i = 0; i < 4; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            inputDiffusers[i].init(ptr, mask);
        }

        // FDN Lines (最大0.5秒 × 16)
        for (int i = 0; i < FDN_ORDER; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
            fdnDelays[i].init(ptr, mask);

            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.1), mask);
            nestedAllpassDelays[i].init(ptr, mask);
        }

        reset();
    }

    void UniversalEngine::reset() {
        memoryPool.clear();
        fbVec.fill(0.0f);
        for (auto& f : absorptionFilters) f.reset();
    }

    void UniversalEngine::setParams(const DSPParams& p) {
        activeParams = p;
        // 選択されたアルゴリズム(0~6)をTopologyにマッピング
        switch (p.algorithmIndex) {
        case 0: case 1: currentTopology = ReverbTopology::Room; break;
        case 2: case 3: currentTopology = ReverbTopology::Hall; break;
        case 4:         currentTopology = ReverbTopology::Plate; break;
        case 5:         currentTopology = ReverbTopology::Spring; break;
        case 6:         currentTopology = ReverbTopology::Goldfoil; break;
        }
        updateTopologyAndRouting();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // 素数べき乗 (Prime Power) アルゴリズムによる遅延時間の算定
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::calculatePrimePowerDelays() {
        static constexpr std::array<int, FDN_ORDER> primes = {
            2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53
        };

        // Room Sizeパラメータ(0.5~2.5)を用いてベース遅延(ms)を計算
        float baseSizeMs = 30.0f * (0.5f + activeParams.roomSizeScale);

        for (int i = 0; i < FDN_ORDER; ++i) {
            float targetSamples = (baseSizeMs + i * 5.0f) * 0.001f * static_cast<float>(fs);

            // 素数べき乗の多重度 m_i を計算: m_i = round( log(target) / log(p_i) )
            float m_i = std::round(std::log(targetSamples) / std::log(static_cast<float>(primes[i])));

            // 最終的な遅延サンプル数: p_i ^ m_i
            fdnBaseDelaySamples[i] = std::pow(static_cast<float>(primes[i]), m_i);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // 動的トポロジー構成（アルゴリズムごとの結線切り替え）
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::updateTopologyAndRouting() {
        calculatePrimePowerDelays();

        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];
        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;
        effectiveRT60 = scaledRT60;

        for (int i = 0; i < FDN_ORDER; ++i) {
            currentAbsorptionCoeffs[i] = FilterDesign::designAbsorption(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption)[0];
        }

        // ─── 動的 Auto Gain Compensation (AGC) の計算 ───

        // 1. 基準となるRT60（500Hz帯域）を取得
        float rt60Mid = std::max(0.1f, scaledRT60[4]);

        // 2. 減衰時間に対する動的ゲイン補償カーブ（Decay Compensation）
        // エネルギーが時間方向に引き伸ばされることによるRMS低下を補正します。
        // RT60が倍になれば +3dB（エネルギー半減を補償）する対数カーブ。基準を1.0秒とします。
        float decayCompDB = 10.0f * std::log10(rt60Mid / 1.0f);

        // 3. トポロジー（アルゴリズム）ごとの固定オフセット
        float topologyOffsetDB = 0.0f;

        switch (currentTopology) {
        case ReverbTopology::Room:
            bypassER = false; bypassInputDiffusers = true;
            apfGain = 0.3f;
            topologyOffsetDB = 0.0f; // トランジェントが残るため補償なし（基準）
            break;
        case ReverbTopology::Hall:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.618f;
            topologyOffsetDB = 1.5f; // 少しSmearingされる分を微補償
            break;
        case ReverbTopology::Plate:
            bypassER = true; bypassInputDiffusers = false;
            apfGain = 0.7f;
            topologyOffsetDB = 4.5f; // ERがなく即座に拡散するため大幅に補償
            break;
        case ReverbTopology::Spring:
            bypassER = true; bypassInputDiffusers = false;
            apfGain = 0.5f;
            topologyOffsetDB = 5.0f; // 分散特性が強いため大幅補償
            break;
        case ReverbTopology::Goldfoil:
            bypassER = true; bypassInputDiffusers = false;
            apfGain = 0.75f;
            topologyOffsetDB = 6.0f; // 完全に自己増殖ネットワークに食われるため最大補償
            break;
        }

        // 4. 最終的なメイクアップゲインの適用
        // Baseの 24.0dB は、16次マトリクスとユーザーが検証した「固定のエネルギー損失」を相殺するキャリブレーション値
        float totalLateMakeupDB = 24.0f + decayCompDB + topologyOffsetDB;
        lateMakeupGainLinear = juce::Decibels::decibelsToGain(totalLateMakeupDB);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // FWHT (O(N log N) の無損失マトリクス) & Sign Flipping
    // ─────────────────────────────────────────────────────────────────────────────
    inline void UniversalEngine::fastWalshHadamardTransform(std::array<float, 16>& v) noexcept {
        // 4段階のバタフライ演算 (log2(16) = 4)
        for (int h = 1; h < 16; h *= 2) {
            for (int i = 0; i < 16; i += h * 2) {
                for (int j = i; j < i + h; ++j) {
                    float x = v[j];
                    float y = v[j + h];
                    v[j] = x + y;
                    v[j + h] = x - y;
                }
            }
        }
        // スケーリング 1/sqrt(16) = 0.25
        for (int i = 0; i < 16; ++i) v[i] *= 0.25f;
    }

    inline void UniversalEngine::applySignFlipping(std::array<float, 16>& v) noexcept {
        static constexpr std::array<float, 16> flip = {
             1.f, -1.f,  1.f, -1.f, -1.f,  1.f, -1.f,  1.f,
             1.f,  1.f, -1.f, -1.f, -1.f, -1.f,  1.f,  1.f
        };
        for (int i = 0; i < 16; ++i) v[i] *= flip[i];
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // メインDSP処理ループ
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::processBlock(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept {

        // LFOのDepth計算 (1.5セント未満のMicro-delay = 数ミリ秒)
        float depthSamples = activeParams.modAmount * 0.002f * static_cast<float>(fs);
        float wetGain = juce::Decibels::decibelsToGain(activeParams.wetDB);

        for (int n = 0; n < numSamples; ++n) {
            float inputMono = (inL[n] + inR[n]) * 0.5f;
            float erOut = 0.0f;
            float fdnInput = inputMono;

            // 1. Input Diffusers (Plate等での初期拡散)
            if (!bypassInputDiffusers) {
                for (int i = 0; i < 4; ++i) {
                    // 簡単な直列オールパス
                    float delaySmp = (3.0f + i * 2.0f) * 0.001f * fs;
                    float d = inputDiffusers[i].read(delaySmp);
                    float w = fdnInput + 0.618f * d;
                    inputDiffusers[i].write(w);
                    fdnInput = d - 0.618f * w;
                }
            }

            // 2. ER Tapped Delay (Room/Hall等)
            if (!bypassER) {
                erDelay.write(inputMono);
                // 擬似的に4つのタップから読み出し
                erOut += erDelay.read(15.0f * 0.001f * fs) * 0.5f;
                erOut += erDelay.read(27.0f * 0.001f * fs) * 0.4f;
                erOut += erDelay.read(41.0f * 0.001f * fs) * 0.3f;
                erOut += erDelay.read(59.0f * 0.001f * fs) * 0.2f;
            }

            // 3. FDN + Nested Allpass Loop
            std::array<float, 16> currentFb = fbVec; // 前回のマトリクス出力
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float fdnOutL = 0.0f, fdnOutR = 0.0f;
            std::array<float, 16> nextFb;

            for (int i = 0; i < FDN_ORDER; ++i) {
                // LFOモジュレーション (Random Walk)
                float lfoVal = lfos[i].tick(activeParams.modRate, fs);
                float delaySmp = fdnBaseDelaySamples[i] + lfoVal * depthSamples;

                // FDNリード
                float d = fdnDelays[i].read(delaySmp);

                // 吸収フィルタ (Biquad)
                d = absorptionFilters[i].tick(d, currentAbsorptionCoeffs[i]);

                // Nested Allpass Filter
                // FDNループ内にAllpassをネストすることで指数関数的なSmearingを生成
                float apfDelaySmp = (1.5f + i * 0.3f) * 0.001f * fs;
                float apfD = nestedAllpassDelays[i].read(apfDelaySmp);
                float apfW = d + apfGain * apfD;
                nestedAllpassDelays[i].write(apfW);
                float apfOut = apfD - apfGain * apfW;

                // 次のフィードバックへ
                nextFb[i] = apfOut;

                // FDNへの書き込み (入力 + フィードバック)
                // 入力ベクトル bIn = 1/sqrt(16) = 0.25
                fdnDelays[i].write(fdnInput * 0.25f + currentFb[i]);

                // 出力ミックス (cOut = 0.25)
                float width = activeParams.stereoWidth;
                if (i % 2 == 0) { fdnOutL += apfOut * 0.25f; fdnOutR += apfOut * 0.25f * (1.f - width); }
                else { fdnOutR += apfOut * 0.25f; fdnOutL += apfOut * 0.25f * (1.f - width); }
            }

            fbVec = nextFb; // 状態更新

            // 4. 最終出力 (ER + Late)
            // ERはマトリクスを通っていないため、メイクアップゲインの対象外
            float erMix = bypassER ? 0.0f : erOut * activeParams.erLevel;

            // Late（FDN出力）に動的メイクアップゲインと、ユーザーのLateLevelパラメータを掛ける
            float lateMixL = fdnOutL * lateMakeupGainLinear * activeParams.lateLevel;
            float lateMixR = fdnOutR * lateMakeupGainLinear * activeParams.lateLevel;

            outL[n] = (erMix + lateMixL) * wetGain;
            outR[n] = (erMix + lateMixR) * wetGain;
        }
    }

} // namespace FDNReverb