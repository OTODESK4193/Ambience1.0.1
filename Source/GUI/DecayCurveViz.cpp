#include "DecayCurveViz.h"

DecayCurveViz::DecayCurveViz() {
    cachedERDelayMs.fill(0.0f);
    cachedERGains.fill(0.0f);
    startTimerHz(15);
}

DecayCurveViz::~DecayCurveViz() {
    stopTimer();
}

void DecayCurveViz::timerCallback() {
    if (processor == nullptr) return;
    const auto& engine = processor->getEngine();

    auto rt60 = engine.getEffectiveRT60();
    cachedRT60Mid = std::max(0.1f, rt60[4]);

    cachedERBypassed = engine.isERBypassed();
    cachedERTapCount = engine.getERTapCount();
    if (cachedERTapCount > MAX_DISPLAY_TAPS)
        cachedERTapCount = MAX_DISPLAY_TAPS;

    double sr = engine.getSampleRate();
    if (sr < 1.0) sr = 48000.0;

    for (int i = 0; i < cachedERTapCount; ++i) {
        float delaySamples = engine.getERTapDelaySamples(i);
        cachedERDelayMs[i] = delaySamples / static_cast<float>(sr) * 1000.0f;
        cachedERGains[i] = engine.getERTapGain(i);
    }
    repaint();
}

void DecayCurveViz::resized() {}

