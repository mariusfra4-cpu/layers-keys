package main

import (
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"testing"
)

func newTestDroid(t *testing.T) *droidApp {
	d := newDroid(t.TempDir(), "TESTID123", 48000)
	d.SetView(2160, 1080)
	d.post(func() {
		d.ui.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
		d.ui.loadSource(1, SrcCfg{Kind: "builtin", Builtin: iPad})
		d.ui.labels[0], d.ui.labels[1] = "Grand Piano", "Nappe"
	})
	d.sync()
	t.Cleanup(func() { close(d.quit) })
	return d
}

func droidShot(t *testing.T, d *droidApp, name string) {
	dir := os.Getenv("DSHOTS")
	if dir == "" {
		return
	}
	d.sync()
	w, h := d.Size()
	dst := make([]uint32, w*h)
	d.Frame(dst, w, h, w)
	img := image.NewRGBA(image.Rect(0, 0, w, h))
	for i, p := range dst {
		img.Set(i%w, i/w, color.RGBA{uint8(p), uint8(p >> 8), uint8(p >> 16), 255})
	}
	f, _ := os.Create(filepath.Join(dir, name+".png"))
	png.Encode(f, img)
	f.Close()
}

// position écran d'une touche du piano tactile
func (d *droidApp) pianoXY(note int) (float64, float64) {
	l, tp, s := d.Layout()
	var x float64
	for i, n := range d.ui.whites {
		if n == note {
			x = (float64(i) - d.pn.first + 0.5) * d.pn.ww
		}
	}
	y := float64(d.pn.kb.y + d.pn.kb.h - 40)
	return l + x*s, tp + y*s
}

func TestDroidPianoMultiTouch(t *testing.T) {
	d := newTestDroid(t)
	d.post(func() { d.setPiano(true) })
	d.sync()
	droidShot(t, d, "piano_vide")
	x1, y1 := d.pianoXY(60)
	x2, y2 := d.pianoXY(64)
	x3, y3 := d.pianoXY(67)
	d.Touch(touchDown, 0, x1, y1)
	d.Touch(touchDown, 1, x2, y2)
	d.Touch(touchDown, 2, x3, y3)
	d.sync()
	if d.ui.counts[60] != 1 || d.ui.counts[64] != 1 || d.ui.counts[67] != 1 {
		t.Fatalf("trois doigts = trois notes : %v", d.ui.counts)
	}
	droidShot(t, d, "piano_accord")
	// le son sort vraiment
	out := make([]float32, 2*4096)
	d.Render(out, 4096)
	peak := 0.0
	for _, v := range out {
		peak = math.Max(peak, math.Abs(float64(v)))
	}
	if peak < 0.01 {
		t.Fatalf("aucun son (crête %.4f)", peak)
	}
	// un doigt se lève, les autres restent
	d.Touch(touchUp, 1, x2, y2)
	d.sync()
	if d.ui.counts[64] != 0 || d.ui.counts[60] != 1 || d.ui.counts[67] != 1 {
		t.Fatalf("lever un doigt ne coupe que sa note : %v", d.ui.counts)
	}
	// glissando : le doigt 0 passe de Do à Ré
	xr, yr := d.pianoXY(62)
	d.Touch(touchMove, 0, xr, yr)
	d.sync()
	if d.ui.counts[60] != 0 || d.ui.counts[62] != 1 {
		t.Fatalf("glissando : %v", d.ui.counts)
	}
	d.Touch(touchUp, 0, xr, yr)
	d.Touch(touchUp, 2, x3, y3)
	d.sync()
	for n, c := range d.ui.counts {
		if c != 0 {
			t.Fatalf("note %d restée enfoncée", n)
		}
	}
}

