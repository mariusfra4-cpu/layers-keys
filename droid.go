package main

// Couche Android de Layers Keys (sans cgo, donc testable sous Linux).
// Java envoie les doigts, les touches et le MIDI ; un seul « fil de travail » Go possède l'interface,
// dessine dans une image que Java copie à l'écran, et demande à Java ce qu'il ne sait pas faire (choisir un fichier, clavier à l'écran…).

import (
	"math"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	touchDown = 0
	touchMove = 1
	touchUp   = 2
)

// un doigt posé sur l'écran
type dPtr struct {
	note     int     // touche du clavier jouée par ce doigt (-1 = aucune)
	active   *region // zone de l'interface saisie par ce doigt
	deferred bool    // appui différé : c'est peut-être le début d'un défilement
	scroll   bool
	kb       bool // doigt posé sur le clavier du piano tactile
	sx, sy   int
	lx, ly   int
	acc      int
	vx, vy   float64 // dernière position dans la vue (pixels)
	pinch    bool    // ce doigt sert à pincer
}

type droidApp struct {
	eng *Engine
	ui  *UI

	filesDir string
	ev       chan func()
	quit     chan struct{}

	cvMain, cvPiano *Canvas
	piano           bool
	vw, vh          int // taille de la vue (pixels)

	// image prête à être copiée à l'écran
	fmu    sync.Mutex
	frame  []uint32
	fw, fh int
	fnew   bool

	dirty int32

	// demandes à Java
	rmu    sync.Mutex
	reqs   []string
	fileCh chan string
	clip   string
	imeOn  bool

	ptrs map[int]*dPtr
	pn   pianoState

	zoom   float64 // agrandissement de l'écran principal (1 = tout l'écran)
	zl, zt float64

	paused int32

	renderCalls  int64
	renderFrames int64
	midiMsgs     int64
	noteOns      int64
	buf          []float32
	buf2         []float32
}

var theDroid *droidApp

// dossier privé de l'appli et identifiant du téléphone (donnés par Java)
var droidFiles, droidID string

// identifiant de l'ordinateur pour la licence : ici, celui du téléphone (stable même si l'appli est réinstallée)
func androidMachineID() string { return "android|" + droidID }

func androidLicenseDirs() []string { return []string{filepath.Join(droidFiles, "licence")} }

func newDroid(filesDir, androidID string, rate int) *droidApp {
	droidFiles, droidID = filesDir, androidID
	d := &droidApp{filesDir: filesDir, ev: make(chan func(), 512), quit: make(chan struct{}),
		fileCh: make(chan string, 1), ptrs: map[int]*dPtr{}}
	_ = os.MkdirAll(filepath.Join(filesDir, "cfg"), 0o755)
	d.eng = NewEngine()
	if rate >= 8000 && rate != d.eng.Rate() {
		d.eng.SetSampleRate(rate)
	}
	d.cvMain = NewCanvas(WinW, WinH)
	SharedTextRenderer().Attach(d.cvMain)
	d.cvPiano = NewCanvas(WinW, 560)
	SharedTextRenderer().Attach(d.cvPiano)
	d.vw, d.vh = 2160, 1080
	d.pn.init()
	cfg := filepath.Join(filesDir, "cfg", "config.json")
	_, errCfg := os.Stat(cfg)
	d.ui = NewUI(d.eng, d, cfg)
	if errCfg != nil { // premier lancement : un piano est déjà chargé, on peut jouer tout de suite
		d.ui.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
	}
	d.ui.OnPiano = func() { d.setPiano(true) }
	d.ui.OnBluetooth = func() { d.push("ble") }
	d.ui.OnRescan = func() { d.push("midiscan") }
	d.Invalidate()
	go d.loop()
	return d
}

// ---------------------------------------------------------------- fil de travail

func (d *droidApp) loop() {
	t := time.NewTicker(16 * time.Millisecond)
	defer t.Stop()
	n := 0
	for {
		select {
		case f := <-d.ev:
			d.safe(f)
			d.after()
		case <-t.C:
			if atomic.LoadInt32(&d.paused) != 0 {
				continue
			}
			n++
			d.safe(func() {
				d.ui.DrainMidi()
				if n%2 == 0 {
					d.ui.Tick()
				}
			})
			d.after()
		case <-d.quit:
			return
		}
	}
}

