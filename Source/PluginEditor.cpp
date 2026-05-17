#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr int Y_HEADER = 8;
static constexpr int Y_ALGO = 48;
static constexpr int Y_SLABEL1 = 86;
static constexpr int Y_ROW1 = 104;
static constexpr int Y_SLABEL2 = 204;
static constexpr int Y_ROW2 = 222;
static constexpr int Y_SEP = 322;
static constexpr int Y_VIZ = 326;

static constexpr int SEC_TIME = 8;
static constexpr int SEC_FREQUENCY = 254;
static constexpr int SEC_DIFFUSION = 418;
static constexpr int SEC_STEREO = 664;
static constexpr int SEC_CHARACTER = 746;
static constexpr int SEP_TF = 245;
static constexpr int SEP_FD = 409;
static constexpr int SEP_DS = 655;
static constexpr int SEP_SC = 737;

// ─────────────────────────────────────────────────────────────────────────────
//  コンストラクタ
// ─────────────────────────────────────────────────────────────────────────────
FDNReverbEditor::FDNReverbEditor(FDNReverbAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    algoSelector(p.apvts),
    vuIn("IN", VUMeter::Side::Input),
    vuOut("OUT", VUMeter::Side::Output)
{
    setLookAndFeel(&laf);
    setSize(W, H);

    // ── Title ──
    titleLabel.setText("AMBIENCE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 14.f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, AmbienceColors::TextPrimary);
    addAndMakeVisible(titleLabel);

    addAndMakeVisible(algoSelector);

    auto BK = [&](ArcKnob& k, const char* id, const char* lbl) {
        k.build(p.apvts, id, lbl, this, laf);
        };

    BK(kPreDelay, "predelay", "PRE-DELAY");
    BK(kRoomSize, "roomsize", "ROOM SIZE");
    BK(kDecay, "decaytime", "DECAY");
    BK(kHFDamp, "hfdamping", "HF DAMP");
    BK(kLFAbsorb, "lfabsorption", "LF ABSORB");
    BK(kDiffusion, "diffusion", "DIFFUSION");
    BK(kModAmt, "modamount", "MOD AMT");
    BK(kModRate, "modrate", "MOD RATE");
    BK(kStereoW, "stereowidth", "WIDTH");
    BK(kERLevel, "erlevel", "ER LEVEL");
    BK(kSaturation, "saturation", "SATURATE");
    BK(kWet, "wetlevel", "WET");
    BK(kDry, "drylevel", "DRY");
    BK(kDuckAmt, "duckamount", "AMOUNT");
    BK(kDuckThr, "duckthresh", "THRESH");
    BK(kDuckAtt, "duckattack", "ATTACK");
    BK(kDuckRel, "duckrelease", "RELEASE");
    BK(kLoCutNorm, "locut", "LO CUT");
    BK(kHiCutNorm, "hicut", "HI CUT");

    // ── ProMode ボタン ──
    proModeButton.setButtonText("PRO");
    proModeButton.setClickingTogglesState(true);
    proModeButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    proModeButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    proModeButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    proModeButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    addAndMakeVisible(proModeButton);
    proModeAttachment.reset(
        new juce::AudioProcessorValueTreeState::ButtonAttachment(
            p.apvts, "promode", proModeButton));

    // ── ER SOLO ボタン ──
    erSoloButton.setButtonText("ER SOLO");
    erSoloButton.setClickingTogglesState(true);
    erSoloButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::AccentBlue);
    erSoloButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    erSoloButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    erSoloButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    addAndMakeVisible(erSoloButton);
    erSoloAttachment.reset(
        new juce::AudioProcessorValueTreeState::ButtonAttachment(
            p.apvts, "ersolo", erSoloButton));

    // ── ProMode: RT60 帯域ノブ ──
    static const char* rtBandIDs[] = {
        "rtband0","rtband1","rtband2","rtband3","rtband4",
        "rtband5","rtband6","rtband7","rtband8","rtband9"
    };
    static const char* rtBandLbls[] = {
        "31Hz","63Hz","125Hz","250Hz","500Hz",
        "1kHz","2kHz","4kHz","8kHz","16kHz"
    };
    for (int i = 0; i < 10; ++i)
        kRTBands[i].build(p.apvts, rtBandIDs[i], rtBandLbls[i], this, laf);

    // ── ProMode: SatType ──
    satTypeLabel.setText("SAT TYPE", juce::dontSendNotification);
    satTypeLabel.setFont(juce::Font(juce::FontOptions(9.f)));
    satTypeLabel.setColour(juce::Label::textColourId, AmbienceColors::TextSecondary);
    satTypeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(satTypeLabel);

    satTypeCombo.addItemList({ "Warm","Tape","Tube","Hard" }, 1);
    satTypeCombo.setLookAndFeel(&laf);
    addAndMakeVisible(satTypeCombo);
    satTypeAttachment.reset(
        new juce::AudioProcessorValueTreeState::ComboBoxAttachment(
            p.apvts, "sattype", satTypeCombo));

    // ── ProMode: Tilt EQ + Output EQ ──
    BK(kTiltLow, "tiltlow", "TILT LOW");
    BK(kTiltMid, "tiltmid", "TILT MID");
    BK(kTiltHigh, "tilthigh", "TILT HIGH");
    BK(kLoCutPro, "locut", "LO CUT");
    BK(kHiCutPro, "hicut", "HI CUT");

    // ─────────────────────────────────────────────────────────────────────────
    //  プリセット UI
    // ─────────────────────────────────────────────────────────────────────────
    presetManager = std::make_unique<PresetManager>(p);

    // ◀ PREV
    presetPrevButton.setButtonText("<");
    presetPrevButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    presetPrevButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextPrimary);
    addAndMakeVisible(presetPrevButton);
    presetPrevButton.onClick = [this] {
        presetManager->loadPrevPreset();
        };

    // プリセット名コンボ
    presetCombo.setLookAndFeel(&laf);
    addAndMakeVisible(presetCombo);
    presetCombo.onChange = [this] {
        int idx = presetCombo.getSelectedItemIndex();
        auto names = presetManager->getPresetNames();
        if (idx >= 0 && idx < names.size())
            presetManager->loadPreset(names[idx]);
        };

    // ▶ NEXT
    presetNextButton.setButtonText(">");
    presetNextButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    presetNextButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextPrimary);
    addAndMakeVisible(presetNextButton);
    presetNextButton.onClick = [this] {
        presetManager->loadNextPreset();
        };

    // SAVE
    presetSaveButton.setButtonText("SAVE");
    presetSaveButton.setColour(juce::TextButton::buttonColourId,
        AmbienceColors::Accent.withAlpha(0.75f));
    presetSaveButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::Background);
    addAndMakeVisible(presetSaveButton);
    presetSaveButton.onClick = [this] { savePresetWithDialog(); };

    // ─── 変更後 ───
        // LOAD
    presetLoadButton.setButtonText("LOAD");
    presetLoadButton.setColour(juce::TextButton::buttonColourId,
        AmbienceColors::AccentBlue.withAlpha(0.75f));
    presetLoadButton.setColour(juce::TextButton::textColourOffId,
        AmbienceColors::Background);
    addAndMakeVisible(presetLoadButton);
    presetLoadButton.onClick = [this] {
        auto names = presetManager->getPresetNames();
        int idx = presetCombo.getSelectedItemIndex();
        if (idx >= 0 && idx < names.size())
            presetManager->loadPreset(names[idx]);
        };

    // DELETE
    presetDeleteButton.setButtonText("DELETE");
    presetDeleteButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    presetDeleteButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    addAndMakeVisible(presetDeleteButton);
    presetDeleteButton.onClick = [this] { deleteCurrentPreset(); };

    // コールバック設定
