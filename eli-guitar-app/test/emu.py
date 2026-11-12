# Contrôle automatique d'Eli Guitar Chords dans un émulateur Android :
# installation, écran du code, saisie du code de test, onglets visibles, code mémorisé après redémarrage.
import re, subprocess, sys, time

OUT, APK = sys.argv[1], sys.argv[2]
PKG = "com.elipraise.guitarchords"
ok = True

def adb(*a):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=True).stdout

def check(c, msg):
    global ok
    print(("OK    " if c else "ECHEC ") + msg, flush=True)
    ok = ok and bool(c)

def shot(name):
    b = subprocess.run(["adb", "exec-out", "screencap", "-p"], capture_output=True).stdout
    open(f"{OUT}/{name}.png", "wb").write(b)

def dump():
    for _ in range(3):
        adb("shell", "uiautomator", "dump", "/sdcard/ui.xml")
        x = adb("shell", "cat", "/sdcard/ui.xml")
        if "<hierarchy" in x:
            return x
        time.sleep(2)
    return ""

def wait_for(pred, secs=40):
    end = time.time() + secs
    while time.time() < end:
        x = dump()
        if pred(x):
            return x
        time.sleep(2)
    return dump()

def start():
    adb("shell", "am", "force-stop", PKG)
    adb("shell", "am", "start", "-n", PKG + "/.MainActivity")

print(adb("install", "-r", "-g", APK).strip())
adb("logcat", "-c")
start()
x = wait_for(lambda x: "EditText" in x or "Analyse" in x)
open(f"{OUT}/ui-verrou.xml", "w").write(x)
shot("1-verrou")
m = re.search(r'class="android.widget.EditText"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', x)
check(m, "écran du code d'accès affiché")
if m:
    a, b, c, d = map(int, m.groups())
    adb("shell", "input", "tap", str((a + c) // 2), str((b + d) // 2))
    time.sleep(1)
    adb("shell", "input", "text", "ELI-TEST-2026")
    time.sleep(1)
    adb("shell", "input", "keyevent", "66")
x = wait_for(lambda x: "Analyse" in x and "EditText" not in x.split("Analyse")[0][-2000:], 20)
open(f"{OUT}/ui-ouvert.xml", "w").write(x)
shot("2-ouvert")
check("Accords" in x and "Tonalit" in x, "code accepté, l'application s'ouvre (onglets visibles)")
adb("shell", "input", "keyevent", "4")  # retour : ne doit pas planter
time.sleep(2)
start()
x = wait_for(lambda x: "Accords" in x, 30)
shot("3-redemarrage")
check("Accords" in x and 'class="android.widget.EditText"' not in x.split("Accords")[0], "code mémorisé après redémarrage")
log = adb("logcat", "-d")
open(f"{OUT}/logcat.txt", "w").write(log)
check("FATAL EXCEPTION" not in log, "aucun plantage")
print("RESULTAT", "OK" if ok else "ECHEC")
sys.exit(0 if ok else 1)