func (d *droidApp) safe(f func()) {
	defer func() {
		if r := recover(); r != nil {
			d.push("log:erreur interne")
		}
	}()
	f()
}

// après chaque événement : clavier à l'écran et nouvelle image si nécessaire
func (d *droidApp) after() {
	want := !d.piano && d.ui.WantsText()
	d.rmu.Lock()
	if want != d.imeOn {
		d.imeOn = want
		if want {
			d.reqs = append(d.reqs, "ime:1")
		} else {
			d.reqs = append(d.reqs, "ime:0")
		}
	}
	d.rmu.Unlock()
	if atomic.CompareAndSwapInt32(&d.dirty, 1, 0) {
		d.safe(d.paint)
	}
}

func (d *droidApp) paint() {
	var cv *Canvas
	if d.piano {
		cv = d.cvPiano
		d.drawPiano(cv)
	} else {
		cv = d.cvMain
		d.ui.Draw(cv)
	}
	d.fmu.Lock()
	if len(d.frame) != len(cv.Pix) {
		d.frame = make([]uint32, len(cv.Pix))
	}
	for i, p := range cv.Pix {
		d.frame[i] = 0xff000000 | (p&0xff)<<16 | p&0xff00 | (p>>16)&0xff // Android : octets R, V, B, A
	}
	d.fw, d.fh, d.fnew = cv.W, cv.H, true
	d.fmu.Unlock()
}

// post : exécute f dans le fil de travail
func (d *droidApp) post(f func()) {
	select {
	case d.ev <- f:
	default: // file pleine : on ignore (un doigt qui bouge)
	}
}

// sync : attend que tout ce qui a été posté soit traité, image comprise (tests)
func (d *droidApp) sync() {
	for i := 0; i < 2; i++ {
		done := make(chan struct{})
		d.ev <- func() { close(done) }
		<-done
	}
	time.Sleep(3 * time.Millisecond)
}

// ---------------------------------------------------------------- Java : image

// Frame copie la dernière image (si elle est nouvelle) dans dst ; renvoie 0 rien de neuf, 1 copiée, 2 la taille a changé.
func (d *droidApp) Frame(dst []uint32, w, h, stride int) int {
	d.fmu.Lock()
	defer d.fmu.Unlock()
	if !d.fnew {
		return 0
	}
	if w != d.fw || h != d.fh {
		return 2
	}
	for y := 0; y < h; y++ {
		copy(dst[y*stride:y*stride+w], d.frame[y*w:(y+1)*w])
	}
	d.fnew = false
	return 1
}

// Size : taille de l'image courante
func (d *droidApp) Size() (int, int) {
	d.fmu.Lock()
	defer d.fmu.Unlock()
	if d.fw == 0 {
		return WinW, WinH
	}
	return d.fw, d.fh
}

func (d *droidApp) SetView(w, h int) {
	d.post(func() {
		if w <= 0 || h <= 0 {
			return
		}
		d.vw, d.vh = w, h
		hh := WinW * h / w
		hh = clamp(hh, 460, WinH)
		if hh != d.cvPiano.H {
			d.cvPiano = NewCanvas(WinW, hh)
			SharedTextRenderer().Attach(d.cvPiano)
		}
		d.Invalidate()
	})
}

// Layout : où l'image est dessinée dans la vue (gauche, haut, échelle)
func (d *droidApp) baseLayout() (float64, float64, float64) {
	cw, ch := WinW, WinH
	if d.piano {
		cw, ch = d.cvPiano.W, d.cvPiano.H
	}
	vw, vh := float64(d.vw), float64(d.vh)
	s := vw / float64(cw)
	if s2 := vh / float64(ch); s2 < s {
		s = s2
	}
	return (vw - float64(cw)*s) / 2, (vh - float64(ch)*s) / 2, s
}

func (d *droidApp) Layout() (float64, float64, float64) {
	l, t, s := d.baseLayout()
	if d.piano || d.zoom <= 1.001 {
		return l, t, s
	}
	return d.zl, d.zt, s * d.zoom
}

