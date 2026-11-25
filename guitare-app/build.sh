#!/bin/bash
# Construit l'APK « AccordLive » avec les outils du SDK Android seuls (pas de Gradle).
set -e
cd "$(dirname "$0")"
SDK=${ANDROID_HOME:-$ANDROID_SDK_ROOT}
BT=$(ls -d "$SDK"/build-tools/* | grep -v rc | sort -V | tail -1)
PLAT=$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)
AJ="$PLAT/android.jar"
OUT=${OUT:-build}
echo "build-tools: $BT ; plateforme: $PLAT"
rm -rf "$OUT" && mkdir -p "$OUT/classes"
"$BT/aapt2" compile --dir res -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$AJ" --manifest AndroidManifest.xml -A assets --min-sdk-version 26 --target-sdk-version 34 \
  --version-code ${VERSION_CODE:-1} --version-name ${VERSION_NAME:-1.0.0} -R "$OUT/res.zip" --auto-add-overlay
javac --release 8 -Xlint:-options -cp "$AJ" -d "$OUT/classes" $(find src -name '*.java')
"$BT/d8" --min-api 26 --lib "$AJ" --output "$OUT" $(find "$OUT/classes" -name '*.class')
(cd "$OUT" && zip -q -j base.apk classes.dex)
"$BT/zipalign" -f -p 4 "$OUT/base.apk" "$OUT/aligned.apk"
"$BT/apksigner" sign --ks ../android-app/jbkeygen.jks --ks-pass pass:jbpraise-keys --key-pass pass:jbpraise-keys --ks-key-alias jbkeys --out "$OUT/AccordLive.apk" "$OUT/aligned.apk"
"$BT/apksigner" verify --verbose "$OUT/AccordLive.apk" | head -5
ls -la "$OUT/AccordLive.apk"
