# Contrôle automatique d'Eli Guitar Chords et du générateur « Eli Clés » dans un émulateur Android
# (versions de test : graine de test, mot de passe TEST-TEST-TEST-TEST). Aucune connexion Internet.
# De bout en bout : le générateur « Eli Clés » (version de test) fabrique la clé du téléphone, l'appli l'accepte.
# Étapes : non activé -> mauvaise clé refusée -> clé du générateur -> activé -> réouverture (toujours activé).
import html, os, re, subprocess, sys, time

OUT, APK, GEN = sys.argv[1], sys.argv[2], sys.argv[3]
GPKG = "com.elipraise.keygen"
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
    for _ in range(6):
        adb("shell", "rm", "-f", "/sdcard/ui.xml")
        r = adb("shell", "uiautomator", "dump", "/sdcard/ui.xml")
        x = adb("shell", "cat", "/sdcard/ui.xml")
        if "<hierarchy" in x:
            return html.unescape(x)
        print("info : lecture de l'écran ratée, nouvel essai :", (r.strip() + " | " + x.strip())[:200])
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

def last_page():
    out = adb("logcat", "-d", "-s", "EliGuitarChords:I")
    m = re.findall(r"page affichée : (application|écran de licence)", out)
    return m[-1] if m else None

def wait_page(want, secs=30):
    end = time.time() + secs
    while time.time() < end:
        if last_page() == want:
            return True
        time.sleep(1)
    return last_page() == want

def start_clean():
    adb("logcat", "-c")
    start()

def is_app(x):
    return "Tonalit" in x and "Progressions" in x

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

# clavier virtuel désactivé : le test tape le texte lui-même, et le clavier cacherait les boutons
for ime in adb("shell", "ime", "list", "-s").split():
    print("clavier désactivé :", ime, adb("shell", "ime", "disable", ime).strip())
adb("uninstall", PKG)
adb("uninstall", GPKG)
print(adb("install", "-r", "-g", APK).strip())
print(adb("install", "-r", GEN).strip())
perms = adb("shell", "dumpsys", "package", PKG)
check("android.permission.INTERNET" not in perms, "l'appli n'a pas la permission Internet")

# 1. téléphone non activé
start_clean()
x = wait_for(lambda x: "pas encore activ" in x)
open(f"{OUT}/ui-1.xml", "w").write(x)
shot("1-non-active")
check("pas encore activ" in x and last_page() == "écran de licence", "écran de licence : téléphone non activé")
m = re.search(r"ELI-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}", x)
check(m, "code du téléphone affiché")
code = m.group(0) if m else "ELI-AUCUN"
print("code :", code)

# 2. une mauvaise clé est refusée
scroll_down()
x = dump()
e = edits(x)
check(e, "champ de la clé visible")
fausse = "AAAAAAAA-" * 12 + "AAAAAAA"
if e:
    type_in(e[0], fausse)
    x = dump()
    if not tap(x, "Activer"):
        scroll_down()
        tap(dump(), "Activer")
time.sleep(2)
adb("shell", "input", "swipe", "160", "200", "160", "600", "300")   # remonte en haut, où s'affiche le message
time.sleep(1)
x = wait_for(lambda x: "correspond pas" in x or "Vérifie qu" in x, 25)
shot("2-mauvaise-cle")
check(("correspond pas" in x or "Vérifie qu" in x) and last_page() == "écran de licence", "mauvaise clé refusée")

# 3. le générateur fabrique la clé
adb("shell", "am", "start", "-n", GPKG + "/.MainActivity")
x = wait_for(lambda x: "DÉVERROUILLER" in x, 30)
shot("3a-generateur-verrou")
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
shot("3b-generateur-cle")
km = re.search(KEYRE, x)
check(km, "générateur : clé de licence fabriquée")
key = km.group(0) if km else "AUCUNE"
print("clé :", key)
check(code in x, "générateur : clé faite pour le code du client")

# 4. le client colle la clé dans l'appli
start_clean()
x = wait_for(lambda x: "pas encore activ" in x)
scroll_down()
x = dump()
e = edits(x)
if e:
    type_in(e[0], key)   # l'appli vient d'être relancée : le champ est vide
    x = dump()
    if not tap(x, "Activer"):
        scroll_down()
        tap(dump(), "Activer")
ok4 = wait_page("application", 30)
shot("4-cle-activee")
check(ok4, "clé du générateur acceptée : l'application s'ouvre")

# 5. réouverture : toujours activée
start_clean()
ok5 = wait_page("application", 30)
time.sleep(3)
ok5 = ok5 and last_page() == "application"
shot("5-reouverture")
check(ok5, "réouverture : l'application s'ouvre directement")

adb("shell", "input", "keyevent", "4")
time.sleep(1)
log = adb("logcat", "-d")
open(f"{OUT}/logcat.txt", "w").write(log)
crash = re.findall(r"FATAL EXCEPTION.*\n.*Process: ([\w.:]+)", log)
for c in re.finditer(r"FATAL EXCEPTION[\s\S]{0,1500}", log):
    print(c.group(0)[:1500])
check(not [c for c in crash if c.startswith(PKG) or c.startswith(GPKG)], "aucun plantage (appli et générateur)" + (" (autres : " + ", ".join(crash) + ")" if crash else ""))
print("RESULTAT", "OK" if ok else "ECHEC")
sys.exit(0 if ok else 1)
