# Contrôle automatique d'Eli Guitar Chords dans un émulateur Android (version de test : la liste des licences
# est servie par cet ordinateur sur http://127.0.0.1:8099/licences.txt).
# De bout en bout : le générateur « Eli Clés » (version de test) fabrique la clé du téléphone, l'appli l'accepte.
# Étapes : non activé -> clé du générateur -> réouverture -> blocage à distance -> déblocage -> clé sans Internet
#          -> activation par la liste -> ancien code d'accès -> délai de grâce sans Internet.
import html, http.server, os, re, socketserver, subprocess, sys, threading, time

OUT, APK, GEN = sys.argv[1], sys.argv[2], sys.argv[3]
GPKG = "com.elipraise.keygen"
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

def edits(x):
    out = []
    for mm in re.finditer(r'<node [^>]*class="android.widget.EditText"[^>]*>', x):
        b = re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', mm.group(0))
        if b:
            a, c, d, e = map(int, b.groups())
            out.append(((a + d) // 2, (c + e) // 2))
    return out

def hide_kb():
    # comme un utilisateur : « Retour » ferme le clavier s'il est affiché (et seulement dans ce cas)
    for _ in range(3):
        st = adb("shell", "dumpsys", "input_method")
        if "mInputShown=true" not in st and "isInputViewShown=true" not in st:
            return
        adb("shell", "input", "keyevent", "4")
        time.sleep(0.8)

def type_in(p, text):
    adb("shell", "input", "tap", str(p[0]), str(p[1]))
    time.sleep(0.8)
    adb("shell", "input", "text", text)
    time.sleep(0.8)
    hide_kb()
    time.sleep(0.5)

def scroll_down():
    adb("shell", "input", "swipe", "160", "520", "160", "160", "300")
    time.sleep(0.8)

KEYRE = r"(?:[A-Z2-7]{8}-){12}[A-Z2-7]{7}"

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

# clavier virtuel désactivé : le test tape le texte lui-même, et le clavier cacherait les boutons
for ime in adb("shell", "ime", "list", "-s").split():
    print("clavier désactivé :", ime, adb("shell", "ime", "disable", ime).strip())
write_list("anciens-codes 2099-12-31\n")
adb("uninstall", PKG)
adb("uninstall", GPKG)
print(adb("install", "-r", "-g", APK).strip())
print(adb("install", "-r", GEN).strip())
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

# 2. le générateur fabrique la clé
adb("shell", "am", "start", "-n", GPKG + "/.MainActivity")
x = wait_for(lambda x: "DÉVERROUILLER" in x, 30)
shot("2a-generateur-verrou")
check("DÉVERROUILLER" in x, "générateur : demande du mot de passe")
e = edits(x)
if e:
    type_in(e[0], "TEST-TEST-TEST-TEST")
    tap(dump(), "DÉVERROUILLER")
x = wait_for(lambda x: "GÉNÉRER LA CLÉ" in x, 90)
check("GÉNÉRER LA CLÉ" in x, "générateur déverrouillé par le mot de passe")
e = edits(x)
if e:
    type_in(e[0], code)
    if len(e) > 1:
        type_in(e[1], "Client%stest")
    tap(dump(), "GÉNÉRER LA CLÉ")
time.sleep(2)
adb("shell", "input", "swipe", "160", "500", "160", "250", "300")
x = wait_for(lambda x: re.search(KEYRE, x), 30)
shot("2b-generateur-cle")
km = re.search(KEYRE, x)
check(km, "générateur : clé de licence fabriquée")
key = km.group(0) if km else "AUCUNE"
print("clé :", key)
check(code in x, "générateur : clé faite pour le code du client")

# 3. le client colle la clé dans l'appli
for essai in range(3):
    start()
    x = wait_for(lambda x: "pas encore activ" in x)
    if "pas encore activ" in x:
        break
    print("info : l'appli n'est pas à l'écran, nouvel essai")
scroll_down()
x = dump()
e = edits(x)
check(e, "champ de la clé visible")
if e:
    type_in(e[0], key)
    x = dump()
    if not tap(x, "Activer"):
        scroll_down()
        tap(dump(), "Activer")
x = wait_for(is_app, 30)
shot("3-cle-activee")
check(is_app(x), "clé du générateur acceptée : l'application s'ouvre")

# 4. réouverture
start()
x = wait_for(is_app, 30)
check(is_app(x), "réouverture : l'application s'ouvre directement")

# 5. blocage à distance d'un téléphone qui a une clé
write_list("bloque " + code + "\n")
start()
x = wait_for(lambda x: "désactivée" in x, 40)
shot("5-bloque")
check("désactivée" in x and not is_app(x), "clé + « bloque » dans la liste : l'application se verrouille")

# 6. déblocage
write_list("anciens-codes 2099-12-31\n")
tap(x, "Vérifier ma licence")
x = wait_for(is_app, 30)
check(is_app(x), "retiré de la liste des blocages : l'application se rouvre")

# 7. clé sans Internet : l'appli s'ouvre
srv.shutdown()
srv.server_close()
start()
x = wait_for(is_app, 30)
time.sleep(4)
x = dump()
shot("7-cle-sans-internet")
check(is_app(x), "clé sans Internet : l'application s'ouvre")

# 8. activation par la liste (sans clé)
srv = socketserver.TCPServer(("127.0.0.1", 8099), Q)
threading.Thread(target=srv.serve_forever, daemon=True).start()
adb("shell", "pm", "clear", PKG)
adb("shell", "pm", "grant", PKG, "android.permission.RECORD_AUDIO")
write_list(code + "\n")
start()
x = wait_for(is_app, 40)
check(is_app(x), "activation par la liste seule : l'application s'ouvre")

# 9. ancien code d'accès
adb("shell", "pm", "clear", PKG)
adb("shell", "pm", "grant", PKG, "android.permission.RECORD_AUDIO")
write_list("anciens-codes 2099-12-31\n")
start()
x = wait_for(lambda x: "pas encore activ" in x)
scroll_down()
scroll_down()
x = dump()
tap(x, "déjà un code")
time.sleep(1.5)
scroll_down()
x = dump()
e = sorted(edits(x), key=lambda q: q[1])[-1:]   # le champ le plus bas = celui de l'ancien code
if e:
    adb("shell", "input", "tap", str(e[-1][0]), str(e[-1][1]))
    time.sleep(0.8)
    adb("shell", "input", "text", "ELI-TEST-2026")
    time.sleep(0.8)
    adb("shell", "input", "keyevent", "66")
    time.sleep(1)
    hide_kb()
x = wait_for(is_app, 30)
shot("9-ancien-code")
check(e and is_app(x), "ancien code ELI-TEST-2026 accepté pendant la transition")

# 10. sans Internet, sans clé : délai de grâce
srv.shutdown()
srv.server_close()
start()
x = wait_for(is_app, 30)
time.sleep(4)
x = dump()
check(is_app(x), "sans clé et sans Internet : ouverte pendant le délai de 7 jours")

adb("shell", "input", "keyevent", "4")
time.sleep(1)
log = adb("logcat", "-d")
open(f"{OUT}/logcat.txt", "w").write(log)
rd = log.count("crashed service " + PKG)
print("info : arrêts du moteur d'affichage WebView pendant le test :", rd)
crash = re.findall(r"FATAL EXCEPTION.*\n.*Process: ([\w.:]+)", log)
for c in re.finditer(r"FATAL EXCEPTION[\s\S]{0,1500}", log):
    print(c.group(0)[:1500])
check(not [c for c in crash if c.startswith(PKG) or c.startswith(GPKG)], "aucun plantage (appli et générateur)" + (" (autres : " + ", ".join(crash) + ")" if crash else ""))
print("RESULTAT", "OK" if ok else "ECHEC")
sys.exit(0 if ok else 1)
