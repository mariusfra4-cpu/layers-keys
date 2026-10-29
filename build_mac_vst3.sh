#!/bin/bash
# Construit le plugin VST3 « LayersKeys.vst3 » pour Mac (Apple Silicon + Intel).
# À lancer SUR UN MAC : il faut Go et les outils de ligne de commande d'Apple (xcode-select --install).
# Sans Mac : voir COMMENT_CONSTRUIRE_MAC.txt (GitHub le fait gratuitement).
#   ./build_mac_vst3.sh            construit dist/mac/LayersKeys.vst3
#   ./build_mac_vst3.sh install    construit puis installe dans ~/Library/Audio/Plug-Ins/VST3
set -e
cd "$(dirname "$0")"
[ "$(uname)" = Darwin ] || { echo "Ce script doit être lancé sur un Mac."; exit 1; }
OUT=dist/mac
PLUG="$OUT/LayersKeys.vst3"
rm -rf "$PLUG" "$OUT/vst" && mkdir -p "$PLUG/Contents/MacOS" "$OUT/vst"
for a in arm64 amd64; do
  CGO_ENABLED=1 GOOS=darwin GOARCH=$a CC=clang go build -tags vst3 -buildmode=c-shared -trimpath \
    -o "$OUT/vst/LayersKeys_$a.dylib" .
done
lipo -create "$OUT/vst/LayersKeys_arm64.dylib" "$OUT/vst/LayersKeys_amd64.dylib" -output "$PLUG/Contents/MacOS/LayersKeys"
cp mac/vst3_Info.plist "$PLUG/Contents/Info.plist"
printf 'BNDL????' > "$PLUG/Contents/PkgInfo"
# signature « ad hoc » : obligatoire sur Apple Silicon pour que le DAW accepte de charger le plugin
codesign --force --deep --sign - "$PLUG"
rm -rf "$OUT/vst"
echo "Plugin construit : $PLUG"
if [ "$1" = install ]; then
  DEST="$HOME/Library/Audio/Plug-Ins/VST3"
  mkdir -p "$DEST" && rm -rf "$DEST/LayersKeys.vst3" && cp -R "$PLUG" "$DEST/"
  xattr -cr "$DEST/LayersKeys.vst3" 2>/dev/null || true
  echo "Installé dans $DEST — relance ton DAW et actualise la liste des plugins."
fi
