#!/bin/bash
# Construit les deux logiciels Windows, le plugin VST3 et l'installateur unique.
set -e
cd "$(dirname "$0")"
TC=${MINGW:-/tmp/claude-0/-home-claude/d0d041ff-53a5-531b-b692-1ca49e4278c1/scratchpad/tc/llvm-mingw-20250709-ucrt-ubuntu-22.04-x86_64/bin}
mkdir -p dist installer/payload
for a in amd64 386; do
  n=64; [ $a = 386 ] && n=32
  GOOS=windows GOARCH=$a go build -trimpath -ldflags "-H windowsgui -s -w" -o dist/LayersKeys$n.exe .
done
# plugin VST3 : compilé depuis une copie sans l'icône (.syso) qui fait planter l'éditeur de liens
rm -rf /tmp/vstbuild && mkdir /tmp/vstbuild
tar --exclude=.git --exclude=dist --exclude='*.syso' --exclude=installer --exclude='*_test.go' -cf - . | tar -xf - -C /tmp/vstbuild
# comme l'Audio Unit du Mac : le moteur Go n'interrompt jamais le fil audio du logiciel hôte (asyncpreemptoff=1)
VSTGODEBUG="$(cd /tmp/vstbuild && GOOS=windows GOARCH=amd64 CGO_ENABLED=1 go list -buildvcs=false -tags vst3 -f '{{.DefaultGODEBUG}}' .)"
VSTGODEBUG="${VSTGODEBUG:+$VSTGODEBUG,}asyncpreemptoff=1"
(cd /tmp/vstbuild && PATH=$TC:$PATH CGO_ENABLED=1 CC=x86_64-w64-mingw32-gcc GOOS=windows GOARCH=amd64 \
  go build -buildvcs=false -tags vst3 -buildmode=c-shared -trimpath -ldflags "-s -w -X=runtime.godebugDefault=$VSTGODEBUG" -o "$OLDPWD/dist/LayersKeys.vst3" .)
rm -f dist/LayersKeys.h
cp dist/LayersKeys64.exe dist/LayersKeys32.exe dist/LayersKeys.vst3 installer/payload/
rm -f installer/payload/LayersKeys_vst3.dll
(cd installer && GOOS=windows GOARCH=386 go build -trimpath -ldflags "-H windowsgui -s -w" -o ../dist/LayersKeys_Installer.exe .)
ls -la dist