func TestDroidPianoBlackAndScroll(t *testing.T) {
	d := newTestDroid(t)
	d.post(func() { d.setPiano(true) })
	d.sync()
	// touche noire Do# entre Do et Ré
	var cx float64
	for i, n := range d.ui.whites {
		if n == 60 {
			cx = (float64(i+1) - d.pn.first) * d.pn.ww
		}
	}
	l, tp, s := d.Layout()
	d.Touch(touchDown, 0, l+cx*s, tp+float64(d.pn.kb.y+30)*s)
	d.sync()
	if d.ui.counts[61] != 1 {
		t.Fatalf("Do# attendu : %v", d.ui.counts)
	}
	d.Touch(touchUp, 0, l+cx*s, tp+float64(d.pn.kb.y+30)*s)
	d.sync()
	// bouton ▶ : la fenêtre se déplace d'une octave
	f0 := d.pn.first
	r := d.ui.find("pright")
	if r == nil {
		t.Fatal("bouton ▶ absent")
	}
	px, py := centre(r.r)
	d.Touch(touchDown, 0, l+float64(px)*s, tp+float64(py)*s)
	d.Touch(touchUp, 0, l+float64(px)*s, tp+float64(py)*s)
	d.sync()
	if d.pn.first != f0+7 {
		t.Fatalf("défilement : %.1f -> %.1f", f0, d.pn.first)
	}
	// pédale
	r = d.ui.find("ppedal")
	px, py = centre(r.r)
	d.Touch(touchDown, 0, l+float64(px)*s, tp+float64(py)*s)
	d.Touch(touchUp, 0, l+float64(px)*s, tp+float64(py)*s)
	d.sync()
	if !d.eng.sustain {
		t.Fatal("la pédale devait s'enclencher")
	}
	// retour au mixage
	r = d.ui.find("pback")
	px, py = centre(r.r)
	d.Touch(touchDown, 0, l+float64(px)*s, tp+float64(py)*s)
	d.Touch(touchUp, 0, l+float64(px)*s, tp+float64(py)*s)
	d.sync()
	if d.piano {
		t.Fatal("« MIXAGE » doit revenir à l'écran principal")
	}
}

func TestDroidMainScreen(t *testing.T) {
	d := newTestDroid(t)
	d.sync()
	droidShot(t, d, "principal")
	l, tp, s := d.Layout()
	// jouer sur le clavier de l'écran principal avec 2 doigts
	kx, ky := float64(kbX+whiteW*10+whiteW/2), float64(kbY+100)
	d.Touch(touchDown, 0, l+kx*s, tp+ky*s)
	d.Touch(touchDown, 1, l+(kx+3*whiteW)*s, tp+ky*s)
	d.sync()
	n := 0
	for _, c := range d.ui.counts {
		n += c
	}
	if n != 2 {
		t.Fatalf("deux doigts sur le clavier = 2 notes, obtenu %d", n)
	}
	d.Touch(touchUp, 0, l+kx*s, tp+ky*s)
	d.Touch(touchUp, 1, l+(kx+3*whiteW)*s, tp+ky*s)
	d.sync()
	// le bouton « PIANO TACTILE » ouvre le piano
	r := d.ui.find("touchpiano")
	if r == nil {
		t.Fatal("bouton PIANO TACTILE absent")
	}
	px, py := centre(r.r)
	d.Touch(touchDown, 0, l+float64(px)*s, tp+float64(py)*s)
	d.Touch(touchUp, 0, l+float64(px)*s, tp+float64(py)*s)
	d.sync()
	if !d.piano {
		t.Fatal("le piano tactile doit s'ouvrir")
	}
}

