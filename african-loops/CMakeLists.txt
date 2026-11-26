cmake_minimum_required(VERSION 3.22)
project(AfricanLoops VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Moteur audio : sans dépendance, utilisable dans un plugin JUCE ou un projet HISE.
add_library(AfricanLoopsCore STATIC
  core/src/Audio.cpp
  core/src/Spectrum.cpp
  core/src/Tempo.cpp
  core/src/Percussion.cpp
  core/src/Library.cpp
  core/src/Looper.cpp)
target_include_directories(AfricanLoopsCore PUBLIC core/include)
set_target_properties(AfricanLoopsCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(AfricanLoopsCore PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(AfricanLoopsCore PRIVATE /W4 /utf-8)
else()
  target_compile_options(AfricanLoopsCore PRIVATE -Wall -Wextra)
endif()

# Tests
add_executable(AfricanLoopsTests core/tests/tests.cpp)
target_link_libraries(AfricanLoopsTests PRIVATE AfricanLoopsCore)
if(MSVC)
  target_compile_options(AfricanLoopsTests PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME moteur COMMAND AfricanLoopsTests)

# licences : signature Ed25519 (Monocypher), codes et clés ; partagé avec « JB Générateur de clés »
set(AL_MAC_LIBS "")
if(APPLE)
  set(AL_MAC_LIBS "-framework IOKit")  # identifiant du Mac pour la licence
endif()
add_library(AfricanLoopsLicence STATIC
  licence/KeyCrypto.cpp
  licence/LicenceCore.cpp
  plugin/third_party/monocypher/monocypher.c)
target_include_directories(AfricanLoopsLicence PUBLIC licence)
set_target_properties(AfricanLoopsLicence PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_executable(AfricanLoopsLicenceTests licence/tests.cpp)
target_link_libraries(AfricanLoopsLicenceTests PRIVATE AfricanLoopsLicence)
add_test(NAME licence COMMAND AfricanLoopsLicenceTests)
if(MSVC)
  target_compile_options(AfricanLoopsLicenceTests PRIVATE /utf-8)
  target_compile_options(AfricanLoopsLicence PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

# Outil en ligne de commande : analyser un dossier, séparer une loop
add_executable(africanloops-analyse tools/analyse.cpp)
target_link_libraries(africanloops-analyse PRIVATE AfricanLoopsCore)
if(MSVC)
  target_compile_options(africanloops-analyse PRIVATE /utf-8)
endif()

# ------------------------------------------------------------------ plugin
# Plugin VST3 / AU / logiciel autonome, construit avec JUCE.
#   -DJUCE_DIR=<dossier de JUCE>  utilise une copie locale de JUCE,
#   sinon JUCE 8.0.4 est téléchargé automatiquement.
option(AFRICANLOOPS_PLUGIN "Construire le plugin (JUCE)" ON)
if(AFRICANLOOPS_PLUGIN)
  include(FetchContent)
  if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                              GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
  endif()

  # Ableton Link (synchronisation avec Ableton et les autres logiciels)
  #   -DLINK_DIR=<dossier de Link>  utilise une copie locale, sinon Link 3.1.5 est téléchargé
  if(LINK_DIR)
    include(${LINK_DIR}/AbletonLinkConfig.cmake)
  else()
    FetchContent_Declare(link GIT_REPOSITORY https://github.com/Ableton/link.git
                              GIT_TAG Link-3.1.5 GIT_SHALLOW TRUE GIT_SUBMODULES modules/asio-standalone)
    FetchContent_GetProperties(link)
    if(NOT link_POPULATED)
      FetchContent_Populate(link)
    endif()
    include(${link_SOURCE_DIR}/AbletonLinkConfig.cmake)
  endif()

  set(AL_FORMATS VST3 Standalone)
  if(APPLE)
    list(APPEND AL_FORMATS AU)
  endif()

  juce_add_plugin(AfricanLoopsPro
    PRODUCT_NAME "African Loops Pro"
    COMPANY_NAME "JB Praise"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/installer/africanloops_icon.png"
    BUNDLE_ID "com.africanloops.pro"
    PLUGIN_MANUFACTURER_CODE Aflp
    PLUGIN_CODE Alpr
    FORMATS ${AL_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

  if(MSVC)
    target_compile_options(AfricanLoopsPro PRIVATE /utf-8)  # textes avec accents
  endif()

  target_sources(AfricanLoopsPro PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/LookAndFeel.cpp
    plugin/LinkSync.cpp
    plugin/Effects.cpp plugin/FxPage.cpp plugin/Lang.cpp plugin/Licence.cpp plugin/LicencePanel.cpp)

  target_compile_definitions(AfricanLoopsPro PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

  target_link_libraries(AfricanLoopsPro
    PRIVATE
      AfricanLoopsCore
      AfricanLoopsLicence
      ${AL_MAC_LIBS}
      Ableton::Link
      juce::juce_audio_utils
      juce::juce_dsp
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)
endif()

# Tests du plugin complet (fil audio + interface), sans logiciel hôte
option(AFRICANLOOPS_PLUGIN_TESTS "Construire les tests du plugin" OFF)
if(AFRICANLOOPS_PLUGIN AND AFRICANLOOPS_PLUGIN_TESTS)
  juce_add_console_app(AfricanLoopsPluginTests PRODUCT_NAME "AfricanLoopsPluginTests")
  if(MSVC)
    target_compile_options(AfricanLoopsPluginTests PRIVATE /utf-8)
  endif()
  target_sources(AfricanLoopsPluginTests PRIVATE
    plugin/tests/PluginTests.cpp
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/LookAndFeel.cpp
    plugin/LinkSync.cpp
    plugin/Effects.cpp plugin/FxPage.cpp plugin/Lang.cpp plugin/Licence.cpp plugin/LicencePanel.cpp)
  target_compile_definitions(AfricanLoopsPluginTests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=1)
  target_link_libraries(AfricanLoopsPluginTests PRIVATE
    AfricanLoopsCore AfricanLoopsLicence ${AL_MAC_LIBS} Ableton::Link juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
if(AFRICANLOOPS_PLUGIN AND AFRICANLOOPS_PLUGIN_TESTS)
  # petit hôte qui ouvre le vrai fichier VST3 construit
  juce_add_console_app(AfricanLoopsHostTest PRODUCT_NAME "AfricanLoopsHostTest")
  target_sources(AfricanLoopsHostTest PRIVATE tools/hosttest.cpp)
  target_compile_definitions(AfricanLoopsHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(AfricanLoopsHostTest PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
if(AFRICANLOOPS_PLUGIN AND AFRICANLOOPS_PLUGIN_TESTS)
  # visite guidée : captures d'écran de chaque étape
  juce_add_console_app(AfricanLoopsTour PRODUCT_NAME "AfricanLoopsTour")
  if(MSVC)
    target_compile_options(AfricanLoopsTour PRIVATE /utf-8)
  endif()
  target_sources(AfricanLoopsTour PRIVATE
    plugin/tests/Tour.cpp plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/LookAndFeel.cpp plugin/LinkSync.cpp plugin/Effects.cpp plugin/FxPage.cpp plugin/Lang.cpp plugin/Licence.cpp plugin/LicencePanel.cpp)
  target_compile_definitions(AfricanLoopsTour PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_STANDALONE_APPLICATION=1)
  target_link_libraries(AfricanLoopsTour PRIVATE
    AfricanLoopsCore AfricanLoopsLicence ${AL_MAC_LIBS} Ableton::Link juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()

# « JB Générateur de clés » (Windows, Mac) : clés de Layers Keys et d'African Loops Pro
option(AFRICANLOOPS_KEYGEN "Construire JB Générateur de clés" ON)
if(AFRICANLOOPS_PLUGIN AND AFRICANLOOPS_KEYGEN)
  juce_add_gui_app(JBKeygen
    PRODUCT_NAME "JB Generateur de cles"
    COMPANY_NAME "JB Praise"
    BUNDLE_ID com.jbpraise.keygen
    ICON_BIG installer/africanloops_icon.png
    VERSION 1.0.0)
  target_sources(JBKeygen PRIVATE keygen/KeygenApp.cpp plugin/LookAndFeel.cpp plugin/Lang.cpp)
  target_compile_definitions(JBKeygen PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
  if(MSVC)
    target_compile_options(JBKeygen PRIVATE /utf-8)
  endif()
  target_link_libraries(JBKeygen PRIVATE AfricanLoopsLicence juce::juce_gui_extra juce::juce_audio_basics
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

