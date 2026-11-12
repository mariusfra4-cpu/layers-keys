#!/bin/bash
# Essai de la télécommande dans l'émulateur : installation, démarrage, captures des 3 pages,
# appui sur M de la piste 1, glissement d'un fader, vérification que l'application ne plante pas.
APK=$1
OUT=$2
mkdir -p "$OUT"
log() { echo "$@" | tee -a "$OUT/03_emulateur.txt"; }
adb wait-for-device
adb shell settings put system accelerometer_rotation 0
adb install -r "$APK" 2>&1 | tee -a "$OUT/03_emulateur.txt"
adb shell am start -W -n com.jbpraise.layersremote/.MainActivity 2>&1 | tee -a "$OUT/03_emulateur.txt"
sleep 6
shot() { adb exec-out screencap -p > "$OUT/$1.png"; log "capture $1"; }
# taille de l'écran (l'application est en paysage) et densité : mêmes calculs que RemoteView.java
read W H < <(adb shell wm size | sed -E 's/.* ([0-9]+)x([0-9]+).*/\1 \2/')
if [ "$W" -lt "$H" ]; then t=$W; W=$H; H=$t; fi
DEN=$(adb shell wm density | sed -E 's/.* ([0-9]+).*/\1/' | tail -1)
dp() { echo $(( $1 * DEN / 160 )); }
log "écran : ${W}x${H}, densité $DEN"
TOP=$(( H * 12 / 100 )); [ $TOP -lt $(dp 52) ] && TOP=$(dp 52)
tab() { echo $(( W * 30 / 100 + $1 * (W * 15 / 100 + $(dp 8)) + W * 15 / 200 )); }
SW=$(( (W - $(dp 10)) / 9 ))
BTOP=$(( TOP + $(dp 36) )); BH=$(( H / 10 )); [ $BH -lt $(dp 40) ] && BH=$(dp 40)
FTOP=$(( BTOP + BH + $(dp 12) )); FBOT=$(( H - $(dp 34) ))
shot 1_mixeur
# M de la piste 1 (premier tiers des boutons de la bande 1)
SX=$(dp 5); BW=$(( (SW - $(dp 6) - $(dp 16)) / 3 ))
adb shell input tap $(( SX + $(dp 4) + BW / 2 )) $(( BTOP + BH / 2 ))
sleep 1
# fader de la piste 2 : on le descend franchement (glisser vers le bas)
FX=$(( SX + SW + (SW - $(dp 6)) / 2 ))
adb shell input swipe $FX $(( (FTOP + FBOT) / 2 )) $FX $(( FBOT - $(dp 4) )) 500
sleep 1
shot 2_mixeur_apres_gestes
adb shell input tap $(tab 1) $(( TOP / 2 ))
sleep 1
shot 3_live
# Leslie (colonne de droite, premier bouton) : passe en RAPIDE
adb shell input tap $(( W * 58 / 100 + $(dp 10) + (W - W * 58 / 100 - $(dp 24)) / 2 )) $(( TOP + $(dp 12) + $(dp 20) ))
sleep 1
shot 4_live_leslie
adb shell input tap $(tab 2) $(( TOP / 2 ))
sleep 1
shot 5_reglages
if adb shell pidof com.jbpraise.layersremote >/dev/null; then
  log "application en marche : OK"
else
  log "application arrêtée : PLANTAGE"
  adb logcat -d | grep -iE 'AndroidRuntime|FATAL|layersremote' | tail -60 >> "$OUT/03_emulateur.txt"
  exit 1
fi
adb logcat -d | grep -iE 'AndroidRuntime|FATAL' | tail -20 >> "$OUT/03_emulateur.txt"
exit 0