// コンストラクタ内のコールバック設定部分
    presetManager->onPresetListChanged = [this] { refreshPresetCombo(); };

    // ★ 修正: プリセット名が変わるたびに Processor に通知する
    presetManager->onPresetLoaded = [this](const juce::String& name) {
        audioProcessor.setLastSavedPresetName(name);
        refreshPresetCombo();
        };
    // ── Visualizers ──
    rt60Viz.setProcessor(&p);
    decayCurveViz.setProcessor(&p);
    addAndMakeVisible(rt60Viz);
    addAndMakeVisible(decayCurveViz);

    addAndMakeVisible(vuIn);
    addAndMakeVisible(vuOut);

    // ── AcousticMetrics ──
    labelMetricsTitle.setText("ACOUSTICS", juce::dontSendNotification);
    labelMetricsTitle.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 8.5f, juce::Font::bold)));
    labelMetricsTitle.setColour(juce::Label::textColourId,
        AmbienceColors::Accent.withAlpha(0.75f));
    labelMetricsTitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(labelMetricsTitle);

    auto setupCaption = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(
            "Helvetica Neue", 8.0f, juce::Font::plain)));
        label.setColour(juce::Label::textColourId,
            AmbienceColors::TextSecondary.withAlpha(0.85f));
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
        };
    auto setupValue = [this](juce::Label& label) {
        label.setText("--", juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(
            "Helvetica Neue", 9.5f, juce::Font::bold)));
        label.setColour(juce::Label::textColourId, AmbienceColors::TextPrimary);
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
        };

    setupCaption(labelD50Caption, "D50:");
    setupCaption(labelC50Caption, "C50:");
    setupCaption(labelC80Caption, "C80:");
    setupCaption(labelEDTCaption, "EDT:");
    setupValue(labelD50Value);
    setupValue(labelC50Value);
    setupValue(labelC80Value);
    setupValue(labelEDTValue);

    // ─── 変更後 ───
    refreshPresetCombo();     // ★ 追加：起動時にコンボを初期化
    updatePanelVisibility();
    startTimerHz(60);
}

