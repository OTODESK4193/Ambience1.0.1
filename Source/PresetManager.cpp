#include "PresetManager.h"
#include "PluginProcessor.h"

PresetManager::PresetManager(FDNReverbAudioProcessor& p)
    : processor(p)
{
    refreshPresetList();
}

// ─────────────────────────────────────────────────────────────────────────────
//  ファイルシステム
// ─────────────────────────────────────────────────────────────────────────────
juce::File PresetManager::getPresetsFolder() const
{
    auto folder = juce::File::getSpecialLocation(
        juce::File::userDocumentsDirectory)
        .getChildFile(kSubFolder);
    if (!folder.exists())
        folder.createDirectory();
    return folder;
}

juce::File PresetManager::getPresetFile(const juce::String& name) const
{
    return getPresetsFolder().getChildFile(name + kExtension);
}

void PresetManager::refreshPresetList()
{
    presetNames.clear();
    auto files = getPresetsFolder().findChildFiles(
        juce::File::findFiles, false,
        juce::String("*") + kExtension);
    files.sort();
    for (const auto& f : files)
        presetNames.add(f.getFileNameWithoutExtension());
}

// ─────────────────────────────────────────────────────────────────────────────
//  保存
// ─────────────────────────────────────────────────────────────────────────────
//  PluginProcessor の getStateInformation() をそのまま利用する。
//  これにより PluginProcessor の変更が一切不要になる。
// ─────────────────────────────────────────────────────────────────────────────
// ─── 変更後 ───
bool PresetManager::savePreset(const juce::String& name)
{
    if (name.isEmpty()) return false;
    // ★ 修正: getStateInformation の前に名前を Processor に通知する
    // これにより getStateInformation が正しい名前を ValueTree に書き込める
    processor.setLastSavedPresetName(name);
    juce::MemoryBlock data;
    processor.getStateInformation(data);

    auto file = getPresetFile(name);
    if (!file.replaceWithData(data.getData(), data.getSize()))
        return false;

    currentPresetName = name;
    refreshPresetList();
    if (onPresetListChanged) onPresetListChanged();
    if (onPresetLoaded)      onPresetLoaded(name);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ロード
// ─────────────────────────────────────────────────────────────────────────────
//  PluginProcessor の setStateInformation() をそのまま利用する。
//  setStateInformation() 内で paramsNeedUpdate=true が設定されるため、
//  次の processBlock で Engine に新しいパラメータが送られる。
// ─────────────────────────────────────────────────────────────────────────────
bool PresetManager::loadPreset(const juce::String& name)
{
    auto file = getPresetFile(name);
    if (!file.exists()) return false;

    juce::MemoryBlock data;
    if (!file.loadFileAsData(data)) return false;

    processor.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    currentPresetName = name;
    if (onPresetLoaded) onPresetLoaded(name);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  削除
// ─────────────────────────────────────────────────────────────────────────────
bool PresetManager::deletePreset(const juce::String& name)
{
    auto file = getPresetFile(name);
    if (!file.exists()) return false;

    if (!file.deleteFile()) return false;

    if (currentPresetName == name)
        currentPresetName.clear();

    refreshPresetList();
    if (onPresetListChanged) onPresetListChanged();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ナビゲーション
// ─────────────────────────────────────────────────────────────────────────────
int PresetManager::getCurrentPresetIndex() const noexcept
{
    return presetNames.indexOf(currentPresetName);
}

void PresetManager::loadPrevPreset()
{
    if (presetNames.isEmpty()) return;
    int idx = getCurrentPresetIndex();
    if (idx <= 0)
        idx = presetNames.size();
    loadPreset(presetNames[idx - 1]);
}

void PresetManager::loadNextPreset()
{
    if (presetNames.isEmpty()) return;
    int idx = getCurrentPresetIndex();
    if (idx < 0 || idx >= presetNames.size() - 1)
        idx = -1;
    loadPreset(presetNames[idx + 1]);
}