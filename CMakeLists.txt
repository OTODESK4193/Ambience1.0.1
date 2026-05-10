cmake_minimum_required(VERSION 3.22)
project(Ambience VERSION 1.0.0 LANGUAGES C CXX)

# ============================================================================
# C++20
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# JUCE
# ============================================================================
add_subdirectory(C:/JUCE ${CMAKE_BINARY_DIR}/JUCE)

# ============================================================================
# プラグイン定義
# ============================================================================
juce_add_plugin(Ambience
    COMPANY_NAME            "OTODESK"
    PLUGIN_MANUFACTURER_CODE "Otdk"
    PLUGIN_CODE             "Ambi"
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "Ambience"
    PLUGIN_DESCRIPTION      "8-channel FDN Algorithmic Reverb"

    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    VST3_AUTO_MANIFEST      FALSE

    COPY_PLUGIN_AFTER_BUILD FALSE
    VERSION                 "1.0.0"
)

# ============================================================================
# ソース（存在するファイルのみ）
# ============================================================================
target_sources(Ambience PRIVATE
    # プラグインコア
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/PluginParameters.h
    Source/PluginParameters.cpp

    # GUIモジュール
    Source/GUI/AmbienceUI.h
    Source/GUI/AmbienceUI.cpp

    # DSPモジュール
    Source/AlgorithmPresets.h
    Source/DSP/DSPConstants.h
    Source/DSP/DelayMemory.h
    Source/DSP/BiquadFilters.h
    Source/DSP/BiquadFilters.cpp
    Source/DSP/UniversalEngine.h
    Source/DSP/UniversalEngine.cpp
)
# ============================================================================
# インクルードパス
# ============================================================================
target_include_directories(Ambience PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
)

# ============================================================================
# JUCE モジュール
# ============================================================================
target_link_libraries(Ambience
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ============================================================================
# コンパイル定義
# ============================================================================
target_compile_definitions(Ambience PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    AMBIENCE_VERSION="1.0.0"
)

# ============================================================================
# MSVC コンパイルオプション
# ============================================================================
if(MSVC)
    target_compile_options(Ambience PRIVATE
        /arch:AVX2
        /utf-8
        /W3
        /wd4819
        $<$<CONFIG:Release>:/O2 /fp:fast>
    )
endif()

# ============================================================================
# JUCE ヘッダー生成
# ============================================================================
juce_generate_juce_header(Ambience)

# ============================================================================
# ビルドメッセージ
# ============================================================================
message(STATUS "===========================================")
message(STATUS "Ambience VST3 - CMake Configuration")
message(STATUS "JUCE: C:/JUCE")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "===========================================")