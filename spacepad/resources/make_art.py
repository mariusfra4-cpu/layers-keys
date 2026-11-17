# Dessine l'icône et les images des installateurs de Spacepad (orbe lumineux + anneau de 12 tonalités).
# Lancer : python3 resources/make_art.py   (depuis le dossier spacepad)
import math, os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MICHROMA = os.path.join(HERE, "fonts", "Michroma-Regular.ttf")
INTER = os.path.join(HERE, "fonts", "Inter-SemiBold.ttf")

def orb(size, bg=True, ring=True):
    S = size * 4
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if bg:
        r = int(S * 0.22)
        base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        bd = ImageDraw.Draw(base)
        bd.rounded_rectangle([S*0.04, S*0.04, S*0.96, S*0.96], radius=r, fill=(10, 14, 28, 255))
        # dégradé spatial
        grad = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        gd = ImageDraw.Draw(grad)
        for i in range(60, 0, -1):
            t = i / 60
            col = (int(18 + 20*(1-t)), int(26 + 30*(1-t)), int(51 + 60*(1-t)), int(255 * (1-t) * 0.6))
            rr = S * 0.46 * t
            gd.ellipse([S/2-rr, S*0.48-rr, S/2+rr, S*0.48+rr], fill=col)
        mask = Image.new("L", (S, S), 0)
        ImageDraw.Draw(mask).rounded_rectangle([S*0.04, S*0.04, S*0.96, S*0.96], radius=r, fill=255)
        base.alpha_composite(grad)
        base.putalpha(mask)
        img.alpha_composite(base)
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([S*0.04, S*0.04, S*0.96, S*0.96], radius=r, outline=(91, 99, 120, 255), width=max(2, S//120))
    cx, cy = S/2, S/2
    # halo
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gr = S * 0.30
    gd.ellipse([cx-gr, cy-gr, cx+gr, cy+gr], fill=(111, 216, 255, 150))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.06))
    img.alpha_composite(glow)
    # orbe
    core = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    cd = ImageDraw.Draw(core)
    R = S * 0.20
    for i in range(80, 0, -1):
        t = i / 80
        ox, oy = cx - R*0.25*(1-t), cy - R*0.35*(1-t)
        if t > 0.7: col = (14, 27, 56)
        elif t > 0.45: col = (42, 95, 158)
        elif t > 0.18: col = (111, 216, 255)
        else: col = (216, 246, 255)
        rr = R * t
        cd.ellipse([ox-rr, oy-rr, ox+rr, oy+rr], fill=col + (255,))
    core = core.filter(ImageFilter.GaussianBlur(S * 0.008))
    img.alpha_composite(core)
    d = ImageDraw.Draw(img)
    if ring:
        RR = S * 0.355
        d.ellipse([cx-RR, cy-RR, cx+RR, cy+RR], outline=(111, 216, 255, 90), width=max(2, S//200))
        for k in range(12):
            a = math.radians(k * 30 - 90)
            x, y = cx + RR*math.cos(a), cy + RR*math.sin(a)
            br = S * (0.042 if k == 4 else 0.03)
            col = (111, 216, 255, 255) if k == 4 else (58, 69, 96, 255)
            if k == 4:
                gl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
                ImageDraw.Draw(gl).ellipse([x-br*2, y-br*2, x+br*2, y+br*2], fill=(111, 216, 255, 160))
                img.alpha_composite(gl.filter(ImageFilter.GaussianBlur(S*0.02)))
                d = ImageDraw.Draw(img)
            d.ellipse([x-br, y-br, x+br, y+br], fill=col, outline=(255, 255, 255, 70), width=max(1, S//300))
    return img.resize((size, size), Image.LANCZOS)

def main():
    res = HERE
    icon = orb(1024)
    icon.save(os.path.join(res, "icon.png"))
    win = os.path.join(ROOT, "installer", "windows")
    orb(256).save(os.path.join(win, "spacepad.ico"), sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])

    # image de côté de l'assistant Windows (164 x 314)
    W, H = 164, 314
    side = Image.new("RGB", (W, H), (7, 10, 20))
    sd = ImageDraw.Draw(side)
    for y in range(H):
        t = y / H
        sd.line([(0, y), (W, y)], fill=(int(7+12*(1-t)), int(10+16*(1-t)), int(20+31*(1-t))))
    o = orb(150, bg=False)
    side.paste(o, (7, 46), o)
    f1 = ImageFont.truetype(MICHROMA, 15)
    f2 = ImageFont.truetype(INTER, 10)
    tw = sd.textlength("SPACEPAD", font=f1)
    sd.text(((W - tw) / 2, 214), "SPACEPAD", font=f1, fill=(242, 246, 255))
    tw = sd.textlength("BY JB PRAISE", font=f2)
    sd.text(((W - tw) / 2, 240), "BY JB PRAISE", font=f2, fill=(154, 166, 189))
    side.save(os.path.join(win, "side.bmp"))

    # bandeau du haut (150 x 57)
    hb = Image.new("RGB", (150, 57), (255, 255, 255))
    o = orb(52, bg=True)
    hb.paste(o, (94, 2), o)
    hb.save(os.path.join(win, "header.bmp"))

    # fond de l'installateur Mac (bas-gauche), clair et sombre
    for name, bgc, txt in (("background.png", (0, 0, 0, 0), (40, 46, 64, 255)), ("background-dark.png", (0, 0, 0, 0), (220, 228, 245, 255))):
        im = Image.new("RGBA", (620, 418), bgc)
        o = orb(150, bg=True)
        im.alpha_composite(o, (22, 220))
        dd = ImageDraw.Draw(im)
        dd.text((24, 376), "SPACEPAD", font=ImageFont.truetype(MICHROMA, 15), fill=txt)
        dd.text((24, 398), "BY JB PRAISE", font=ImageFont.truetype(INTER, 9), fill=txt)
        im.save(os.path.join(ROOT, "installer", "mac", "res", name))

if __name__ == "__main__":
    main()
