#!/bin/bash
# Construit l'APK « JB Clés » avec les outils du SDK Android seuls (pas de Gradle).
# SECRET_JAVA (facultatif) : autre fichier Secret.java (utilisé pour la version de test automatique)
set -e
cd "$(dirname "$0")"
SDK=${ANDROID_HOME:-$ANDROID_SDK_ROOT}
BT=$(ls -d "$SDK"/build-tools/* | grep -v rc | sort -V | tail -1)
PLAT=$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)
AJ="$PLAT/android.jar"
OUT=${OUT:-build}
echo "build-tools: $BT ; plateforme: $PLAT"
rm -rf "$OUT" && mkdir -p "$OUT/classes" "$OUT/src"
cp -r src/. "$OUT/src/"
[ -n "$SECRET_JAVA" ] && cp "$SECRET_JAVA" "$OUT/src/com/jbpraise/keygen/Secret.java"
"$BT/aapt2" compile --dir res -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$AJ" --manifest AndroidManifest.xml --min-sdk-version 26 --target-sdk-version 34 --version-code 1 --version-name 1.0.0 -R "$OUT/res.zip"
javac --release 8 -Xlint:-options -cp "$AJ" -d "$OUT/classes" $(find "$OUT/src" -name '*.java')
"$BT/d8" --min-api 26 --lib "$AJ" --output "$OUT" $(find "$OUT/classes" -name '*.class')
(cd "$OUT" && zip -q -j base.apk classes.dex)
"$BT/zipalign" -f -p 4 "$OUT/base.apk" "$OUT/aligned.apk"
"$BT/apksigner" sign --ks jbkeygen.jks --ks-pass pass:jbpraise-keys --key-pass pass:jbpraise-keys --ks-key-alias jbkeys --out "$OUT/jbkeys.apk" "$OUT/aligned.apk"
"$BT/apksigner" verify --verbose "$OUT/jbkeys.apk" | head -5
ls -la "$OUT/jbkeys.apk"
