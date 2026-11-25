#!/usr/bin/env python3
"""Test d'AccordLive dans l'émulateur : démarrage, navigation, analyse d'une vidéo partagée.
Usage : emu.py <dossier sortie> <apk> <video mp4>"""
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

OUT, APK, VIDEO = sys.argv[1], sys.argv[2], sys.argv[3]
PKG = 'com.jbpraise.guitare'
ok = True


def sh(cmd, check=False, timeout=60):
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        print('  (délai dépassé : %s)' % cmd)
        return ''
    if check and r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit('échec : ' + cmd)
    return r.stdout


def shot(name):
    subprocess.run(f'adb exec-out screencap -p > {OUT}/{name}.png', shell=True)


def nodes():
    sh('adb shell uiautomator dump /sdcard/ui.xml', timeout=25)
    xml = sh('adb shell cat /sdcard/ui.xml')
    try:
        root = ET.fromstring(xml[xml.index('<?xml'):] if '<?xml' in xml else xml)
    except Exception:
        return []
    res = []
    for n in root.iter('node'):
        t = (n.get('text') or '') + ' ' + (n.get('content-desc') or '')
        b = re.findall(r'\d+', n.get('bounds') or '')
        if len(b) == 4:
            res.append((t.strip(), [int(x) for x in b]))
    return res


def tap_text(txt, last=True):
    found = [b for t, b in nodes() if t == txt or t.endswith(' ' + txt) or t.startswith(txt)]
    if not found:
        print('  (introuvable : %s)' % txt)
        return False
    b = found[-1] if last else found[0]
    sh(f'adb shell input tap {(b[0] + b[2]) // 2} {(b[1] + b[3]) // 2}')
    return True


def all_text():
    return ' | '.join(t for t, _ in nodes() if t)


sh(f'adb install -r -g "{APK}"', check=True)
sh('adb logcat -c')
sh(f'adb shell am start -W -n {PKG}/.MainActivity', check=True)
time.sleep(10)
shot('1-demarrage')
txt = all_text()
print('Écran de démarrage :', txt[:400])
time.sleep(3)
txt = txt or all_text()
if 'Écouter' not in txt and 'Analyse' not in txt:
    print('ERREUR : la page ne semble pas chargée')
    ok = False

# vidéo partagée vers l'application (placée dans le dossier propre à l'appli, lisible sans permission)
DEST = f'/sdcard/Android/data/{PKG}/files/accordlive_test.mp4'
sh(f'adb shell mkdir -p /sdcard/Android/data/{PKG}/files')
sh(f'adb push "{VIDEO}" {DEST}', check=True)
vid = 'file://' + DEST
if vid:
    sh(f'adb shell am start -a android.intent.action.SEND -t video/mp4 --eu android.intent.extra.STREAM {vid} -n {PKG}/.MainActivity')
    time.sleep(25)
    shot('4-video-analysee')
    txt = all_text()
    print('Après analyse :', txt[:1500])
    if 'Tonalité' not in txt:
        print('ERREUR : pas de résultat d’analyse')
        ok = False
    # lecture de la vidéo : le compteur de temps doit avancer
    pos = [b for t, b in nodes() if t == 'play' or t.startswith('play')]
    el = -1
    if pos:
        b = pos[0]
        sh('adb logcat -c')
        sh(f'adb shell input tap {(b[0] + b[2]) // 2} {(b[1] + b[3]) // 2}')
        time.sleep(8)
        shot('4b-lecture')
        lg = sh('adb logcat -d -s AccordLive:V')
        ts = [float(x) for x in re.findall(r'lecture t=([\d.]+)', lg)]
        el = max(ts) if ts else -1
        print('Lecture vidéo : temps atteint', el, 's', '| erreurs :', re.findall(r'lecture impossible[^\n]*', lg))
    if el < 2:
        print('ERREUR : la vidéo ne se lit pas')
        ok = False
    # relance l'appli (une vidéo en lecture empêche la lecture de l'interface par uiautomator)
    sh(f'adb shell am force-stop {PKG}')
    sh(f'adb shell am start -W -n {PKG}/.MainActivity'); time.sleep(6)
else:
    pass

for i, tab in enumerate(['Accords', 'Progressions', 'Théorie', 'Exercices', 'Outils']):
    if tap_text(tab):
        time.sleep(2)
        shot(f'2-{i}-{tab}')

# micro : démarre la détection (permission accordée par install -g)
tap_text('Analyse'); time.sleep(1)
tap_text('▶ Écouter'); time.sleep(4)
shot('3-micro')
print('Micro :', all_text()[:300])
tap_text('■ Arrêter'); time.sleep(1)

log = sh(f'adb logcat -d -s AccordLive:V chromium:E AndroidRuntime:E ActivityManager:W')
open(f'{OUT}/logcat.txt', 'w').write(log)
print('--- journal ---')
print(log[-4000:])
if 'Uncaught' in log or 'js ERROR' in log:
    print('ERREUR : erreurs JavaScript dans le journal')
    ok = False
print('RÉSULTAT :', 'OK' if ok else 'ÉCHEC')
sys.exit(0 if ok else 1)
