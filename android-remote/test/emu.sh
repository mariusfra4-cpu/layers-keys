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
# taille de l'écran (l'application est en paysage)
read W H < <(adb shell wm size | sed -E 's/.* ([0-9]+)x([0-9]+).*/\1 \2/')
if [ "$W" -lt "$H" ]; then t=$W; W=$H; H=$t; fi
log "écran : ${W}x${H}"
TOP=$(( H * 12 / 100 ))
[ $TOP -lt 140 ] && TOP=140
shot 1_mixeur
# bouton M de la piste 1 : bande 1, rangée des boutons
SW=$(( (W - 26) / 9 ))
adb shell input tap $(( 13 + SW / 6 )) $(( TOP + 90 + H / 20 ))
sleep 1
# fader de la piste 2 : glisser vers le haut
FX=$(( 13 + SW + SW / 2 ))
adb shell input swipe $FX $(( H - 200 )) $FX $(( H / 2 )) 600
sleep 1
shot 2_mixeur_apres_gestes
# onglet LIVE
adb shell input tap $(( W * 30 / 100 + W * 15 / 200 + W * 15 / 100 + 20 )) $(( TOP / 2 ))
sleep 1
shot 3_live
# onglet RÉGLAGES
adb shell input tap $(( W * 30 / 100 + 2 * (W * 15 / 100 + 20) + W * 15 / 200 )) $(( TOP / 2 ))
sleep 1
shot 4_reglages
if adb shell pidof com.jbpraise.layersremote >/dev/null; then
  log "application en marche : OK"
else
  log "application arrêtée : PLANTAGE"
  adb logcat -d | grep -iE 'AndroidRuntime|FATAL|layersremote' | tail -60 >> "$OUT/03_emulateur.txt"
  exit 1
fi
adb logcat -d | grep -iE 'AndroidRuntime|FATAL' | tail -20 >> "$OUT/03_emulateur.txt"
exit 0
