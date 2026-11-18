cmake_minimum_required(VERSION 3.22)
project(AfricanLoops VERSION 0.1.0 LANGUAGES CXX)

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
enable_testing()
add_test(NAME moteur COMMAND AfricanLoopsTests)

# Outil en ligne de commande : analyser un dossier, séparer une loop
add_executable(africanloops-analyse tools/analyse.cpp)
target_link_libraries(africanloops-analyse PRIVATE AfricanLoopsCore)
