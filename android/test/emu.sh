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
adb("shell", "input", "keyevent", "KEYCODE_BACK"); time.sleep(1)  # le clavier cache le bouton
r = dump(); tap(r, text="DÉVERROUILLER"); time.sleep(6)
r = dump(); shot(out + "/2_mauvais_mdp.png")
check("incorrect" in alltext(r), "mauvais mot de passe refusé")
# bon mot de passe
tap(r, cls="EditText"); adb("shell", "input", "keyevent", "KEYCODE_MOVE_END")
for _ in range(14): adb("shell", "input", "keyevent", "KEYCODE_DEL")
adb("shell", "input", "text", "TESTTESTTEST")
adb("shell", "input", "keyevent", "KEYCODE_BACK"); time.sleep(1)
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
# African Loops Pro : choix du logiciel, déverrouillage, clé JBA
r = dump(); tap(r, text="African Loops Pro"); time.sleep(2)
r = dump(); shot(out + "/5_african_verrouille.png")
check("DÉVERROUILLER" in alltext(r), "African Loops : écran de déverrouillage")
tap(r, cls="EditText"); adb("shell", "input", "text", "TESTTESTTEST")
adb("shell", "input", "keyevent", "KEYCODE_BACK"); time.sleep(1)
r = dump(); tap(r, text="DÉVERROUILLER"); time.sleep(8)
r = dump(); tap(r, cls="EditText"); adb("shell", "input", "text", "JBA7F3C91D24B8EA05D")
adb("shell", "input", "keyevent", "KEYCODE_BACK"); time.sleep(1)
r = dump(); tap(r, text="GÉNÉRER LA CLÉ"); time.sleep(3)
r = dump(); shot(out + "/6_african_cle.png")
t = alltext(r)
check("2PVJM63G-KRRHQHXJ-34HS3UFF-YI4CXY7G-N5YXNCWH-CJBRLSRV-QUL4RNX6-V77XRIAN-5RHL435L-NPMVW7WT-SPNXRRYP-LXPQYXP5-CH4TICY" in t,
      "African Loops : la clé affichée est la bonne")
# Spacepad
r = dump(); tap(r, text="Spacepad"); time.sleep(2)
r = dump(); shot(out + "/7_spacepad_verrouille.png")
tap(r, cls="EditText"); adb("shell", "input", "text", "TESTTESTTEST")
adb("shell", "input", "keyevent", "KEYCODE_BACK"); time.sleep(1)
r = dump(); tap(r, text="DÉVERROUILLER"); time.sleep(8)
r = dump(); tap(r, cls="EditText"); adb("shell", "input", "text", "JBS1234ABCD5678EF90")
adb("shell", "input", "keyevent", "KEYCODE_BACK"); time.sleep(1)
r = dump(); tap(r, text="GÉNÉRER LA CLÉ"); time.sleep(3)
r = dump(); shot(out + "/8_spacepad_cle.png")
check("NKIBGX2A-QB7R4NJP-4G7AFYOJ-F4WA5CUL-UWWV5IES-6LBIZLCD-7QSTC4IV-SHTE7BFN-SNYPRSLV-4AT7HKBT-NTOXKAKT-UJ4KXFUD-AJEZEAQ" in alltext(r),
      "Spacepad : la clé affichée est la bonne")
sys.exit(0 if ok else 1)
PY
RES=$?
adb logcat -d -s AndroidRuntime:E > "$OUT/crash.txt"
if [ -s "$OUT/crash.txt" ]; then echo "PLANTAGE détecté"; RES=1; fi
echo "résultat: $RES" > "$OUT/resultat.txt"
exit $RES