// recadre l'image zoomée pour qu'elle couvre toujours la vue
func (d *droidApp) clampZoom() {
	if d.zoom < 1.05 {
		d.zoom = 1
		return
	}
	d.zoom = math.Min(d.zoom, 3.5)
	_, _, s0 := d.baseLayout()
	s := s0 * d.zoom
	vw, vh := float64(d.vw), float64(d.vh)
	cw, ch := float64(WinW)*s, float64(WinH)*s
	if cw <= vw {
		d.zl = (vw - cw) / 2
	} else {
		d.zl = math.Min(0, math.Max(vw-cw, d.zl))
	}
	if ch <= vh {
		d.zt = (vh - ch) / 2
	} else {
		d.zt = math.Min(0, math.Max(vh-ch, d.zt))
	}
}

// pincer : deux doigts hors du clavier agrandissent ou déplacent l'écran principal
func (d *droidApp) pinch(vx0, vy0, vx1, vy1, px0, py0, px1, py1 float64) {
	l, t, s := d.Layout()
	_, _, s0 := d.baseLayout()
	dOld := math.Hypot(px1-px0, py1-py0)
	dNew := math.Hypot(vx1-vx0, vy1-vy0)
	if dOld < 20 || dNew < 20 {
		return
	}
	z := math.Min(3.5, math.Max(1, s*dNew/dOld/s0))
	ns := s0 * z
	mx0, my0 := (px0+px1)/2, (py0+py1)/2
	mx1, my1 := (vx0+vx1)/2, (vy0+vy1)/2
	cx, cy := (mx0-l)/s, (my0-t)/s
	d.zoom = z
	d.zl, d.zt = mx1-cx*ns, my1-cy*ns
	d.clampZoom()
}

// ---------------------------------------------------------------- Host

func (d *droidApp) Invalidate()                          { atomic.StoreInt32(&d.dirty, 1) }
func (d *droidApp) Mods() (bool, bool)                   { return false, false }
func (d *droidApp) SetTitle(string)                      {}
func (d *droidApp) Quit()                                { d.push("quit") }
func (d *droidApp) AudioAPIs() []string                  { return []string{apiAAudio} }
func (d *droidApp) AudioDevices(string) []string         { return nil }
func (d *droidApp) AudioPanel()                          {}
func (d *droidApp) KeyLabel(sc uint32, fb string) string { return fb }
func (d *droidApp) AudioOpen(AudioCfg) (string, error)   { return "Sortie Android (AAudio)", nil }
func (d *droidApp) ClipGet() string                      { d.rmu.Lock(); defer d.rmu.Unlock(); return d.clip }
func (d *droidApp) ClipSet(s string)                     { d.push("clip:" + s) }

func (d *droidApp) push(r string) {
	d.rmu.Lock()
	d.reqs = append(d.reqs, r)
	d.rmu.Unlock()
}

// NextReq : prochaine demande pour Java ("" = aucune)
func (d *droidApp) NextReq() string {
	d.rmu.Lock()
	defer d.rmu.Unlock()
	if len(d.reqs) == 0 {
		return ""
	}
	r := d.reqs[0]
	d.reqs = d.reqs[1:]
	return r
}

func (d *droidApp) SetClip(s string) {
	d.rmu.Lock()
	d.clip = s
	d.rmu.Unlock()
}

// FileResult : Java a copié le fichier choisi dans l'appli ("" = annulé)
func (d *droidApp) FileResult(p string) {
	select {
	case d.fileCh <- p:
	default:
	}
}

func (d *droidApp) OpenFile(kind string) (string, bool) {
	select { // résultat resté d'une ancienne demande
	case <-d.fileCh:
	default:
	}
	d.push("open:" + kind)
	p := <-d.fileCh // le fil de travail attend ; les doigts s'empilent et seront traités ensuite
	if p == "" {
		return "", false
	}
	return p, true
}

func (d *droidApp) SaveFile(name string) (string, bool) {
	dir := filepath.Join(d.filesDir, "projets")
	_ = os.MkdirAll(dir, 0o755)
	name = strings.Map(func(r rune) rune {
		if strings.ContainsRune(`/\:*?"<>|`, r) {
			return '_'
		}
		return r
	}, name)
	if strings.TrimSpace(name) == "" {
		name = "Mon projet"
	}
	return filepath.Join(dir, name), true
}

// ---------------------------------------------------------------- Java : événements

func (d *droidApp) setPiano(on bool) {
	if d.piano == on {
		return
	}
	d.releaseAll()
	d.piano = on
	d.Invalidate()
}

// retire tous les doigts (changement d'écran, perte de focus)
func (d *droidApp) releaseAll() {
	for id, p := range d.ptrs {
		d.lift(id, p, 0, 0)
	}
	d.ptrs = map[int]*dPtr{}
}

