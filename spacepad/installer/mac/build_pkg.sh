#!/bin/bash
# Assemble l'installateur Mac « Spacepad-<version>.pkg » : assistant en français avec choix des composants
# (application, plugin Audio Unit, plugin VST3). À lancer sur un Mac.
# Usage : installer/mac/build_pkg.sh <dossier contenant Spacepad.app, Spacepad.vst3, Spacepad.component> [version]
set -e
cd "$(dirname "$0")"
M="$(pwd)"
[ "$(uname)" = Darwin ] || { echo "Ce script doit être lancé sur un Mac."; exit 1; }
IN="$(cd "$1" && pwd)"
VER="${2:-1.0.0}"
OUT="$M/../../dist"
mkdir -p "$OUT"
P="$(mktemp -d)"
mkdir -p "$P/app/Applications" "$P/vst3/Library/Audio/Plug-Ins/VST3" "$P/au/Library/Audio/Plug-Ins/Components"

# icône du désinstallateur (à partir de l'icône de Spacepad)
U="$P/app/Applications/Désinstaller Spacepad.app"
cp -R "$M/uninstall/Désinstaller Spacepad.app" "$P/app/Applications/"
ICS="$P/icon.iconset"; mkdir -p "$ICS"
for s in 16 32 128 256 512; do
  sips -z $s $s "$M/../../resources/icon.png" --out "$ICS/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) "$M/../../resources/icon.png" --out "$ICS/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICS" -o "$U/Contents/Resources/icon.icns"
chmod +x "$U/Contents/MacOS/Desinstaller" "$U/Contents/Resources/desinstaller.sh"

cp -R "$IN/Spacepad.app" "$P/app/Applications/"
cp -R "$IN/Spacepad.vst3" "$P/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$IN/Spacepad.component" "$P/au/Library/Audio/Plug-Ins/Components/"

# un paquet par composant ; BundleIsRelocatable=false pour que macOS n'installe jamais ailleurs que prévu
mk() { # nom identifiant
  pkgbuild --analyze --root "$P/$1" "$P/$1.plist" >/dev/null
  i=0
  while /usr/libexec/PlistBuddy -c "Print :$i:RootRelativeBundlePath" "$P/$1.plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$P/$1.plist" 2>/dev/null || \
      /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$P/$1.plist"
    i=$((i+1))
  done
  pkgbuild --root "$P/$1" --component-plist "$P/$1.plist" --identifier "$2" --version "$VER" \
    --install-location / --scripts "$M/scripts/$1" "$P/Spacepad_$1.pkg"
}
mk app com.jbpraise.spacepad.app
mk vst3 com.jbpraise.spacepad.vst3
mk au com.jbpraise.spacepad.au

cat > "$P/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>SP_TITLE</title>
  <organization>com.jbpraise</organization>
  <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
  <domains enable_anywhere="false" enable_localSystem="true" enable_currentUserHome="false"/>
  <background file="background.png" mime-type="image/png" alignment="bottomleft" scaling="none"/>
  <background-darkAqua file="background-dark.png" mime-type="image/png" alignment="bottomleft" scaling="none"/>
  <welcome file="welcome.html" mime-type="text/html"/>
  <conclusion file="conclusion.html" mime-type="text/html"/>
  <pkg-ref id="com.jbpraise.spacepad.app"/>
  <pkg-ref id="com.jbpraise.spacepad.au"/>
  <pkg-ref id="com.jbpraise.spacepad.vst3"/>
  <choices-outline>
    <line choice="app"/>
    <line choice="au"/>
    <line choice="vst3"/>
  </choices-outline>
  <choice id="app" title="SP_APP_T" description="SP_APP_D" enabled="false" selected="true">
    <pkg-ref id="com.jbpraise.spacepad.app"/>
  </choice>
  <choice id="au" title="SP_AU_T" description="SP_AU_D" start_selected="true">
    <pkg-ref id="com.jbpraise.spacepad.au"/>
  </choice>
  <choice id="vst3" title="SP_VST_T" description="SP_VST_D" start_selected="true">
    <pkg-ref id="com.jbpraise.spacepad.vst3"/>
  </choice>
  <pkg-ref id="com.jbpraise.spacepad.app" version="$VER" onConclusion="none">Spacepad_app.pkg</pkg-ref>
  <pkg-ref id="com.jbpraise.spacepad.au" version="$VER" onConclusion="none">Spacepad_au.pkg</pkg-ref>
  <pkg-ref id="com.jbpraise.spacepad.vst3" version="$VER" onConclusion="none">Spacepad_vst3.pkg</pkg-ref>
</installer-gui-script>
XML
productbuild --distribution "$P/distribution.xml" --resources "$M/res" --package-path "$P" "$OUT/Spacepad-$VER.pkg"
rm -rf "$P"
ls -la "$OUT/Spacepad-$VER.pkg"