// ─────────────────────────────────────────────────────────────────────────────
//  デストラクタ
// ─────────────────────────────────────────────────────────────────────────────
FDNReverbEditor::~FDNReverbEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
    satTypeCombo.setLookAndFeel(nullptr);
    presetCombo.setLookAndFeel(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  timerCallback
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbEditor::timerCallback()
{
    vuIn.setLevels(audioProcessor.getInputRMSL(),
        audioProcessor.getInputRMSR());
    vuOut.setLevels(audioProcessor.getOutputRMSL(),
        audioProcessor.getOutputRMSR());
    vuIn.repaint();
    vuOut.repaint();

    static int metricsCounter = 0;
    if (++metricsCounter >= 2) {
        metricsCounter = 0;
        labelD50Value.setText(
            juce::String(audioProcessor.getD50() * 100.0f, 1) + "%",
            juce::dontSendNotification);
        labelC50Value.setText(
            juce::String(audioProcessor.getC50(), 1) + "dB",
            juce::dontSendNotification);
        labelC80Value.setText(
            juce::String(audioProcessor.getC80(), 1) + "dB",
            juce::dontSendNotification);
        labelEDTValue.setText(
            juce::String(audioProcessor.getEDT(), 2) + "s",
            juce::dontSendNotification);
    }

    bool newProMode = (*audioProcessor.apvts.getRawParameterValue("promode") > 0.5f);
    if (newProMode != isProMode) {
        isProMode = newProMode;
        updatePanelVisibility();
        resized();
        repaint();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  updatePanelVisibility
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbEditor::updatePanelVisibility()
{
    auto setKnob = [](ArcKnob& k, bool vis) {
        k.slider.setVisible(vis);
        k.label.setVisible(vis);
        };

    const bool showNormal = !isProMode;
    const bool showPro = isProMode;

    setKnob(kPreDelay, showNormal);
    setKnob(kRoomSize, showNormal);
    setKnob(kDecay, showNormal);
    setKnob(kHFDamp, showNormal);
    setKnob(kLFAbsorb, showNormal);
    setKnob(kDiffusion, showNormal);
    setKnob(kModAmt, showNormal);
    setKnob(kModRate, showNormal);
    setKnob(kStereoW, showNormal);
    setKnob(kERLevel, showNormal);
    setKnob(kSaturation, showNormal);
    setKnob(kWet, showNormal);
    setKnob(kDry, showNormal);
    setKnob(kDuckAmt, showNormal);
    setKnob(kDuckThr, showNormal);
    setKnob(kDuckAtt, showNormal);
    setKnob(kDuckRel, showNormal);
    setKnob(kLoCutNorm, showNormal);
    setKnob(kHiCutNorm, showNormal);

    for (auto& k : kRTBands) setKnob(k, showPro);
    satTypeLabel.setVisible(showPro);
    satTypeCombo.setVisible(showPro);
    setKnob(kTiltLow, showPro);
    setKnob(kTiltMid, showPro);
    setKnob(kTiltHigh, showPro);
    setKnob(kLoCutPro, showPro);
    setKnob(kHiCutPro, showPro);

    // プリセット UI は常時表示
    presetPrevButton.setVisible(true);
    presetCombo.setVisible(true);
    presetNextButton.setVisible(true);
    // ─── 変更後 ───
    presetSaveButton.setVisible(true);
    presetLoadButton.setVisible(true);    // ★ 追加
    presetDeleteButton.setVisible(true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  resized
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbEditor::resized()
{
    titleLabel.setBounds(PAD, Y_HEADER, 180, 32);
    proModeButton.setBounds(196, Y_HEADER + 5, 52, 22);
    erSoloButton.setBounds(256, Y_HEADER + 5, 72, 22);
    vuIn.setBounds(W - 220, Y_HEADER + 2, 96, 28);
    vuOut.setBounds(W - 120, Y_HEADER + 2, 96, 28);

    algoSelector.setBounds(PAD, Y_ALGO, W - PAD * 2, 30);

    auto place1 = [&](ArcKnob& k, int& x, int y) {
        k.label.setBounds(x, y, KNOB_W, KNOB_LBL_H);
        k.slider.setBounds(x, y + KNOB_LBL_H, KNOB_W, KNOB_H);
        x += KNOB_W + ROW1_GAP;
        };
    auto place2 = [&](ArcKnob& k, int& x, int y) {
        k.label.setBounds(x, y, KNOB_W, KNOB_LBL_H);
        k.slider.setBounds(x, y + KNOB_LBL_H, KNOB_W, KNOB_H);
        x += KNOB_W + PAD;
        };

    if (!isProMode) {
        // ── Row 1 ──
        int kx = PAD;
        place1(kPreDelay, kx, Y_ROW1);
        place1(kRoomSize, kx, Y_ROW1);
        place1(kDecay, kx, Y_ROW1);
        place1(kHFDamp, kx, Y_ROW1);
        place1(kLFAbsorb, kx, Y_ROW1);
        place1(kDiffusion, kx, Y_ROW1);
        place1(kModAmt, kx, Y_ROW1);
        place1(kModRate, kx, Y_ROW1);
        place1(kStereoW, kx, Y_ROW1);
        place1(kERLevel, kx, Y_ROW1);
        place1(kSaturation, kx, Y_ROW1);

        // ── Row 2: MIX | OUT EQ | DUCKING ──
        kx = PAD;
        place2(kWet, kx, Y_ROW2);
        place2(kDry, kx, Y_ROW2);
        kx += 16;
        place2(kLoCutNorm, kx, Y_ROW2);
        place2(kHiCutNorm, kx, Y_ROW2);
        kx += 16;
        place2(kDuckAmt, kx, Y_ROW2);
        place2(kDuckThr, kx, Y_ROW2);
        place2(kDuckAtt, kx, Y_ROW2);
        place2(kDuckRel, kx, Y_ROW2);

    }
    else {
        // ── ProMode 1段目 ──
        int kx = PAD;
        for (int i = 0; i < 10; ++i)
            place1(kRTBands[i], kx, Y_ROW1);

        // ── ProMode 2段目 ──
        int kx2 = PAD;
        satTypeLabel.setBounds(kx2, Y_SLABEL2, KNOB_W, KNOB_LBL_H);
        satTypeCombo.setBounds(kx2, Y_SLABEL2 + KNOB_LBL_H + 2, KNOB_W + PAD, 24);
        kx2 += KNOB_W + PAD + PAD + 8;
        place2(kTiltLow, kx2, Y_ROW2);
        place2(kTiltMid, kx2, Y_ROW2);
        place2(kTiltHigh, kx2, Y_ROW2);
        kx2 += 16;
        place2(kLoCutPro, kx2, Y_ROW2);
        place2(kHiCutPro, kx2, Y_ROW2);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  プリセット UI (常時・モード非依存)
    // ─────────────────────────────────────────────────────────────────────────
    //  配置 (PRESET_PANEL_X=632 起点):
    //
    //  上段 Y=Y_ROW2:   [◀(26)] gap4 [combo(154)] gap4 [▶(26)]  → 右端 848
    //  下段 Y=Y_ROW2+34:[SAVE(104)] gap8 [DELETE(104)]           → 右端 848
    //
    //  セパレーター縦線: PRESET_PANEL_X - 9 = 623
    // ─────────────────────────────────────────────────────────────────────────
    {
        const int px = PRESET_PANEL_X;
        const int btnH = 26;

        // 上段
        presetPrevButton.setBounds(px, Y_ROW2, 26, btnH);
        presetCombo.setBounds(px + 30, Y_ROW2, 154, btnH);
        presetNextButton.setBounds(px + 188, Y_ROW2, 26, btnH);

        // ─── 変更後 ───
               // 下段: [SAVE(68)] [LOAD(68)] [DELETE(68)]
        presetSaveButton.setBounds(px, Y_ROW2 + 34, 68, btnH);
        presetLoadButton.setBounds(px + 72, Y_ROW2 + 34, 68, btnH);
        presetDeleteButton.setBounds(px + 144, Y_ROW2 + 34, 68, btnH);
    }

    // ── Visualizers ──
    const int vizTotalH = H - Y_VIZ - PAD;
    const int rt60Height = vizTotalH / 2 - 2;
    const int decayHeight = vizTotalH / 2 - 2;
    const int decayY = Y_VIZ + rt60Height + 4;

    rt60Viz.setBounds(PAD, Y_VIZ, W - PAD * 2, rt60Height);
    decayCurveViz.setBounds(PAD, decayY, W - PAD * 2, decayHeight);

    // ── AcousticMetrics ──
    const int metricsRight = W - PAD - 8;
    const int metricsW = 280;
    const int metricsLeft = metricsRight - metricsW;
    const int metricsTop = decayY + 6;
    const int metricsRowH = 14;
    const int metricsRow1Y = metricsTop + 14;
    const int metricsRow2Y = metricsRow1Y + metricsRowH;
    const int captionW = 28;
    const int valueW = 52;
    const int colSpacing = 8;
    const int colW = captionW + valueW;

    labelMetricsTitle.setBounds(metricsLeft, metricsTop, 80, 12);
    labelD50Caption.setBounds(metricsLeft, metricsRow1Y, captionW, metricsRowH);
    labelD50Value.setBounds(metricsLeft + captionW, metricsRow1Y, valueW, metricsRowH);
    labelC50Caption.setBounds(metricsLeft + colW + colSpacing, metricsRow1Y, captionW, metricsRowH);
    labelC50Value.setBounds(metricsLeft + colW + colSpacing + captionW, metricsRow1Y, valueW, metricsRowH);
    labelC80Caption.setBounds(metricsLeft, metricsRow2Y, captionW, metricsRowH);
    labelC80Value.setBounds(metricsLeft + captionW, metricsRow2Y, valueW, metricsRowH);
    labelEDTCaption.setBounds(metricsLeft + colW + colSpacing, metricsRow2Y, captionW, metricsRowH);
    labelEDTValue.setBounds(metricsLeft + colW + colSpacing + captionW, metricsRow2Y, valueW, metricsRowH);
}

// ─────────────────────────────────────────────────────────────────────────────
//  paint
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbEditor::paint(juce::Graphics& g)
{
    g.fillAll(AmbienceColors::Background);
    juce::ColourGradient grad(
        AmbienceColors::Surface.withAlpha(0.12f), 0.f, 0.f,
        AmbienceColors::Background, 0.f, (float)H, false);
    g.setGradientFill(grad);
    g.fillAll();

    g.setFont(juce::Font(juce::FontOptions(8.f)));
    g.setColour(AmbienceColors::TextSecondary.withAlpha(0.35f));
    g.drawText("16ch FDN | SAPF | ISM-ER | 44.1-192kHz",
        PAD + 336, Y_HEADER + 10, W / 2, 12,
        juce::Justification::centredLeft);

    g.setColour(AmbienceColors::Separator);
    g.drawHorizontalLine(Y_SEP, (float)PAD, (float)(W - PAD));

    g.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 8.5f, juce::Font::bold)));

    auto sl = [&](int x, int y, const char* t) {
        g.drawText(t, x, y, 200, 14, juce::Justification::centredLeft);
        };

    if (!isProMode) {
        // ── Row 1 セパレーター ──
        g.setColour(AmbienceColors::Separator);
        g.drawVerticalLine(SEP_TF, (float)Y_SLABEL1, (float)(Y_ROW1 + UNIT_H));
        g.drawVerticalLine(SEP_FD, (float)Y_SLABEL1, (float)(Y_ROW1 + UNIT_H));
        g.drawVerticalLine(SEP_DS, (float)Y_SLABEL1, (float)(Y_ROW1 + UNIT_H));
        g.drawVerticalLine(SEP_SC, (float)Y_SLABEL1, (float)(Y_ROW1 + UNIT_H));

        // ── Row 2 セパレーター ──
        const int row2_outeq_x = PAD + 2 * (KNOB_W + PAD) + 16;
        const int row2_duck_x = row2_outeq_x + 2 * (KNOB_W + PAD) + 16;
        g.drawVerticalLine(row2_outeq_x - 9, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));
        g.drawVerticalLine(row2_duck_x - 9, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));

        // ── セクション名 ──
        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        sl(SEC_TIME, Y_SLABEL1, "TIME");
        sl(SEC_FREQUENCY, Y_SLABEL1, "FREQUENCY");
        sl(SEC_DIFFUSION, Y_SLABEL1, "DIFFUSION");
        sl(SEC_STEREO, Y_SLABEL1, "STEREO");
        sl(SEC_CHARACTER, Y_SLABEL1, "CHARACTER");
        sl(PAD, Y_SLABEL2, "MIX");
        sl(row2_outeq_x, Y_SLABEL2, "OUT EQ");
        sl(row2_duck_x, Y_SLABEL2, "DUCKING");

    }
    else {
        // ── ProMode ──
        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        sl(PAD, Y_SLABEL1, "RT60 PER BAND");

        g.setColour(AmbienceColors::Separator.withAlpha(0.5f));
        g.drawHorizontalLine(Y_SLABEL2 - 4, (float)PAD, (float)(W - PAD));

        const int tilt_x = PAD + KNOB_W + PAD + PAD + 8;
        const int outeq_x = tilt_x + 3 * (KNOB_W + PAD) + 16;
        g.setColour(AmbienceColors::Separator);
        g.drawVerticalLine(outeq_x - 9, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));

        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        sl(tilt_x, Y_SLABEL2, "TILT EQ");
        sl(outeq_x, Y_SLABEL2, "OUT EQ");
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  プリセットセクション (常時・モード非依存)
    // ─────────────────────────────────────────────────────────────────────────
    g.setColour(AmbienceColors::Separator);
    g.drawVerticalLine(PRESET_PANEL_X - 9,
        (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));
    g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
    sl(PRESET_PANEL_X, Y_SLABEL2, "PRESET");
}

// ─────────────────────────────────────────────────────────────────────────────
//  プリセット UI ヘルパー
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbEditor::refreshPresetCombo()
{
    presetCombo.clear(juce::dontSendNotification);
    auto names = presetManager->getPresetNames();

    // ─────────────────────────────────────────────────────────────────────
    //  ★ 修正: 起動時に Processor から保存済みのプリセット名を復元
    // ─────────────────────────────────────────────────────────────────────
    //  エディターが閉じられると PresetManager ごと破棄されるため、
    //  currentPresetName が空になる。
    //  Processor 側に lastSavedPresetName として永続化した名前を
    //  ここで PresetManager に再セットすることで、
    //  コンボボックスの選択が正しく復元される。
    // ─────────────────────────────────────────────────────────────────────
    if (presetManager->getCurrentPresetName().isEmpty()) {
        auto saved = audioProcessor.getLastSavedPresetName();
        if (saved.isNotEmpty())
            presetManager->setCurrentPresetName(saved);
    }

    if (names.isEmpty()) {
        presetCombo.addItem("-- No Presets --", 1);
        presetCombo.setSelectedItemIndex(0, juce::dontSendNotification);
        presetDeleteButton.setEnabled(false);
        presetLoadButton.setEnabled(false);
        presetPrevButton.setEnabled(false);
        presetNextButton.setEnabled(false);
        return;
    }

    for (int i = 0; i < names.size(); ++i)
        presetCombo.addItem(names[i], i + 1);

    int idx = presetManager->getCurrentPresetIndex();
    if (idx >= 0)
        presetCombo.setSelectedItemIndex(idx, juce::dontSendNotification);
    else
        presetCombo.setSelectedItemIndex(0, juce::dontSendNotification);

    presetDeleteButton.setEnabled(true);
    presetLoadButton.setEnabled(true);
    presetPrevButton.setEnabled(names.size() > 1);
    presetNextButton.setEnabled(names.size() > 1);
}

void FDNReverbEditor::savePresetWithDialog()
{
    // ─────────────────────────────────────────────────────────────────────
    //  AlertWindow を使ったプリセット名入力ダイアログ
    //  enterModalState(async callback) を使用: runModalLoop() 不要
    //  SafePointer でエディタが先に破棄された場合を安全に処理
    // ─────────────────────────────────────────────────────────────────────
    auto* dialog = new juce::AlertWindow(
        "Save Preset",
        "Enter a name for this preset:",
        juce::MessageBoxIconType::NoIcon);

    dialog->addTextEditor("name", presetManager->getCurrentPresetName());
    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FDNReverbEditor> safeThis(this);

    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, dialog](int result) {
                if (safeThis != nullptr && result == 1) {
                    auto name = dialog->getTextEditorContents("name").trim();
                    if (name.isNotEmpty())
                        safeThis->presetManager->savePreset(name);
                }
            }),
        true   // deleteWhenDismissed
    );
}

void FDNReverbEditor::deleteCurrentPreset()
{
    auto name = presetManager->getCurrentPresetName();
    if (name.isEmpty()) return;

    auto* dialog = new juce::AlertWindow(
        "Delete Preset",
        "Delete \"" + name + "\"?",
        juce::MessageBoxIconType::WarningIcon);

    dialog->addButton("Delete", 1);
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FDNReverbEditor> safeThis(this);

    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, name](int result) {
                if (safeThis != nullptr && result == 1)
                    safeThis->presetManager->deletePreset(name);
            }),
        true
    );
}