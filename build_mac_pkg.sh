#!/bin/bash
# Assemble l'installateur Mac « Layers-Keys-1.1.0.pkg » : assistant en français avec choix des composants
# (application, plugin VST3, plugin Audio Unit). À lancer sur un Mac après build_mac.sh, build_mac_vst3.sh et build_mac_au.sh.
set -e
cd "$(dirname "$0")"
[ "$(uname)" = Darwin ] || { echo "Ce script doit être lancé sur un Mac."; exit 1; }
VER=1.1.0
OUT=dist/mac
P="$OUT/pk"
rm -rf "$P" && mkdir -p "$P/app/Applications" "$P/vst3/Library/Audio/Plug-Ins/VST3" "$P/au/Library/Audio/Plug-Ins/Components"
cp -R "$OUT/app/Layers Keys.app" "$P/app/Applications/"
cp -R "mac/uninstall/Désinstaller Layers Keys.app" "$P/app/Applications/"
chmod +x "$P/app/Applications/Désinstaller Layers Keys.app/Contents/MacOS/Desinstaller" \
         "$P/app/Applications/Désinstaller Layers Keys.app/Contents/Resources/desinstaller.sh"
cp -R "$OUT/LayersKeys.vst3" "$P/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$OUT/LayersKeys.component" "$P/au/Library/Audio/Plug-Ins/Components/"

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
    --install-location / --scripts "mac/pkg/$1" "$P/LayersKeys_$1.pkg"
}
mk app com.jbpraise.layerskeys.app
mk vst3 com.jbpraise.layerskeys.vst3
mk au com.jbpraise.layerskeys.au

mkdir -p "$OUT/res"
cp mac/pkgres/* "$OUT/res/"
cat > "$OUT/distribution.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Layers Keys</title>
  <organization>com.jbpraise</organization>
  <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
  <domains enable_anywhere="false" enable_localSystem="true" enable_currentUserHome="false"/>
  <background file="background.png" mime-type="image/png" alignment="bottomleft" scaling="none"/>
  <background-darkAqua file="background-dark.png" mime-type="image/png" alignment="bottomleft" scaling="none"/>
  <welcome file="welcome.html" mime-type="text/html"/>
  <conclusion file="conclusion.html" mime-type="text/html"/>
  <pkg-ref id="com.jbpraise.layerskeys.app"/>
  <pkg-ref id="com.jbpraise.layerskeys.vst3"/>
  <pkg-ref id="com.jbpraise.layerskeys.au"/>
  <choices-outline>
    <line choice="app"/>
    <line choice="vst3"/>
    <line choice="au"/>
  </choices-outline>
  <choice id="app" title="Layers Keys (obligatoire)" description="Le logiciel Layers Keys, installé dans le dossier Applications, avec « Désinstaller Layers Keys »." enabled="false" selected="true">
    <pkg-ref id="com.jbpraise.layerskeys.app"/>
  </choice>
  <choice id="vst3" title="Plugin VST3 (Ableton Live et autres)" description="Le plugin VST3, installé dans /Library/Audio/Plug-Ins/VST3." start_selected="true">
    <pkg-ref id="com.jbpraise.layerskeys.vst3"/>
  </choice>
  <choice id="au" title="Plugin Audio Unit (Logic Pro, GarageBand)" description="Le plugin Audio Unit, installé dans /Library/Audio/Plug-Ins/Components." start_selected="true">
    <pkg-ref id="com.jbpraise.layerskeys.au"/>
  </choice>
  <pkg-ref id="com.jbpraise.layerskeys.app" version="1.1.0" onConclusion="none">LayersKeys_app.pkg</pkg-ref>
  <pkg-ref id="com.jbpraise.layerskeys.vst3" version="1.1.0" onConclusion="none">LayersKeys_vst3.pkg</pkg-ref>
  <pkg-ref id="com.jbpraise.layerskeys.au" version="1.1.0" onConclusion="none">LayersKeys_au.pkg</pkg-ref>
</installer-gui-script>
XML
productbuild --distribution "$OUT/distribution.xml" --resources "$OUT/res" --package-path "$P" dist/Layers-Keys-$VER.pkg
cp dist/Layers-Keys-$VER.pkg dist/LayersKeys_Mac_Installer.pkg
rm -rf "$P"
ls -la dist/Layers-Keys-$VER.pkg
