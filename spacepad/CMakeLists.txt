# Spacepad by JB Praise — lecteur de pads et de crowds pour la scène.
# Construit : application autonome, plugin VST3, plugin Audio Unit (Mac) + programme de tests.
cmake_minimum_required(VERSION 3.22)
project(Spacepad VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS minimum")
endif()
if(MSVC)
  # programme autonome sans dépendre des bibliothèques Visual C++ installées sur le PC
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE : dossier fourni (-DJUCE_DIR=...) ou téléchargé automatiquement
set(JUCE_DIR "" CACHE PATH "Dossier de JUCE (facultatif)")
if(JUCE_DIR)
  add_subdirectory(${JUCE_DIR} juce)
else()
  include(FetchContent)
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
endif()

set(SPACEPAD_FORMATS Standalone VST3)
if(APPLE)
  list(APPEND SPACEPAD_FORMATS AU)
endif()

# polices intégrées (Michroma pour le logo, Inter pour le texte : licences libres OFL)
juce_add_binary_data(SpacepadData SOURCES
  resources/fonts/Michroma-Regular.ttf
  resources/fonts/Inter-Regular.ttf
  resources/fonts/Inter-SemiBold.ttf
  resources/fonts/Inter-Bold.ttf
  resources/galaxie.jpg
  resources/foule.jpg)
set_target_properties(SpacepadData PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(Spacepad
  PRODUCT_NAME "Spacepad"
  COMPANY_NAME "JB Praise"
  COMPANY_WEBSITE ""
  BUNDLE_ID "com.jbpraise.spacepad"
  PLUGIN_MANUFACTURER_CODE JBPr
  PLUGIN_CODE Spcp
  FORMATS ${SPACEPAD_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Sampler
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  DESCRIPTION "Spacepad by JB Praise"
  ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
  MICROPHONE_PERMISSION_ENABLED FALSE
  HARDENED_RUNTIME_ENABLED FALSE)

set(SPACEPAD_CORE_SOURCES
  Source/KeyDetect.cpp
  Source/Lang.cpp
  Source/Setlist.cpp
  Source/Library.cpp
  Source/PadEngine.cpp
  Source/Effects.cpp
  Source/AudioOpen.cpp
  Source/Modulation.cpp
  Source/CrowdPrefs.cpp)

target_sources(Spacepad PRIVATE
  ${SPACEPAD_CORE_SOURCES}
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/UI.cpp
  Source/UISpace.cpp
  Source/UIPanels.cpp
  Source/UIExtra.cpp
  Source/UICrowd.cpp
  Source/UIMain.cpp)

target_compile_definitions(Spacepad PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_USE_MP3AUDIOFORMAT=1
  JUCE_USE_WINDOWS_MEDIA_FORMAT=1
  JUCE_USE_FLAC=1
  JUCE_USE_OGGVORBIS=1
  JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Spacepad,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Spacepad,JUCE_VERSION>")

target_link_libraries(Spacepad
  PRIVATE
    SpacepadData
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------- Programme de tests (analyse des noms, des sons, des dossiers, moteur audio, capture de l'interface) ----------
juce_add_console_app(SpacepadTests PRODUCT_NAME "SpacepadTests")
target_sources(SpacepadTests PRIVATE
  ${SPACEPAD_CORE_SOURCES}
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/UI.cpp
  Source/UISpace.cpp
  Source/UIPanels.cpp
  Source/UIExtra.cpp
  Source/UICrowd.cpp
  Source/UIMain.cpp
  tests/Tests.cpp)
target_include_directories(SpacepadTests PRIVATE Source)
target_compile_definitions(SpacepadTests PRIVATE
  SPACEPAD_TESTS=1
  JUCE_MODAL_LOOPS_PERMITTED=1
  JucePlugin_Name="Spacepad"
  JucePlugin_Manufacturer="JB Praise"
  JucePlugin_VersionString="1.0.0"
  JucePlugin_IsSynth=1
  JucePlugin_WantsMidiInput=1
  JucePlugin_ProducesMidiOutput=0
  JucePlugin_IsMidiEffect=0
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_USE_MP3AUDIOFORMAT=1
  JUCE_USE_WINDOWS_MEDIA_FORMAT=1
  JUCE_USE_FLAC=1
  JUCE_USE_OGGVORBIS=1)
target_link_libraries(SpacepadTests
  PRIVATE
    SpacepadData
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags)