void DecayCurveViz::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.getWidth() < 10.0f || bounds.getHeight() < 10.0f) return;

    g.fillAll(AmbienceColors::Background);

    const float topMargin = 10.0f;
    const float bottomMargin = 18.0f;
    const float leftMargin = 30.0f;
    const float rightMargin = 8.0f;
    const float plotX = bounds.getX() + leftMargin;
    const float plotY = bounds.getY() + topMargin;
    const float plotW = bounds.getWidth() - leftMargin - rightMargin;
    const float plotH = bounds.getHeight() - topMargin - bottomMargin;

    const float maxTimeSec = juce::jlimit(0.5f, 8.0f, cachedRT60Mid * 1.5f);
    const float minDB = -60.0f;
    const float maxDB = 0.0f;

    // ─────────────────────────────────────────────────────────────────────────
    //  スプリット時間軸
    //   0〜splitSec   → 全幅の splitRatio 割合に拡大 (ER ゾーン)
    //   splitSec〜max → 残りの幅 (Late ゾーン)
    // ─────────────────────────────────────────────────────────────────────────
    constexpr float splitSec = 0.20f;  // 200ms まで拡大
    constexpr float splitRatio = 0.30f;  // 全幅の 30% を ER ゾーンに割り当て

    auto timeToX = [&](float timeSec) -> float {
        if (timeSec <= splitSec) {
            const float ratio = timeSec / splitSec;
            return plotX + ratio * plotW * splitRatio;
        }
        else {
            const float lateRange = maxTimeSec - splitSec;
            if (lateRange <= 0.0f) return plotX + plotW;
            const float ratio = (timeSec - splitSec) / lateRange;
            return plotX + plotW * splitRatio + ratio * plotW * (1.0f - splitRatio);
        }
        };

    auto dbToY = [&](float db) -> float {
        const float normalized = (db - minDB) / (maxDB - minDB);
        return plotY + (1.0f - normalized) * plotH;
        };

    // ─── ER ゾーン背景 ───
    {
        const float erZoneW = plotW * splitRatio;
        g.setColour(juce::Colour(0xFF1A2535));
        g.fillRect(plotX, plotY, erZoneW, plotH);
    }

    // ─── グリッド: 水平線 (dB) ───
    g.setColour(AmbienceColors::Separator.withAlpha(0.3f));
    for (float db = 0.0f; db >= -60.0f; db -= 20.0f)
        g.drawHorizontalLine((int)dbToY(db), plotX, plotX + plotW);

    // ─── グリッド: ER ゾーン垂直線 (ms) ───
    {
        static const float erGridMs[] = { 20.0f, 50.0f, 100.0f, 150.0f, 200.0f };
        g.setColour(AmbienceColors::Separator.withAlpha(0.5f));
        for (float ms : erGridMs) {
            const float t = ms * 0.001f;
            if (t >= maxTimeSec) break;
            g.drawVerticalLine((int)timeToX(t), plotY, plotY + plotH);
        }
    }

    // ─── グリッド: Late ゾーン垂直線 (s) ───
    float timeStep;
    if (maxTimeSec <= 2.0f) timeStep = 0.5f;
    else if (maxTimeSec <= 4.0f) timeStep = 1.0f;
    else                         timeStep = 2.0f;

    g.setColour(AmbienceColors::Separator.withAlpha(0.3f));
    for (float t = splitSec + timeStep; t <= maxTimeSec; t += timeStep)
        g.drawVerticalLine((int)timeToX(t), plotY, plotY + plotH);

    // ─── スプリット境界線 ───
    {
        const float splitX = timeToX(splitSec);
        g.setColour(AmbienceColors::Separator.withAlpha(0.9f));
        g.drawVerticalLine((int)splitX, plotY, plotY + plotH);
    }

    // ─── 軸ラベル ───
    g.setFont(juce::Font(juce::FontOptions(8.5f)));
    g.setColour(AmbienceColors::TextSecondary.withAlpha(0.6f));

    for (float db = 0.0f; db >= -60.0f; db -= 20.0f) {
        const float y = dbToY(db);
        g.drawText(juce::String((int)db) + "dB",
            (int)(plotX - leftMargin + 2), (int)(y - 6),
            (int)(leftMargin - 4), 12,
            juce::Justification::centredRight);
    }

    // ER ゾーン時間ラベル (ms)
    {
        static const float erGridMs[] = { 20.0f, 50.0f, 100.0f, 150.0f, 200.0f };
        for (float ms : erGridMs) {
            const float t = ms * 0.001f;
            if (t >= maxTimeSec) break;
            const float x = timeToX(t);
            g.drawText(juce::String((int)ms) + "ms",
                (int)(x - 20), (int)(plotY + plotH + 2),
                40, 14, juce::Justification::centred);
        }
    }

    // Late ゾーン時間ラベル (s)
    for (float t = splitSec + timeStep; t <= maxTimeSec; t += timeStep) {
        const float x = timeToX(t);
        g.drawText(juce::String(t, 1) + "s",
            (int)(x - 20), (int)(plotY + plotH + 2),
            40, 14, juce::Justification::centred);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Late Reverb 減衰カーブ (オレンジ系 2D グラデーション)
    // ─────────────────────────────────────────────────────────────────────────
    {
        const int numPoints = 80;
        juce::Colour orangeColor = AmbienceColors::Accent;

        juce::Path latePath;
        latePath.startNewSubPath(plotX, dbToY(maxDB));
        for (int i = 0; i <= numPoints; ++i) {
            const float t = (i / static_cast<float>(numPoints)) * maxTimeSec;
            const float db = std::max(minDB, -60.0f * t / cachedRT60Mid);
            latePath.lineTo(timeToX(t), dbToY(db));
        }
        latePath.lineTo(timeToX(maxTimeSec), dbToY(minDB));
        latePath.lineTo(plotX, dbToY(minDB));
        latePath.closeSubPath();

        juce::ColourGradient lateGrad(
            orangeColor.withAlpha(0.55f), plotX, plotY,
            orangeColor.withAlpha(0.0f), plotX + plotW, plotY + plotH,
            false);
        g.setGradientFill(lateGrad);
        g.fillPath(latePath);

        juce::Path lateOutline;
        lateOutline.startNewSubPath(plotX, dbToY(maxDB));
        for (int i = 0; i <= numPoints; ++i) {
            const float t = (i / static_cast<float>(numPoints)) * maxTimeSec;
            const float db = std::max(minDB, -60.0f * t / cachedRT60Mid);
            lateOutline.lineTo(timeToX(t), dbToY(db));
        }
        g.setColour(orangeColor.withAlpha(0.75f));
        g.strokePath(lateOutline, juce::PathStrokeType(1.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  ER タップ描画 (★ 改善版)
    //    旧: 細い縦線 2px + 丸マーカー
    //    新: 幅広矩形 5px + ダイヤマーカー + エンベロープ塗りつぶし
    // ─────────────────────────────────────────────────────────────────────────
    if (!cachedERBypassed && cachedERTapCount > 0) {
        const juce::Colour blueColor = juce::Colour::fromRGB(80, 160, 230);

        // ── ER エンベロープ塗りつぶし領域 ──
        if (cachedERTapCount >= 2) {
            juce::Path erFill;
            bool started = false;
            float lastX = plotX;

            for (int t = 0; t < cachedERTapCount; ++t) {
                const float timeSec = cachedERDelayMs[t] * 0.001f;
                if (timeSec > maxTimeSec) continue;

                float gainDB = (cachedERGains[t] > 1e-6f)
                    ? juce::Decibels::gainToDecibels(cachedERGains[t]) : minDB;
                gainDB = juce::jlimit(minDB, maxDB, gainDB);

                const float x = timeToX(timeSec);
                const float y = dbToY(gainDB);

                if (!started) {
                    erFill.startNewSubPath(plotX, dbToY(minDB));
                    erFill.lineTo(x, y);
                    started = true;
                }
                else {
                    erFill.lineTo(x, y);
                }
                lastX = x;
            }

            if (started) {
                erFill.lineTo(lastX, dbToY(minDB));
                erFill.closeSubPath();

                juce::ColourGradient erAreaGrad(
                    blueColor.withAlpha(0.20f), plotX, plotY,
                    blueColor.withAlpha(0.03f), plotX + plotW * splitRatio, plotY + plotH,
                    false);
                g.setGradientFill(erAreaGrad);
                g.fillPath(erFill);
            }
        }

        // ── 各タップ: 幅広矩形 + ダイヤマーカー ──
        for (int t = 0; t < cachedERTapCount; ++t) {
            const float timeSec = cachedERDelayMs[t] * 0.001f;
            if (timeSec > maxTimeSec) continue;

            float gainDB = (cachedERGains[t] > 1e-6f)
                ? juce::Decibels::gainToDecibels(cachedERGains[t]) : minDB;
            gainDB = juce::jlimit(minDB, maxDB, gainDB);

            const float x = timeToX(timeSec);
            const float yTop = dbToY(gainDB);
            const float yBottom = dbToY(minDB);
            const float barW = 5.0f;

            juce::ColourGradient tapGrad(
                blueColor.withAlpha(0.90f), x, yTop,
                blueColor.withAlpha(0.10f), x, yBottom,
                false);
            g.setGradientFill(tapGrad);
            g.fillRect(x - barW * 0.5f, yTop, barW, yBottom - yTop);

            // ダイヤ形マーカー
            g.setColour(blueColor);
            juce::Path diamond;
            diamond.startNewSubPath(x, yTop - 5.0f);
            diamond.lineTo(x + 4.0f, yTop);
            diamond.lineTo(x, yTop + 3.0f);
            diamond.lineTo(x - 4.0f, yTop);
            diamond.closeSubPath();
            g.fillPath(diamond);
        }

        // ── ER エンベロープ輪郭線 ──
        if (cachedERTapCount >= 2) {
            juce::Path erOutline;
            bool started = false;

            for (int t = 0; t < cachedERTapCount; ++t) {
                const float timeSec = cachedERDelayMs[t] * 0.001f;
                if (timeSec > maxTimeSec) continue;

                float gainDB = (cachedERGains[t] > 1e-6f)
                    ? juce::Decibels::gainToDecibels(cachedERGains[t]) : minDB;
                gainDB = juce::jlimit(minDB, maxDB, gainDB);

                const float x = timeToX(timeSec);
                const float y = dbToY(gainDB);

                if (!started) { erOutline.startNewSubPath(x, y); started = true; }
                else            erOutline.lineTo(x, y);
            }

            g.setColour(blueColor.withAlpha(0.65f));
            g.strokePath(erOutline, juce::PathStrokeType(1.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // ─── ゾーンラベル ───
    g.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 8.0f, juce::Font::bold)));

    g.setColour(juce::Colour::fromRGB(80, 160, 230).withAlpha(0.9f));
    g.drawText("ER",
        (int)(plotX + 4), (int)(plotY + 2),
        30, 12, juce::Justification::centredLeft);

    {
        const float splitX = timeToX(splitSec);
        g.setColour(AmbienceColors::Accent.withAlpha(0.9f));
        g.drawText("LATE",
            (int)(splitX + 6), (int)(plotY + 2),
            40, 12, juce::Justification::centredLeft);
    }

    g.setFont(juce::Font(juce::FontOptions(7.5f)));
    g.setColour(AmbienceColors::TextSecondary.withAlpha(0.4f));
    g.drawText("0-200ms (x2)",
        (int)(plotX + 2), (int)(plotY + plotH - 14),
        (int)(plotW * splitRatio - 4), 12,
        juce::Justification::centredLeft);
}