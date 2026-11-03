# Contrôle automatique de Layers Keys dans un émulateur Android.
import re, subprocess, sys, time, struct

OUT = sys.argv[1]
REAL, TEST = sys.argv[2], sys.argv[3]
ok = True

def adb(*a, text=True):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=text).stdout

def check(c, msg):
    global ok
    print(("OK    " if c else "ECHEC ") + msg, flush=True)
    ok = ok and bool(c)

def shot(name):
    b = subprocess.run(["adb", "exec-out", "screencap", "-p"], capture_output=True).stdout
    open(f"{OUT}/{name}.png", "wb").write(b)
    w, h = struct.unpack(">II", b[16:24])
    return w, h

def status():
    time.sleep(3.6)  # le moteur écrit son état toutes les 3 secondes
    out = adb("logcat", "-d", "-s", "LayersKeys:I")
    m = re.findall(r"état audio=(\d+) frames=(\d+) midi=(\d+) notes=(\d+)", out)
    return tuple(map(int, m[-1])) if m else None

def start(pkg="com.jbpraise.layerskeys"):
    adb("logcat", "-c")
    adb("shell", "am", "force-stop", pkg)
    adb("shell", "am", "start", "-n", pkg + "/.MainActivity")

class Scr:
    """passage coordonnées de l'image du logiciel -> pixels de l'écran"""
    def __init__(self, w, h, cw=1200, ch=744):
        s = min(w / cw, h / ch)
        self.l, self.t, self.s = (w - cw * s) / 2, (h - ch * s) / 2, s
    def xy(self, x, y):
        return int(self.l + x * self.s), int(self.t + y * self.s)
    def tap(self, x, y):
        a, b = self.xy(x, y)
        adb("shell", "input", "tap", str(a), str(b))
    def swipe(self, x1, y1, x2, y2, ms=400):
        a, b = self.xy(x1, y1); c, d = self.xy(x2, y2)
        adb("shell", "input", "swipe", str(a), str(b), str(c), str(d), str(ms))

# écran en paysage
adb("shell", "settings", "put", "system", "accelerometer_rotation", "0")
adb("shell", "settings", "put", "system", "user_rotation", "1")

# ---------------------------------------------------------------- 1. version distribuée : écran de licence
print("== APK distribué")
adb("install", "-r", REAL)
start()
time.sleep(9)
w, h = shot("1_licence")
check(w > h, f"écran en paysage ({w}x{h})")
st = status()
print("état", st)
check(st is not None and st[0] > 20, "la sortie audio tourne (rappels audio : %s)" % (st and st[0]))
log = adb("logcat", "-d")
check("audio ouvert" in log, "AAudio ouvert : " + (re.findall(r"audio ouvert[^\n]*", log) or ["?"])[0])
crash = adb("logcat", "-d", "-s", "AndroidRuntime:E", "DEBUG:F")
check(not crash.strip(), "pas de plantage au démarrage")

# ---------------------------------------------------------------- 2. version de test (déjà autorisée)
print("== APK de test")
adb("install", "-r", TEST)
start()
time.sleep(9)
w, h = shot("2_principal")
sc = Scr(w, h)
# clavier de l'écran principal
for x in (300, 340, 380):
    sc.tap(x, 620)
    time.sleep(0.3)
time.sleep(1.5)
shot("3_notes_principal")
st = status(); print("état", st)
check(st is not None and st[3] >= 3, "3 appuis sur le clavier = 3 notes (notes : %s)" % (st and st[3]))
check(st is not None and st[1] > 48000, "du son est produit (images sonores : %s)" % (st and st[1]))
# piano tactile
sc.tap(936, 29)
time.sleep(2)
w2, h2 = shot("4_piano")
b = open(f"{OUT}/2_principal.png", "rb").read(); c = open(f"{OUT}/4_piano.png", "rb").read()
check(b != c, "l'écran change quand on ouvre le piano tactile")
sp = Scr(w, h, 1200, max(460, min(744, 1200 * h // w)))
n0 = (status() or (0, 0, 0, 0))[3]
for x in (150, 450, 750, 1050):
    sp.tap(x, 330)
    time.sleep(0.3)
sp.swipe(100, 400, 1100, 400, 800)  # glissando
time.sleep(1.5)
shot("5_piano_notes")
st = status(); print("état", st)
check(st is not None and st[3] >= n0 + 5, "touches du piano tactile : notes jouées %s -> %s" % (n0, st and st[3]))
# retour (bouton Retour d'Android) puis réglages
adb("shell", "input", "keyevent", "KEYCODE_BACK")
time.sleep(1.5)
shot("6_retour")
sc.tap(1100, 29)  # Paramètres
time.sleep(1.5)
shot("7_parametres")
# vie de l'application : pause / reprise
adb("shell", "input", "keyevent", "KEYCODE_HOME")
time.sleep(2)
adb("shell", "am", "start", "-n", "com.jbpraise.layerskeys/.MainActivity")
time.sleep(4)
shot("8_reprise")
st2 = status(); print("état", st2)
check(st2 is not None and st2[0] > (st[0] if st else 0), "l'audio repart après la pause")
crash = adb("logcat", "-d", "-s", "AndroidRuntime:E", "DEBUG:F")
open(f"{OUT}/crash.txt", "w").write(crash)
check(not crash.strip(), "aucun plantage pendant tout le test")
open(f"{OUT}/logcat.txt", "w").write(adb("logcat", "-d"))
sys.exit(0 if ok else 1)
