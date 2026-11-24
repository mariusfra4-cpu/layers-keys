cmake_minimum_required (VERSION 3.22)

project (LayersSplit VERSION 1.3.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    # textes du code source en UTF-8 (accents)
    add_compile_options (/utf-8 /MP)
    # exécutables sans dépendance au runtime Visual C++ installé
    set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

set (JUCE_DIR "" CACHE PATH "Dossier des sources de JUCE 8")
set (ORT_INCLUDE_DIR "" CACHE PATH "Dossier des en-têtes d'ONNX Runtime")

if (NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message (FATAL_ERROR "JUCE introuvable : -DJUCE_DIR=...")
endif ()
if (NOT EXISTS "${ORT_INCLUDE_DIR}/onnxruntime_cxx_api.h")
    message (FATAL_ERROR "En-têtes ONNX Runtime introuvables : -DORT_INCLUDE_DIR=...")
endif ()

add_subdirectory ("${JUCE_DIR}" juce EXCLUDE_FROM_ALL)

set (ENGINE_SOURCES
    Source/Engine/OrtRuntime.cpp
    Source/Engine/Dsp.cpp
    Source/Engine/MdxModel.cpp
    Source/Engine/DemucsModel.cpp
    Source/Engine/LeadSplit.cpp
    Source/Engine/ChoirSplit.cpp
    Source/Engine/BasicPitch.cpp
    Source/Engine/Separator.cpp
    Source/Engine/Analysis.cpp
    Source/Core/AudioIO.cpp)

set (COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    _USE_MATH_DEFINES)

#==============================================================================
juce_add_plugin (LayersSplit
    COMPANY_NAME "JB Praise"
    COMPANY_WEBSITE ""
    PRODUCT_NAME "Layers Split"
    BUNDLE_ID com.jbpraise.layerssplit
    PLUGIN_MANUFACTURER_CODE JbPr
    PLUGIN_CODE LSpl
    DESCRIPTION "Séparation de stems par IA"
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Sampler
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources (LayersSplit PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/TrackView.cpp
    Source/Core/SeparationJob.cpp
    ${ENGINE_SOURCES})

target_include_directories (LayersSplit PRIVATE "${ORT_INCLUDE_DIR}")
target_compile_definitions (LayersSplit PUBLIC
    ${COMMON_DEFS}
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries (LayersSplit
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

if (UNIX AND NOT APPLE)
    target_link_libraries (LayersSplit PRIVATE dl)
endif ()

#==============================================================================
juce_add_console_app (LayersSplitCli PRODUCT_NAME "LayersSplitCli")
target_sources (LayersSplitCli PRIVATE cli/main.cpp ${ENGINE_SOURCES})
target_include_directories (LayersSplitCli PRIVATE "${ORT_INCLUDE_DIR}")
target_compile_definitions (LayersSplitCli PRIVATE ${COMMON_DEFS})
target_link_libraries (LayersSplitCli
    PRIVATE juce::juce_audio_formats juce::juce_events
    PUBLIC juce::juce_recommended_config_flags)
if (UNIX AND NOT APPLE)
    target_link_libraries (LayersSplitCli PRIVATE dl)
endif ()

#==============================================================================
option (LAYERS_SPLIT_TESTS "Construire les tests" OFF)
if (LAYERS_SPLIT_TESTS)
    add_executable (LayersSplitTests tests/processor_test.cpp)
    target_link_libraries (LayersSplitTests PRIVATE LayersSplit)
    target_include_directories (LayersSplitTests PRIVATE "${ORT_INCLUDE_DIR}" "${JUCE_DIR}/modules")
    target_compile_definitions (LayersSplitTests PRIVATE $<TARGET_PROPERTY:LayersSplit,COMPILE_DEFINITIONS>)
endif ()