func TestDroidScrollList(t *testing.T) {
	d := newTestDroid(t)
	d.post(func() {
		o := &overlay{title: "Liste longue", sel: -1}
		for i := 0; i < 60; i++ {
			o.items = append(o.items, olItem{text: fmt.Sprintf("Son %d", i), action: func() {}})
		}
		d.ui.ov = o
		d.Invalidate()
	})
	d.sync()
	droidShot(t, d, "menu_sons")
	l, tp, s := d.Layout()
	if d.ui.ov == nil {
		t.Fatal("menu attendu")
	}
	sc0 := d.ui.ov.scroll
	// glisser le doigt vers le haut = descendre dans la liste
	x, y := float64(WinW/2), float64(WinH/2)
	d.Touch(touchDown, 0, l+x*s, tp+y*s)
	for k := 1; k <= 12; k++ {
		d.Touch(touchMove, 0, l+x*s, tp+(y-float64(k)*20)*s)
	}
	d.Touch(touchUp, 0, l+x*s, tp+(y-240)*s)
	d.sync()
	if d.ui.ov == nil {
		t.Fatal("le menu ne doit pas se fermer ni choisir un élément pendant un défilement")
	}
	if d.ui.ov.scroll <= sc0 {
		t.Fatalf("la liste devait défiler (%d -> %d)", sc0, d.ui.ov.scroll)
	}
}

func TestDroidLicenseKeyboardRequest(t *testing.T) {
	d := newTestDroid(t)
	d.post(func() { d.ui.licShow = true; d.Invalidate() })
	d.sync()
	got := ""
	for r := d.NextReq(); r != ""; r = d.NextReq() {
		got += r + ";"
	}
	if got != "ime:1;" {
		t.Fatalf("le clavier à l'écran doit s'ouvrir sur l'écran de licence : %q", got)
	}
	droidShot(t, d, "licence")
}

func TestDroidFrameFormat(t *testing.T) {
	d := newTestDroid(t)
	d.sync()
	w, h := d.Size()
	dst := make([]uint32, w*h)
	if r := d.Frame(dst, w, h, w); r != 1 {
		t.Fatalf("image attendue, code %d", r)
	}
	if d.Frame(dst, w, h, w) != 0 {
		t.Fatal("une image déjà copiée ne doit pas être renvoyée")
	}
	if dst[0]>>24 != 0xff {
		t.Fatal("alpha opaque attendu")
	}
}

func TestDroidIDAndLicenseDir(t *testing.T) {
	d := newTestDroid(t)
	if androidMachineID() != "android|TESTID123" {
		t.Fatal(androidMachineID())
	}
	if len(androidLicenseDirs()) != 1 || filepath.Base(androidLicenseDirs()[0]) != "licence" {
		t.Fatal(androidLicenseDirs())
	}
	_ = d
}

func TestDroidPinchZoom(t *testing.T) {
	d := newTestDroid(t)
	d.sync()
	_, _, s0 := d.Layout()
	// deux doigts qui s'écartent sur l'écran principal (hors clavier)
	d.Touch(touchDown, 0, 900, 300)
	d.Touch(touchDown, 1, 1100, 300)
	for k := 1; k <= 10; k++ {
		d.Touch(touchMove, 0, 900-float64(k)*20, 300)
		d.Touch(touchMove, 1, 1100+float64(k)*20, 300)
	}
	d.sync()
	_, _, s1 := d.Layout()
	if s1 < s0*1.8 {
		t.Fatalf("le pincement devait agrandir (%.2f -> %.2f)", s0, s1)
	}
	d.Touch(touchUp, 0, 700, 300)
	d.Touch(touchUp, 1, 1300, 300)
	d.sync()
	droidShot(t, d, "zoom")
	// un appui après le zoom tombe au bon endroit de l'écran
	l, tp, sc := d.Layout()
	cx, cy := 700.0, 300.0
	d.Touch(touchDown, 0, l+cx*sc, tp+cy*sc)
	d.sync()
	if abs(d.ui.mx-int(cx)) > 1 || abs(d.ui.my-int(cy)) > 1 {
		t.Fatalf("position après zoom : %d,%d au lieu de %v,%v", d.ui.mx, d.ui.my, cx, cy)
	}
	d.Touch(touchUp, 0, l+cx*sc, tp+cy*sc)
	d.sync()
	if len(d.ptrs) != 0 {
		t.Fatal("doigts oubliés")
	}
}
