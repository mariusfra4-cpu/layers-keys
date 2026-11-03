import re, subprocess, sys, time, xml.etree.ElementTree as ET

def adb(*a):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=True).stdout

def dump():
    for _ in range(5):
        adb("shell", "uiautomator", "dump", "/sdcard/ui.xml")
        x = adb("exec-out", "cat", "/sdcard/ui.xml")
        if "<hierarchy" in x:
            return ET.fromstring(x[x.index("<hierarchy"):])
        time.sleep(1)
    raise SystemExit("impossible de lire l'écran")

def nodes(root):
    for n in root.iter("node"):
        m = re.match(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", n.get("bounds", ""))
        if m:
            x1, y1, x2, y2 = map(int, m.groups())
            yield n, (x1 + x2) // 2, (y1 + y2) // 2

def find(root, text=None, cls=None):
    for n, cx, cy in nodes(root):
        if text is not None and text not in (n.get("text") or ""):
            continue
        if cls is not None and cls not in (n.get("class") or ""):
            continue
        return n, cx, cy
    return None

def tap(root, **k):
    r = find(root, **k)
    if not r:
        raise SystemExit("introuvable : %s" % k)
    adb("shell", "input", "tap", str(r[1]), str(r[2]))
    time.sleep(1)

def alltext(root):
    return " | ".join((n.get("text") or "") for n, _, _ in nodes(root) if n.get("text"))

def shot(name):
    with open(name, "wb") as f:
        f.write(subprocess.run(["adb", "exec-out", "screencap", "-p"], capture_output=True).stdout)