// Touch : un doigt (id) bouge ; x, y en pixels de la vue
func (d *droidApp) Touch(action, id int, vx, vy float64) {
	d.post(func() {
		l, t, sc := d.Layout()
		x, y := int((vx-l)/sc), int((vy-t)/sc)
		switch action {
		case touchDown:
			if old := d.ptrs[id]; old != nil {
				d.lift(id, old, x, y)
			}
			p := &dPtr{note: -1, sx: x, sy: y, lx: x, ly: y, vx: vx, vy: vy}
			d.ptrs[id] = p
			d.fingerDown(id, p, x, y)
			d.startPinch(id, p)
		case touchMove:
			p := d.ptrs[id]
			if p == nil {
				return
			}
			if p.pinch {
				if q := d.pinchPartner(id); q != nil {
					d.pinch(vx, vy, q.vx, q.vy, p.vx, p.vy, q.vx, q.vy)
				}
				p.vx, p.vy = vx, vy
				d.Invalidate()
				return
			}
			p.vx, p.vy = vx, vy
			d.fingerMove(id, p, x, y)
		case touchUp:
			if p := d.ptrs[id]; p != nil {
				if !p.pinch {
					d.lift(id, p, x, y)
				}
				delete(d.ptrs, id)
			}
		}
	})
}

// deux doigts libres (ni sur une note, ni sur le clavier) sur l'écran principal : pincement
func (d *droidApp) startPinch(id int, p *dPtr) {
	if d.piano || p.note >= 0 || p.kb || d.ui.licShow {
		return
	}
	for oid, q := range d.ptrs {
		if oid != id && !q.pinch && q.note < 0 && !q.kb {
			q.pinch, p.pinch = true, true
			d.cancelFinger(q)
			d.cancelFinger(p)
			return
		}
	}
}

func (d *droidApp) pinchPartner(id int) *dPtr {
	for oid, q := range d.ptrs {
		if oid != id && q.pinch {
			return q
		}
	}
	return nil
}

// le doigt devient un doigt de pincement : on oublie ce qu'il avait saisi
func (d *droidApp) cancelFinger(p *dPtr) {
	p.active, p.deferred, p.scroll = nil, false, false
}

func (d *droidApp) lift(id int, p *dPtr, x, y int) {
	if d.piano {
		d.pianoUp(id, p, x, y)
		return
	}
	d.mainUp(id, p, x, y)
}

func (d *droidApp) fingerDown(id int, p *dPtr, x, y int) {
	if d.piano {
		d.pianoDown(id, p, x, y)
		return
	}
	d.mainDown(id, p, x, y)
}

func (d *droidApp) fingerMove(id int, p *dPtr, x, y int) {
	if d.piano {
		d.pianoMove(id, p, x, y)
		return
	}
	d.mainMove(id, p, x, y)
}

// ---- écran principal : l'interface de bureau, avec plusieurs doigts

func (d *droidApp) hit(x, y int) (top *region, scrollable bool) {
	u := d.ui
	for i := len(u.regions) - 1; i >= 0; i-- {
		r := u.regions[i]
		if !r.r.has(x, y) {
			continue
		}
		if r.wheel != nil {
			scrollable = true
		}
		if top == nil && (r.down != nil || r.dbl != nil) {
			rr := r
			top = &rr
		}
	}
	return
}

func (d *droidApp) mainDown(id int, p *dPtr, x, y int) {
	u := d.ui
	top, scrollable := d.hit(x, y)
	if top != nil && top.id == "keys" && u.ov == nil {
		if k := u.keyAt(x, y); k >= 0 {
			p.note = k
			u.press(3000+id, k, mouseVel(y))
			d.noteOnCount()
			d.Invalidate()
		}
		return
	}
	if top != nil && top.drag == nil && scrollable { // liste : un doigt qui glisse la fait défiler, un simple appui choisit
		p.deferred = true
		return
	}
	prev := u.active
	u.active = nil
	u.MouseDown(x, y)
	p.active = u.active
	u.active = prev
}

