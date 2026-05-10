#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    // 統合メモリプール (Single-Large Buffer)
    // ─────────────────────────────────────────────────────────────────────────────
    class DelayMemoryPool {
    public:
        void allocate(size_t totalSamples) {
            buffer.assign(totalSamples, 0.0f);
            allocOffset = 0;
        }

        // 要求されたサイズを「次の2のべき乗」に切り上げてポインタを返す（高速なマスク演算のため）
        float* requestMemory(size_t samplesNeeded, int& outMask) {
            size_t powerOfTwoSize = 1;
            while (powerOfTwoSize < samplesNeeded) powerOfTwoSize *= 2;

            if (allocOffset + powerOfTwoSize > buffer.size()) return nullptr;

            float* ptr = buffer.data() + allocOffset;
            outMask = static_cast<int>(powerOfTwoSize - 1);
            allocOffset += powerOfTwoSize;

            return ptr;
        }

        void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); }

    private:
        std::vector<float> buffer;
        size_t allocOffset{ 0 };
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // 高速リニア補間ディレイライン
    // ─────────────────────────────────────────────────────────────────────────────
    class LinearDelayLine {
    public:
        void init(float* memory, int bitmask) {
            buffer = memory;
            mask = bitmask;
            writeIndex = 0;
        }

        // 線形補間（高域の自然なAir Absorptionを生む）
        inline float read(float delayInSamples) const noexcept {
            int id = static_cast<int>(delayInSamples);
            float frac = delayInSamples - static_cast<float>(id);

            // 負の数に対するビット演算の未定義動作を完全に防ぐため、uint32_tでラップアラウンドさせる
            uint32_t uWrite = static_cast<uint32_t>(writeIndex);
            uint32_t uId = static_cast<uint32_t>(id);
            uint32_t uMask = static_cast<uint32_t>(mask);

            int readIdx1 = static_cast<int>((uWrite - uId) & uMask);
            int readIdx2 = static_cast<int>((uWrite - uId - 1) & uMask);

            return buffer[readIdx1] + frac * (buffer[readIdx2] - buffer[readIdx1]);
        }

        inline void write(float input) noexcept {
            buffer[writeIndex] = input;
            writeIndex = (writeIndex + 1) & mask;
        }

    private:
        float* buffer{ nullptr };
        int mask{ 0 };
        int writeIndex{ 0 };
    };

} // namespace FDNReverb