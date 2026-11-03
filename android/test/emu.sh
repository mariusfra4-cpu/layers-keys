#!/bin/bash
# Contrôle automatique dans un émulateur Android : installation, mauvais mot de passe, bon mot de passe, génération de clé.
cd "$(dirname "$0")"
OUT=${OUT:-/tmp/emu}; mkdir -p "$OUT"
APK=$1; EXPECT=$2
set -x
adb install -r "$APK" || exit 1
adb logcat -c
adb shell am start -n com.jbpraise.keygen/.MainActivity
sleep 4
python3 - "$OUT" "$EXPECT" <<'PY'
import sys, time
sys.path.insert(0, ".")
from ui import *
out, expect = sys.argv[1], sys.argv[2]
ok = True
def check(c, msg):
    global ok
    print(("OK   " if c else "ECHEC ") + msg)
    ok = ok and c
r = dump(); shot(out + "/1_deverrouillage.png")
check("DÉVERROUILLER" in alltext(r), "écran de déverrouillage affiché")
# mauvais mot de passe
tap(r, cls="EditText"); adb("shell", "input", "text", "AAAABBBBCCCC")
r = dump(); tap(r, text="DÉVERROUILLER"); time.sleep(6)
r = dump(); shot(out + "/2_mauvais_mdp.png")
check("incorrect" in alltext(r), "mauvais mot de passe refusé")
# bon mot de passe
tap(r, cls="EditText"); adb("shell", "input", "keyevent", "KEYCODE_MOVE_END")
for _ in range(14): adb("shell", "input", "keyevent", "KEYCODE_DEL")
adb("shell", "input", "text", "TESTTESTTEST")
r = dump(); tap(r, text="DÉVERROUILLER"); time.sleep(8)
r = dump(); shot(out + "/3_principal.png")
check("GÉNÉRER LA CLÉ" in alltext(r), "écran principal après déverrouillage")
# génération
tap(r, cls="EditText"); adb("shell", "input", "text", "JBK1A2B3C4D5E6F7A8B")
r = dump(); adb("shell", "input", "keyevent", "KEYCODE_BACK"); time.sleep(1)
r = dump(); tap(r, text="GÉNÉRER LA CLÉ"); time.sleep(3)
r = dump(); shot(out + "/4_cle.png")
t = alltext(r)
check(expect in t, "la clé affichée est la bonne : " + expect)
check("JBK-1A2B-3C4D-5E6F-7A8B" in t, "code normalisé")
sys.exit(0 if ok else 1)
PY
RES=$?
adb logcat -d -s AndroidRuntime:E > "$OUT/crash.txt"
if [ -s "$OUT/crash.txt" ]; then echo "PLANTAGE détecté"; RES=1; fi
echo "résultat: $RES" > "$OUT/resultat.txt"
exit $RES
