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

# langue : celle choisie dans Spacepad, sinon celle du Mac
LG=$(sed -n 's/.*"language"[^"]*"\([a-z][a-z]\)".*/\1/p' "$SHARED/settings.json" 2>/dev/null | head -1)
[ -z "$LG" ] && LG=$(defaults read -g AppleLanguages 2>/dev/null | sed -n '2s/[^a-z]*\([a-z][a-z]\).*/\1/p')
case "$LG" in
  en) T_TITLE="Uninstall Spacepad"; T_ASK="Do you want to uninstall Spacepad?\n\nThe app and the VST3 and Audio Unit plugins will be removed from your Mac. Your sounds are not touched."
      B_CANCEL="Cancel"; B_GO="Uninstall"; T_DATA="Do you also want to delete your Spacepad library list?\n\nYour sound files are never deleted. Choose “Keep” if you plan to reinstall."
      B_KEEP="Keep"; B_DEL="Delete"; T_DONE="Spacepad has been uninstalled.\n\nSpacepad by JB Praise."; T_STOP="Uninstall was cancelled." ;;
  es) T_TITLE="Desinstalar Spacepad"; T_ASK="¿Quieres desinstalar Spacepad?\n\nLa aplicación y los plugins VST3 y Audio Unit se quitarán de tu Mac. Tus sonidos no se tocan."
      B_CANCEL="Cancelar"; B_GO="Desinstalar"; T_DATA="¿Quieres borrar también la lista de tus librerías de Spacepad?\n\nTus archivos de sonido nunca se borran. Elige «Conservar» si piensas reinstalar."
      B_KEEP="Conservar"; B_DEL="Borrar"; T_DONE="Spacepad se ha desinstalado.\n\nSpacepad by JB Praise."; T_STOP="La desinstalación se canceló." ;;
  *)  T_TITLE="Désinstaller Spacepad"; T_ASK="Voulez-vous désinstaller Spacepad ?\n\nL’application et les plugins VST3 et Audio Unit seront retirés de votre Mac. Vos sons ne sont pas touchés."
      B_CANCEL="Annuler"; B_GO="Désinstaller"; T_DATA="Voulez-vous aussi effacer la liste de vos librairies Spacepad ?\n\nVos fichiers de sons ne sont jamais effacés. Choisissez « Conserver » si vous comptez réinstaller."
      B_KEEP="Conserver"; B_DEL="Effacer"; T_DONE="Spacepad a été désinstallé.\n\nSpacepad by JB Praise."; T_STOP="La désinstallation a été annulée." ;;
esac

osascript -e "display dialog \"$T_ASK\" buttons {\"$B_CANCEL\", \"$B_GO\"} default button \"$B_CANCEL\" cancel button \"$B_CANCEL\" with title \"$T_TITLE\" with icon note" >/dev/null 2>&1 || exit 0

EXTRA=""
if [ -f "$SHARED/libraries.json" ]; then
  R=$(osascript -e "button returned of (display dialog \"$T_DATA\" buttons {\"$B_KEEP\", \"$B_DEL\"} default button \"$B_KEEP\" with title \"$T_TITLE\" with icon caution)" 2>/dev/null)
  [ "$R" = "$B_DEL" ] && EXTRA=' & " --data"'
fi

osascript >/dev/null 2>&1 <<OSA
do shell script (quoted form of "$SELF") & " --remove"$EXTRA with administrator privileges
OSA
if [ $? -eq 0 ]; then
  osascript -e "display dialog \"$T_DONE\" buttons {\"OK\"} default button \"OK\" with title \"$T_TITLE\"" >/dev/null 2>&1
else
  osascript -e "display dialog \"$T_STOP\" buttons {\"OK\"} default button \"OK\" with title \"$T_TITLE\" with icon caution" >/dev/null 2>&1
fi
exit 0