func (d *droidApp) mainMove(id int, p *dPtr, x, y int) {
	u := d.ui
	switch {
	case p.deferred:
		if !p.scroll && abs(y-p.sy)+abs(x-p.sx) > 14 {
			p.scroll = true
		}
		if p.scroll {
			p.acc += y - p.ly
			p.lx, p.ly = x, y
			u.mx, u.my = x, y
			for p.acc >= 56 {
				u.Wheel(120)
				p.acc -= 56
			}
			for p.acc <= -56 {
				u.Wheel(-120)
				p.acc += 56
			}
		}
	case p.note >= 0:
		k := u.keyAt(x, y)
		if k != p.note && u.ov == nil {
			u.release(3000+id, p.note)
			p.note = k
			if k >= 0 {
				u.press(3000+id, k, mouseVel(y))
			}
			d.Invalidate()
		}
	case p.active != nil:
		prev := u.active
		u.active = p.active
		u.MouseMove(x, y, true)
		u.active = prev
	}
	p.lx, p.ly = x, y
}

func (d *droidApp) mainUp(id int, p *dPtr, x, y int) {
	u := d.ui
	switch {
	case p.deferred:
		if !p.scroll {
			prev := u.active
			u.active = nil
			u.MouseDown(x, y)
			u.MouseUp(x, y)
			u.active = prev
		}
	case p.note >= 0:
		u.release(3000+id, p.note)
		p.note = -1
		d.Invalidate()
	case p.active != nil:
		prev := u.active
		u.active = p.active
		u.MouseUp(x, y)
		u.active = prev
	}
}

func (d *droidApp) noteOnCount() { atomic.AddInt64(&d.noteOns, 1) }

func abs(a int) int {
	if a < 0 {
		return -a
	}
	return a
}

// Key : touche du clavier (vk Windows : Entrée 13, Retour arrière 8, Échap 27, Espace 32)
func (d *droidApp) Key(vk int, down bool) {
	d.post(func() {
		if down {
			d.ui.KeyDown(vk, 0, false, false)
		} else {
			d.ui.KeyUp(vk, 0)
		}
		d.Invalidate()
	})
}

func (d *droidApp) Char(r rune) {
	d.post(func() { d.ui.Char(r); d.Invalidate() })
}

// Back : bouton retour d'Android ; renvoie vrai si l'appli a utilisé le geste
func (d *droidApp) Back() {
	d.post(func() {
		switch {
		case d.piano:
			d.setPiano(false)
		case d.ui.licShow:
			// rien : l'écran de licence reste
		case d.ui.ov != nil:
			d.ui.KeyDown(vkEscape, 0, false, false)
		default:
			d.push("background")
		}
		d.Invalidate()
	})
}

// Midi : octets bruts reçus d'un clavier MIDI (USB ou Bluetooth)
func (d *droidApp) Midi(b []byte, running *byte) {
	for _, m := range midiBytesToMsgs(b, running) {
		atomic.AddInt64(&d.midiMsgs, 1)
		d.ui.MidiFast(m)
	}
}

// MidiStatus : texte affiché dans l'en-tête (clavier branché ou non)
func (d *droidApp) MidiStatus(ok bool, s string) {
	d.post(func() { d.ui.SetMidiStatus(ok, s) })
}

// Render : son stéréo entrelacé (thread audio)
func (d *droidApp) Render(out []float32, frames int) {
	if frames <= 0 {
		return
	}
	if len(d.buf) < frames {
		d.buf = make([]float32, frames)
		d.buf2 = make([]float32, frames)
	}
	l, r := d.buf[:frames], d.buf2[:frames]
	d.eng.RenderF(l, r)
	for i := 0; i < frames; i++ {
		out[2*i], out[2*i+1] = l[i], r[i]
	}
	atomic.AddInt64(&d.renderCalls, 1)
	atomic.AddInt64(&d.renderFrames, int64(frames))
}

// Resume : l'appli revient au premier plan
func (d *droidApp) Resume() {
	atomic.StoreInt32(&d.paused, 0)
	d.post(func() { d.Invalidate() })
}

func (d *droidApp) Stop() {
	atomic.StoreInt32(&d.paused, 1)
	d.post(func() {
		d.releaseAll()
		d.eng.Panic()
		d.ui.Shutdown()
	})
}

func (d *droidApp) Shutdown() {
	done := make(chan struct{})
	d.ev <- func() {
		d.releaseAll()
		d.eng.Panic()
		d.ui.Shutdown()
		close(done)
	}
	select {
	case <-done:
	case <-time.After(3 * time.Second):
	}
}
