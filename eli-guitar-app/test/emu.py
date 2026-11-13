# Contrôle automatique d'Eli Guitar Chords dans un émulateur Android (version de test : la liste des licences
# est servie par cet ordinateur sur http://127.0.0.1:8099/licences.txt).
# Étapes : téléphone non activé -> activation à distance -> réouverture -> blocage à distance
#          -> ancien code d'accès -> sans Internet (délai de grâce).
import html, http.server, os, re, socketserver, subprocess, sys, threading, time

OUT, APK = sys.argv[1], sys.argv[2]
PKG = "com.elipraise.guitarchords"
LIST = os.path.join(OUT, "licences.txt")
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
            return html.unescape(x)
        time.sleep(1.5)
    return ""

def wait_for(pred, secs=40):
    end = time.time() + secs
    x = ""
    while time.time() < end:
        x = dump()
        if pred(x):
            return x
        time.sleep(1.5)
    return x

def node(x, text):
    for m in re.finditer(r'<node [^>]*>', x):
        n = m.group(0)
        if text in n:
            b = re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', n)
            if b:
                a, c, d, e = map(int, b.groups())
                if d > a and e > c:
                    return (a + d) // 2, (c + e) // 2
    return None

def tap(x, text):
    p = node(x, text)
    if p:
        adb("shell", "input", "tap", str(p[0]), str(p[1]))
    return p

def start():
    adb("shell", "am", "force-stop", PKG)
    adb("shell", "am", "start", "-n", PKG + "/.MainActivity")

def is_app(x):
    return "Tonalit" in x and "Progressions" in x

def write_list(text):
    open(LIST, "w").write(text)

# ---- serveur de la liste ----
class Q(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=OUT, **k)
    def log_message(self, *a):
        pass
socketserver.TCPServer.allow_reuse_address = True
srv = socketserver.TCPServer(("127.0.0.1", 8099), Q)
threading.Thread(target=srv.serve_forever, daemon=True).start()
print(adb("reverse", "tcp:8099", "tcp:8099").strip())

write_list("anciens-codes 2099-12-31\n")
adb("uninstall", PKG)
print(adb("install", "-r", "-g", APK).strip())
adb("logcat", "-c")

# 1. téléphone non activé
start()
x = wait_for(lambda x: "pas encore activ" in x)
open(f"{OUT}/ui-1.xml", "w").write(x)
shot("1-non-active")
check("pas encore activ" in x, "écran de licence : téléphone non activé")
m = re.search(r"ELI-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}", x)
check(m, "code du téléphone affiché")
code = m.group(0) if m else "ELI-AUCUN"
print("code :", code)

# 2. activation à distance
write_list("# test\n" + code + "   # téléphone de test\nanciens-codes 2099-12-31\n")
tap(x, "Vérifier ma licence")
x = wait_for(is_app, 30)
shot("2-active")
check(is_app(x), "après activation dans la liste : l'application s'ouvre")

# 3. réouverture
start()
x = wait_for(is_app, 30)
shot("3-reouverture")
check(is_app(x), "réouverture : l'application s'ouvre directement")

# 4. blocage à distance
write_list("bloque " + code + "\n" + code + "\n")
start()
x = wait_for(lambda x: "désactivée" in x, 40)
shot("4-bloque")
check("désactivée" in x and not is_app(x), "bloqué dans la liste : l'application se verrouille")

# 5. ancien code d'accès (pendant la période de transition)
adb("shell", "pm", "clear", PKG)
adb("shell", "pm", "grant", PKG, "android.permission.RECORD_AUDIO")
write_list("anciens-codes 2099-12-31\n")
start()
x = wait_for(lambda x: "pas encore activ" in x)
tap(x, "déjà un code")
time.sleep(1.5)
x = dump()
p = None
for mm in re.finditer(r'<node [^>]*class="android.widget.EditText"[^>]*>', x):
    b = re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', mm.group(0))
    if b:
        a, c, d, e = map(int, b.groups())
        p = ((a + d) // 2, (c + e) // 2)
if p:
    adb("shell", "input", "tap", str(p[0]), str(p[1]))
    time.sleep(1)
    adb("shell", "input", "text", "ELI-TEST-2026")
    time.sleep(1)
    adb("shell", "input", "keyevent", "66")
x = wait_for(is_app, 30)
shot("5-ancien-code")
check(p and is_app(x), "ancien code ELI-TEST-2026 accepté pendant la transition")

# 6. sans Internet : l'appli reste ouverte (délai de grâce)
srv.shutdown()
srv.server_close()
start()
x = wait_for(is_app, 30)
time.sleep(5)
x = dump()
shot("6-sans-internet")
check(is_app(x), "sans Internet : l'application reste ouverte (7 jours)")

adb("shell", "input", "keyevent", "4")
time.sleep(1)
log = adb("logcat", "-d")
open(f"{OUT}/logcat.txt", "w").write(log)
crash = re.findall(r"FATAL EXCEPTION.*\n.*Process: ([\w.:]+)", log)
for c in re.finditer(r"FATAL EXCEPTION[\s\S]{0,1500}", log):
    print(c.group(0)[:1500])
check(not [c for c in crash if c.startswith(PKG)], "aucun plantage de l'application" + (" (autres : " + ", ".join(crash) + ")" if crash else ""))
print("RESULTAT", "OK" if ok else "ECHEC")
sys.exit(0 if ok else 1)
