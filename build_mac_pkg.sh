#!/bin/bash
# Assemble l'installateur Mac « LayersKeys_Mac_Installer.pkg » (application + VST3 + Audio Unit).
# À lancer sur un Mac après build_mac.sh, build_mac_vst3.sh et build_mac_au.sh.
set -e
cd "$(dirname "$0")"
[ "$(uname)" = Darwin ] || { echo "Ce script doit être lancé sur un Mac."; exit 1; }
OUT=dist/mac
S="$OUT/pkgroot"
rm -rf "$S" && mkdir -p "$S/Applications" "$S/Library/Audio/Plug-Ins/VST3" "$S/Library/Audio/Plug-Ins/Components"
cp -R "$OUT/app/Layers Keys.app" "$S/Applications/"
cp -R "$OUT/LayersKeys.vst3" "$S/Library/Audio/Plug-Ins/VST3/"
cp -R "$OUT/LayersKeys.component" "$S/Library/Audio/Plug-Ins/Components/"
pkgbuild --root "$S" --identifier com.jbpraise.layerskeys.pkg --version 1.0.0 --install-location / \
  --scripts mac/pkg "$OUT/LayersKeys_component.pkg"
cat > "$OUT/distribution.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Layers Keys · By JB Praise</title>
  <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <pkg-ref id="com.jbpraise.layerskeys.pkg"/>
  <choices-outline><line choice="default"><line choice="com.jbpraise.layerskeys.pkg"/></line></choices-outline>
  <choice id="default"/>
  <choice id="com.jbpraise.layerskeys.pkg" visible="false"><pkg-ref id="com.jbpraise.layerskeys.pkg"/></choice>
  <pkg-ref id="com.jbpraise.layerskeys.pkg" version="1.0.0" onConclusion="none">LayersKeys_component.pkg</pkg-ref>
</installer-gui-script>
XML
productbuild --distribution "$OUT/distribution.xml" --package-path "$OUT" dist/LayersKeys_Mac_Installer.pkg
rm -rf "$S"
ls -la dist/LayersKeys_Mac_Installer.pkg
