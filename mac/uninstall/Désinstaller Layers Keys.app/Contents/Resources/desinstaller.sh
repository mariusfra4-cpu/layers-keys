#!/bin/bash
# Désinstalle Layers Keys : application, plugin VST3, plugin Audio Unit.
# Demande avant d'effacer les réglages, sons et presets (jamais effacés sans accord).
# Options :  --silent  (sans fenêtres, pour les tests)   --data (efface aussi les données)
#            --home <dossier>  (dossier de l'utilisateur)   --remove (étape interne, en administrateur)
SELF="$0"
MODE=gui; REMOVE=0; DATA=0; UHOME="$HOME"
while [ $# -gt 0 ]; do
  case "$1" in
    --silent) MODE=silent ;;
    --remove) REMOVE=1 ;;
    --data) DATA=1 ;;
    --home) shift; UHOME="$1" ;;
  esac
  shift
done

faire_le_menage() {
  pkill -TERM -x LayersKeys 2>/dev/null; sleep 1; pkill -KILL -x LayersKeys 2>/dev/null
  rm -rf "/Applications/Layers Keys.app"
  rm -rf "/Library/Audio/Plug-Ins/VST3/LayersKeys.vst3"
  rm -rf "/Library/Audio/Plug-Ins/Components/LayersKeys.component"
  for id in com.jbpraise.layerskeys.app com.jbpraise.layerskeys.vst3 com.jbpraise.layerskeys.au com.jbpraise.layerskeys.pkg; do
    pkgutil --forget "$id" >/dev/null 2>&1
  done
  killall -9 AudioComponentRegistrar 2>/dev/null
  if [ "$DATA" = 1 ]; then
    rm -rf "$UHOME/Library/Application Support/LayersKeys"
  fi
  # le désinstallateur s'efface en dernier
  rm -rf "/Applications/Désinstaller Layers Keys.app"
}

if [ "$REMOVE" = 1 ] || [ "$MODE" = silent ]; then
  faire_le_menage
  [ "$MODE" = silent ] && echo "Layers Keys désinstallé."
  exit 0
fi

# --- mode normal, avec fenêtres
osascript -e 'display dialog "Voulez-vous désinstaller Layers Keys ?\n\nL’application et les plugins VST3 et Audio Unit seront retirés de votre Mac." buttons {"Annuler", "Désinstaller"} default button "Annuler" cancel button "Annuler" with title "Désinstaller Layers Keys" with icon note' >/dev/null 2>&1 || exit 0

EXTRA=""
if [ -d "$UHOME/Library/Application Support/LayersKeys" ]; then
  R=$(osascript -e 'button returned of (display dialog "Voulez-vous aussi effacer vos réglages, sons et presets Layers Keys ?\n\nChoisissez « Conserver » si vous comptez réinstaller le logiciel." buttons {"Conserver", "Effacer"} default button "Conserver" with title "Désinstaller Layers Keys" with icon caution)' 2>/dev/null)
  [ "$R" = "Effacer" ] && EXTRA=' & " --data"'
fi

osascript >/dev/null 2>&1 <<OSA
do shell script (quoted form of "$SELF") & " --remove --home " & quoted form of "$UHOME"$EXTRA with administrator privileges
OSA
if [ $? -eq 0 ]; then
  osascript -e 'display dialog "Layers Keys a été désinstallé.\n\nBy JB Praise — for church musician." buttons {"OK"} default button "OK" with title "Désinstaller Layers Keys"' >/dev/null 2>&1
else
  osascript -e 'display dialog "La désinstallation a été annulée." buttons {"OK"} default button "OK" with title "Désinstaller Layers Keys" with icon caution' >/dev/null 2>&1
fi
exit 0
