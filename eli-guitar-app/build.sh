#!/bin/bash
# Construit l'APK « Eli Guitar Chords » (page HTML dans une WebView Android). Pas de Gradle :
# SDK Android (aapt2, d8, zipalign, apksigner) + javac.
set -e
cd "$(dirname "$0")"
HERE=$PWD
SDK=${ANDROID_HOME:-$ANDROID_SDK_ROOT}
BT=$(ls -d "$SDK"/build-tools/* | grep -v rc | sort -V | tail -1)
PLAT=$(ls -d "$SDK"/platforms/android-* | grep -Ev "beta|ext|rc|preview" | sort -V | tail -1)
AJ="$PLAT/android.jar"
OUT=${OUT:-$HERE/build}
VCODE=${VCODE:-1}
VNAME=${VNAME:-1.1.0}
echo "build-tools: $BT ; plateforme: $PLAT"
rm -rf "$OUT" && mkdir -p "$OUT/classes" "$OUT/src"
cp -r src/. "$OUT/src/"
cp AndroidManifest.xml "$OUT/AndroidManifest.xml"
# version de test automatique : autre adresse de la liste des licences, vérification à chaque ouverture
if [ -n "$LICENCE_URL" ]; then
  L="$OUT/src/com/elipraise/guitarchords/Licence.java"
  sed -i "s#static final String LIST_URL = \".*\";#static final String LIST_URL = \"$LICENCE_URL\";#" "$L"
  sed -i "s#static final long RECHECK_MS = .*;#static final long RECHECK_MS = 0;#" "$L"
  sed -i 's#<application#<application android:usesCleartextTraffic="true"#' "$OUT/AndroidManifest.xml"
  grep -q "$LICENCE_URL" "$L"
fi
"$BT/aapt2" compile --dir res -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$AJ" --manifest "$OUT/AndroidManifest.xml" -A assets \
  --min-sdk-version 26 --target-sdk-version 34 --version-code "$VCODE" --version-name "$VNAME" "$OUT/res.zip"
javac --release 8 -Xlint:-options -cp "$AJ" -d "$OUT/classes" $(find "$OUT/src" -name '*.java')
"$BT/d8" --min-api 26 --lib "$AJ" --output "$OUT" $(find "$OUT/classes" -name '*.class')
(cd "$OUT" && zip -q -j base.apk classes.dex)
"$BT/zipalign" -f -p 4 "$OUT/base.apk" "$OUT/aligned.apk"
"$BT/apksigner" sign --ks jbkeygen.jks --ks-pass pass:jbpraise-keys --key-pass pass:jbpraise-keys --ks-key-alias jbkeys \
  --out "$OUT/${APKNAME:-eliguitarchords}.apk" "$OUT/aligned.apk"
"$BT/apksigner" verify --verbose "$OUT/${APKNAME:-eliguitarchords}.apk" | head -5
ls -la "$OUT/${APKNAME:-eliguitarchords}.apk"
