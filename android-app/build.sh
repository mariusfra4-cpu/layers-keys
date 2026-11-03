#!/bin/bash
# Construit l'APK « Layers Keys » pour Android : moteur Go (arm64 + x86_64) + application Java.
# Outils : Go, NDK Android (compilateur C), SDK Android (aapt2, d8, zipalign, apksigner). Pas de Gradle.
set -e
cd "$(dirname "$0")"
HERE=$PWD
ROOT=$(cd .. && pwd)
SDK=${ANDROID_HOME:-$ANDROID_SDK_ROOT}
NDK=${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_ROOT:-$ANDROID_NDK_HOME}}
[ -z "$NDK" ] && NDK=$(ls -d "$SDK"/ndk/* | sort -V | tail -1)
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
BT=$(ls -d "$SDK"/build-tools/* | grep -v rc | sort -V | tail -1)
PLAT=$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)
AJ="$PLAT/android.jar"
OUT=${OUT:-$HERE/build}
ABIS=${ABIS:-"arm64-v8a x86_64"}
GOTAGS=${GOTAGS:-}
echo "NDK: $NDK ; build-tools: $BT ; plateforme: $PLAT ; go: $(go version)"
rm -rf "$OUT" && mkdir -p "$OUT/classes" "$OUT/lib"

for abi in $ABIS; do
  case $abi in
    arm64-v8a) GA=arm64; CC="$TC/aarch64-linux-android26-clang" ;;
    x86_64)    GA=amd64; CC="$TC/x86_64-linux-android26-clang" ;;
  esac
  mkdir -p "$OUT/lib/$abi"
  echo "== moteur Go ($abi)"
  (cd "$ROOT" && CGO_ENABLED=1 GOOS=android GOARCH=$GA CC="$CC" \
     go build -tags "$GOTAGS" -trimpath -buildmode=c-shared -ldflags="-s -w" -o "$OUT/lib/$abi/liblk.so" .)
  rm -f "$OUT/lib/$abi/liblk.h"
  ls -la "$OUT/lib/$abi/liblk.so"
done

"$BT/aapt2" compile --dir res -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$AJ" --manifest AndroidManifest.xml --min-sdk-version 26 --target-sdk-version 34 --version-code 1 --version-name 1.0.0 -R "$OUT/res.zip"
javac --release 8 -Xlint:-options -cp "$AJ" -d "$OUT/classes" $(find src -name '*.java')
"$BT/d8" --min-api 26 --lib "$AJ" --output "$OUT" $(find "$OUT/classes" -name '*.class')
(cd "$OUT" && zip -q -j base.apk classes.dex && zip -q -r base.apk lib)
"$BT/zipalign" -f -p 4 "$OUT/base.apk" "$OUT/aligned.apk"
"$BT/apksigner" sign --ks jbkeygen.jks --ks-pass pass:jbpraise-keys --key-pass pass:jbpraise-keys --ks-key-alias jbkeys --out "$OUT/${APKNAME:-layerskeys}.apk" "$OUT/aligned.apk"
"$BT/apksigner" verify --verbose "$OUT/${APKNAME:-layerskeys}.apk" | head -5
ls -la "$OUT/${APKNAME:-layerskeys}.apk"
