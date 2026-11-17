#!/bin/bash
# Construit Spacepad sur votre Mac (Apple Silicon + Intel) et fabrique l'installateur dist/Spacepad-<version>.pkg.
# À faire une seule fois avant : installer les outils d'Apple (Terminal : xcode-select --install)
# et CMake (https://cmake.org/download, ou « brew install cmake »).
# Puis, dans le Terminal :  cd chemin/vers/layers-keys/spacepad && ./build_mac.sh
set -e
cd "$(dirname "$0")"
VER="${1:-1.0.0}"
[ "$(uname)" = Darwin ] || { echo "Ce script doit être lancé sur un Mac."; exit 1; }
command -v cmake >/dev/null || { echo "CMake manque : installez-le (https://cmake.org/download ou brew install cmake)."; exit 1; }
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build-mac --config Release --target Spacepad_Standalone Spacepad_VST3 Spacepad_AU SpacepadTests -j "$(sysctl -n hw.ncpu)"
A=build-mac/Spacepad_artefacts/Release
for b in "$A/Standalone/Spacepad.app" "$A/VST3/Spacepad.vst3" "$A/AU/Spacepad.component"; do
  codesign --force --deep -s - "$b"
done
echo "== Tests automatiques"
build-mac/SpacepadTests_artefacts/Release/SpacepadTests | tail -3
rm -rf build-mac/in && mkdir -p build-mac/in
cp -R "$A/Standalone/Spacepad.app" "$A/VST3/Spacepad.vst3" "$A/AU/Spacepad.component" build-mac/in/
installer/mac/build_pkg.sh build-mac/in "$VER"
echo
echo "Installateur prêt : $(pwd)/dist/Spacepad-$VER.pkg  (double-cliquez dessus pour installer)"
