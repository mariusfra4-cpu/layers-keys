#!/bin/bash
# Désinstalle Spacepad : application, plugin VST3, plugin Audio Unit.
# Les sons ne sont jamais effacés. La liste des librairies n'est effacée qu'avec votre accord.
# Options :  --silent  (sans fenêtres, pour les tests)   --data (efface aussi la liste des librairies)
#            --remove (étape interne, en administrateur)
SELF="$0"
MODE=gui; REMOVE=0; DATA=0
SHARED="/Library/Application Support/JB Praise/Spacepad"
while [ $# -gt 0 ]; do
  case "$1" in
    --silent) MODE=silent ;;
    --remove) REMOVE=1 ;;
    --data) DATA=1 ;;
  esac
  shift
done

faire_le_menage() {
  pkill -TERM -x Spacepad 2>/dev/null; sleep 1; pkill -KILL -x Spacepad 2>/dev/null
  rm -rf "/Applications/Spacepad.app"
  rm -rf "/Library/Audio/Plug-Ins/VST3/Spacepad.vst3"
  rm -rf "/Library/Audio/Plug-Ins/Components/Spacepad.component"
  for id in com.jbpraise.spacepad.app com.jbpraise.spacepad.vst3 com.jbpraise.spacepad.au; do
    pkgutil --forget "$id" >/dev/null 2>&1
  done
  killall -9 AudioComponentRegistrar 2>/dev/null
  if [ "$DATA" = 1 ]; then
    rm -rf "$SHARED"
  fi
  # le désinstallateur s'efface en dernier
  rm -rf "/Applications/Désinstaller Spacepad.app"
}

if [ "$REMOVE" = 1 ] || [ "$MODE" = silent ]; then
  faire_le_menage
  [ "$MODE" = silent ] && echo "Spacepad désinstallé."
  exit 0
fi

osascript -e 'display dialog "Voulez-vous désinstaller Spacepad ?\n\nL’application et les plugins VST3 et Audio Unit seront retirés de votre Mac. Vos sons ne sont pas touchés." buttons {"Annuler", "Désinstaller"} default button "Annuler" cancel button "Annuler" with title "Désinstaller Spacepad" with icon note' >/dev/null 2>&1 || exit 0

EXTRA=""
if [ -f "$SHARED/libraries.json" ]; then
  R=$(osascript -e 'button returned of (display dialog "Voulez-vous aussi effacer la liste de vos librairies Spacepad ?\n\nVos fichiers de sons ne sont jamais effacés. Choisissez « Conserver » si vous comptez réinstaller." buttons {"Conserver", "Effacer"} default button "Conserver" with title "Désinstaller Spacepad" with icon caution)' 2>/dev/null)
  [ "$R" = "Effacer" ] && EXTRA=' & " --data"'
fi

osascript >/dev/null 2>&1 <<OSA
do shell script (quoted form of "$SELF") & " --remove"$EXTRA with administrator privileges
OSA
if [ $? -eq 0 ]; then
  osascript -e 'display dialog "Spacepad a été désinstallé.\n\nSpacepad by JB Praise." buttons {"OK"} default button "OK" with title "Désinstaller Spacepad"' >/dev/null 2>&1
else
  osascript -e 'display dialog "La désinstallation a été annulée." buttons {"OK"} default button "OK" with title "Désinstaller Spacepad" with icon caution' >/dev/null 2>&1
fi
exit 0
