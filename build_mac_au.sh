#!/bin/bash
# Construit le plugin Audio Unit « LayersKeys.component » pour Mac (Logic Pro, GarageBand, MainStage…).
# À lancer SUR UN MAC : il faut Go et les outils de ligne de commande d'Apple (xcode-select --install).
# Sans Mac : voir COMMENT_CONSTRUIRE_MAC.txt (GitHub le fait gratuitement).
#   ./build_mac_au.sh            construit dist/mac/LayersKeys.component
#   ./build_mac_au.sh install    construit, installe dans ~/Library/Audio/Plug-Ins/Components et lance le test officiel auval
set -e
cd "$(dirname "$0")"
[ "$(uname)" = Darwin ] || { echo "Ce script doit être lancé sur un Mac."; exit 1; }
# compatible macOS 11 (Big Sur) et plus récent, même construit sur un Mac plus récent
export MACOSX_DEPLOYMENT_TARGET=11.0
export CGO_CFLAGS="${CGO_CFLAGS:--O2} -mmacosx-version-min=11.0"
export CGO_LDFLAGS="${CGO_LDFLAGS} -mmacosx-version-min=11.0"
OUT=dist/mac
PLUG="$OUT/LayersKeys.component"
rm -rf "$PLUG" "$OUT/au" && mkdir -p "$PLUG/Contents/MacOS" "$OUT/au"
# Logic Pro 13 prend tout signal reçu pendant un appel au plugin pour un plantage (« reported a problem… »).
# Go envoie de lui-même des signaux (SIGURG) pour interrompre ses fils : on les coupe dans le plugin
# (asyncpreemptoff=1, ajouté aux réglages par défaut de Go). Vérifié par : tools/au_logic_test.m --gardien
LKGODEBUG="$(go list -tags au -f '{{.DefaultGODEBUG}}' .)"
LKGODEBUG="${LKGODEBUG:+$LKGODEBUG,}asyncpreemptoff=1"
export LK_AU_LDFLAGS="-X=runtime.godebugDefault=$LKGODEBUG"
echo "réglages Go du plugin : $LKGODEBUG"
for a in arm64 amd64; do
  CGO_ENABLED=1 GOOS=darwin GOARCH=$a CC=clang go build -tags au -buildmode=c-shared -trimpath -ldflags="$LK_AU_LDFLAGS" \
    -o "$OUT/au/LayersKeys_$a.dylib" .
done
lipo -create "$OUT/au/LayersKeys_arm64.dylib" "$OUT/au/LayersKeys_amd64.dylib" -output "$PLUG/Contents/MacOS/LayersKeys"
cp mac/au_Info.plist "$PLUG/Contents/Info.plist"
printf 'BNDL????' > "$PLUG/Contents/PkgInfo"
codesign --force --deep --sign - "$PLUG"
rm -rf "$OUT/au"
echo "Plugin construit : $PLUG"
if [ "$1" = install ]; then
  DEST="$HOME/Library/Audio/Plug-Ins/Components"
  mkdir -p "$DEST" && rm -rf "$DEST/LayersKeys.component" && cp -R "$PLUG" "$DEST/"
  xattr -cr "$DEST/LayersKeys.component" 2>/dev/null || true
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  echo "Installé dans $DEST. Test officiel d'Apple (auval) :"
  auval -v aumu Lkys JBPr || echo "auval a signalé un problème : envoie-moi le texte ci-dessus."
fi
