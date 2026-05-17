#pragma once
#include <JuceHeader.h>

class FDNReverbAudioProcessor;

// ─────────────────────────────────────────────────────────────────────────────
//  PresetManager
// ─────────────────────────────────────────────────────────────────────────────
//  プリセットをファイルとして管理する。
//
//  保存先: ~/Documents/Ambience/Presets/*.ambpreset
//  フォーマット: APVTS の getStateInformation/setStateInformation と同じバイナリ
//               → 既存のセッション保存ロジックを完全流用し、追加コードを最小化
//
//  リアルタイム安全性:
//    - ファイル I/O はすべてメッセージスレッド（UI）から呼ばれる前提
//    - processBlock は一切関与しない
// ─────────────────────────────────────────────────────────────────────────────
class PresetManager
{
public:
    explicit PresetManager(FDNReverbAudioProcessor& processor);

    // ─── プリセット操作 ───
    bool savePreset(const juce::String& name);
    bool loadPreset(const juce::String& name);
    bool deletePreset(const juce::String& name);

    // ─── ナビゲーション ───
    void loadPrevPreset();
    void loadNextPreset();

    // ─── 状態取得 ───
    juce::StringArray getPresetNames()       const noexcept { return presetNames; }
    // ─── 変更後 ───
    juce::String      getCurrentPresetName() const noexcept { return currentPresetName; }
    void              setCurrentPresetName(const juce::String& name) noexcept { currentPresetName = name; }  // ★ 追加
    int               getCurrentPresetIndex() const noexcept;

    bool              hasPresets()           const noexcept { return !presetNames.isEmpty(); }

    // ─── フォルダ取得 ───
    juce::File getPresetsFolder() const;

    // ─── UI 更新コールバック ───
    std::function<void()>                    onPresetListChanged;
    std::function<void(const juce::String&)> onPresetLoaded;

private:
    void       refreshPresetList();
    juce::File getPresetFile(const juce::String& name) const;

    FDNReverbAudioProcessor& processor;
    juce::StringArray        presetNames;
    juce::String             currentPresetName;

    static constexpr const char* kExtension = ".ambpreset";
    static constexpr const char* kSubFolder = "Ambience/Presets";
};