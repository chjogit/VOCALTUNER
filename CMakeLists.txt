cmake_minimum_required(VERSION 3.22)
project(VocalTuner VERSION 0.1.0)

option(VOCALTUNER_BUILD_TESTS "Build unit tests" ON)
# ON이면 JUCE 없이 vocaltuner_core + 테스트만 빌드 (Linux CI/검증용)
option(VOCALTUNER_CORE_ONLY "Build only the JUCE-free core and tests" OFF)

if(NOT VOCALTUNER_CORE_ONLY)
    add_subdirectory(JUCE)
endif()

# WORLD (BSD): Harvest F0 추출. 자체 CMakeLists는 쓰지 않고 필요한 소스만 빌드
include(FetchContent)
FetchContent_Declare(world
    GIT_REPOSITORY https://github.com/mmorise/World.git
    GIT_TAG d625e7608ca23a870018f01e7c562ac683d9847f   # v1.0.1
    SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(world)

add_library(world_harvest STATIC
    ${world_SOURCE_DIR}/src/harvest.cpp
    ${world_SOURCE_DIR}/src/common.cpp
    ${world_SOURCE_DIR}/src/matlabfunctions.cpp
    ${world_SOURCE_DIR}/src/fft.cpp
    ${world_SOURCE_DIR}/src/cheaptrick.cpp
    ${world_SOURCE_DIR}/src/d4c.cpp
    ${world_SOURCE_DIR}/src/synthesis.cpp
    ${world_SOURCE_DIR}/src/stonemask.cpp)
target_include_directories(world_harvest SYSTEM PUBLIC ${world_SOURCE_DIR}/src)
set_target_properties(world_harvest PROPERTIES POSITION_INDEPENDENT_CODE ON)

# JUCE 비의존 분석 코어 (플러그인과 테스트가 공유)
add_library(vocaltuner_core STATIC
    Source/Analysis/VocalFeatures.cpp
    Source/Analysis/BlockParser.cpp
    Source/Analysis/BlockEditing.cpp
    Source/Analysis/BlockTransforms.cpp
    Source/Analysis/BlockHistory.cpp
    Source/Alignment/SpectralFrames.cpp
    Source/Alignment/SectionClassifier.cpp
    Source/Alignment/TimeWarp.cpp
    Source/Alignment/ReferenceFit.cpp
    Source/Alignment/WordAnchors.cpp
    Source/Render/EditTimeMap.cpp
    Source/Render/EditRenderer.cpp
    Source/Render/SpectralDiff.cpp)
target_include_directories(vocaltuner_core PUBLIC Source/Analysis Source/Alignment Source/Render)
target_include_directories(vocaltuner_core PUBLIC Source/UI)
target_link_libraries(vocaltuner_core PUBLIC world_harvest)
target_compile_features(vocaltuner_core PUBLIC cxx_std_17)
set_target_properties(vocaltuner_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT VOCALTUNER_CORE_ONLY)
# juce_add_plugin 이전에 호출해야 함
juce_set_ara_sdk_path(${CMAKE_CURRENT_SOURCE_DIR}/ARA_SDK)

# 기본 복사 경로(Program Files)는 관리자 권한이 필요. -DVOCALTUNER_VST3_COPY_DIR=... 로 변경 가능
if(DEFINED VOCALTUNER_VST3_COPY_DIR)
    set_directory_properties(PROPERTIES JUCE_VST3_COPY_DIR "${VOCALTUNER_VST3_COPY_DIR}")
endif()

juce_add_plugin(VocalTuner
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    # TRUE면 에디터가 포커스를 잡아 일부 DAW에서 Space가 막힘. 텍스트 입력 UI 추가 시 재검토.
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE YCom
    PLUGIN_CODE Vtna
    FORMATS VST3
    PRODUCT_NAME "VocalTuner"
    IS_ARA_EFFECT TRUE)

target_sources(VocalTuner
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DocumentController.cpp
        Source/AlignmentController.cpp
        Source/AlignmentFitQueue.cpp
        Source/PlaybackRenderer.cpp
        Source/RenderCache.cpp
        Source/Alignment/WhisperRunner.cpp
        Source/UI/PianoRollView.cpp
        Source/UI/PianoRollEdit.cpp
        Source/UI/PianoRollDraw.cpp
        Source/UI/TrackPanel.cpp
        Source/UI/PitchBalancePanel.cpp
        Source/UI/ToolIcons.cpp
        Source/PluginEditorHistory.cpp
        Source/PlaybackPreview.cpp
        Source/PluginEditorPreview.cpp
        Source/Analysis/AnalysisTask.cpp
        Source/Analysis/AnalysisManager.cpp)

# 한글 주석이 CP949로 해석되지 않도록 소스를 UTF-8로 컴파일
if(MSVC)
    target_compile_options(VocalTuner PRIVATE /utf-8)
    target_compile_options(vocaltuner_core PRIVATE /utf-8)
endif()

target_compile_definitions(VocalTuner
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VocalTuner
    PRIVATE
        vocaltuner_core
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

endif()

if(VOCALTUNER_BUILD_TESTS)
    enable_testing()
    add_executable(VocalFeaturesTests Tests/VocalFeaturesTests.cpp)
    target_link_libraries(VocalFeaturesTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(VocalFeaturesTests PRIVATE /utf-8)
    endif()
    add_test(NAME VocalFeaturesTests COMMAND VocalFeaturesTests)

    add_executable(BlockParserTests Tests/BlockParserTests.cpp)
    target_link_libraries(BlockParserTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(BlockParserTests PRIVATE /utf-8)
    endif()
    add_test(NAME BlockParserTests COMMAND BlockParserTests)

    add_executable(BlockEditingTests Tests/BlockEditingTests.cpp)
    target_link_libraries(BlockEditingTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(BlockEditingTests PRIVATE /utf-8)
    endif()
    add_test(NAME BlockEditingTests COMMAND BlockEditingTests)

    add_executable(BlockTransformsTests Tests/BlockTransformsTests.cpp)
    target_link_libraries(BlockTransformsTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(BlockTransformsTests PRIVATE /utf-8)
    endif()
    add_test(NAME BlockTransformsTests COMMAND BlockTransformsTests)

    add_executable(BlockHistoryTests Tests/BlockHistoryTests.cpp)
    target_link_libraries(BlockHistoryTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(BlockHistoryTests PRIVATE /utf-8)
    endif()
    add_test(NAME BlockHistoryTests COMMAND BlockHistoryTests)

    add_executable(RollViewportTests Tests/RollViewportTests.cpp)
    target_link_libraries(RollViewportTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(RollViewportTests PRIVATE /utf-8)
    endif()
    add_test(NAME RollViewportTests COMMAND RollViewportTests)

    add_executable(AlignmentTests Tests/AlignmentTests.cpp)
    target_link_libraries(AlignmentTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(AlignmentTests PRIVATE /utf-8)
    endif()
    add_test(NAME AlignmentTests COMMAND AlignmentTests)

    add_executable(RenderTests Tests/RenderTests.cpp)
    target_link_libraries(RenderTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(RenderTests PRIVATE /utf-8)
    endif()
    add_test(NAME RenderTests COMMAND RenderTests)

    add_executable(WordAnchorTests Tests/WordAnchorTests.cpp)
    target_link_libraries(WordAnchorTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(WordAnchorTests PRIVATE /utf-8)
    endif()
    add_test(NAME WordAnchorTests COMMAND WordAnchorTests)

    add_executable(PitchSimilarityTests Tests/PitchSimilarityTests.cpp)
    target_link_libraries(PitchSimilarityTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(PitchSimilarityTests PRIVATE /utf-8)
    endif()
    add_test(NAME PitchSimilarityTests COMMAND PitchSimilarityTests)

    add_executable(SpectralDiffTests Tests/SpectralDiffTests.cpp)
    target_link_libraries(SpectralDiffTests PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(SpectralDiffTests PRIVATE /utf-8)
    endif()
    add_test(NAME SpectralDiffTests COMMAND SpectralDiffTests)

    # 원본 vs 렌더 스펙트럼 차이 CLI (테스트 아님)
    add_executable(spectral_diff Tools/spectral_diff.cpp)
    target_include_directories(spectral_diff PRIVATE Tools)
    target_link_libraries(spectral_diff PRIVATE vocaltuner_core)
    if(MSVC)
        target_compile_options(spectral_diff PRIVATE /utf-8)
    endif()
endif()
