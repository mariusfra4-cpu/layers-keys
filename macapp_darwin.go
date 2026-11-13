//go:build darwin

package main

// Cœur Mac commun à l'application autonome et au plugin VST3 : fenêtre/vue Cocoa, dessin, événements, boîtes de dialogue.
// Écrit sans compilateur C : la bibliothèque purego appelle directement les fonctions du système.

import (
	"math"
	"path/filepath"
	"runtime/debug"
	"strings"
	"sync"
	"sync/atomic"
	"unicode/utf8"

	"github.com/ebitengine/purego/objc"
)

// une sortie audio ouverte
type audioBackend interface {
	Close()
	Info() string
	Panel() // panneau de réglages du pilote (inutile sur Mac)
}

type macApp struct {
	eng   *Engine
	ui    *UI
	audio audioBackend
	midi  macMidi

	win, view, layer objc.ID
	cgCtx            uintptr
	canvas           *Canvas

	scale  float64 // réduction si l'écran est petit
	cw, ch float64 // taille de la fenêtre en points

	pending  int32
	tick     int
	closing  bool
	wheelAcc float64
	audioErr string
	ready    bool

	plugin   bool    // vrai : la vue est l'éditeur d'un plugin VST3 (pas de MIDI ni de sortie audio propres)
	timer    objc.ID // minuterie de l'éditeur
	userFlag int32   // une action est à faire dans le thread de l'interface (état reçu du DAW)
	onUser   func()

	autoDetach bool // AU : le logiciel hôte garde la vue (Logic peut la cacher puis la réafficher telle quelle)
	hidden     bool // la vue n'est dans aucune fenêtre : on ne dessine plus, mais on garde tout pour sa réouverture
}

// toutes les vues ouvertes : l'application seule, ou une par éditeur de plugin
var macApps sync.Map // objc.ID de la vue → *macApp

func appOf(view objc.ID) *macApp {
	if v, ok := macApps.Load(view); ok {
		return v.(*macApp)
	}
	return theMac // nil dans un plugin : chaque fonction de la vue le vérifie
}

// une erreur dans une fonction appelée par macOS (souris, clavier, dessin) ne doit jamais bloquer ni faire tomber
// le logiciel hôte : elle est notée dans le journal et ignorée
func macRecover() {
	if r := recover(); r != nil {
		lklog("interface : erreur attrapée : %v\n%s", r, debug.Stack())
	}
}

var theMac *macApp

// ---- Host : ce que l'interface demande au Mac

func (a *macApp) Invalidate() { atomic.StoreInt32(&a.pending, 1) }

func (a *macApp) Mods() (bool, bool) {
	f := objc.Send[uint64](cls("NSEvent"), sel("modifierFlags"))
	return macMods(f)
}

func (a *macApp) SetTitle(t string) {
	if a.win != 0 && !a.plugin {
		a.win.Send(sel("setTitle:"), nsString(t))
	}
}

func (a *macApp) Quit() {
	if a.plugin {
		return
	}
	a.closing = true
	cls("NSApplication").Send(sel("sharedApplication")).Send(sel("terminate:"), objc.ID(0))
}

func (a *macApp) AudioAPIs() []string              { return audioAPIs() }
func (a *macApp) AudioDevices(api string) []string { return audioDevices(api) }
func (a *macApp) AudioOpen(cfg AudioCfg) (string, error) {
	if a.audio != nil {
		a.audio.Close()
		a.audio = nil
	}
	b, info, err := openAudio(a.eng, cfg)
	if err != nil {
		a.audioErr = err.Error()
		return "", err
	}
	a.audio, a.audioErr = b, ""
	return info, nil
}
func (a *macApp) AudioPanel() {}

// lettre écrite sur la touche (selon le clavier du Mac : AZERTY, QWERTY…)
func (a *macApp) KeyLabel(sc uint32, fallback string) string {
	if cgEventCreateKeyboardEvent == nil || cgEventKeyboardGetUnicode == nil {
		return fallback
	}
	code, ok := macKeyCodeForScan(sc)
	if !ok {
		return fallback
	}
	ev := cgEventCreateKeyboardEvent(0, uint16(code), true)
	if ev == 0 {
		return fallback
	}
	defer cfRelease(ev)
	var n uintptr
	var buf [8]uint16
	cgEventKeyboardGetUnicode(ev, 4, &n, &buf[0])
	if n == 0 || n > 4 {
		return fallback
	}
	rs := make([]rune, 0, 4)
	for i := 0; i < int(n); i++ {
		rs = append(rs, rune(buf[i]))
	}
	s := strings.TrimSpace(strings.ToUpper(string(rs)))
	if s == "" || s[0] < 32 {
		return fallback
	}
	return s
}

// ---- boîtes « Ouvrir » et « Enregistrer sous »

func (a *macApp) OpenFile(kind string) (string, bool) {
	panel := cls("NSOpenPanel").Send(sel("openPanel"))
	panel.Send(sel("setCanChooseFiles:"), true)
	panel.Send(sel("setCanChooseDirectories:"), false)
	panel.Send(sel("setAllowsMultipleSelection:"), false)
	switch kind {
	case "project":
		panel.Send(sel("setMessage:"), nsString("Ouvrir un projet Layers Keys"))
		panel.Send(sel("setAllowedFileTypes:"), nsArray("lkeys"))
	case "audio":
		panel.Send(sel("setMessage:"), nsString("Choisir un fichier audio à lire"))
		panel.Send(sel("setAllowedFileTypes:"), nsArray("wav", "wave", "mp3"))
	default:
		panel.Send(sel("setMessage:"), nsString("Choisir un fichier SF2, SF3, WAV ou MP3"))
		panel.Send(sel("setAllowedFileTypes:"), nsArray("sf2", "sf3", "wav", "wave", "mp3"))
	}
	r := a.runPanel(panel)
	a.ui.FocusLost()
	return r, r != ""
}

func (a *macApp) SaveFile(defaultName string) (string, bool) {
	panel := cls("NSSavePanel").Send(sel("savePanel"))
	panel.Send(sel("setMessage:"), nsString("Enregistrer le projet"))
	panel.Send(sel("setAllowedFileTypes:"), nsArray("lkeys"))
	panel.Send(sel("setCanCreateDirectories:"), true)
	if defaultName != "" {
		panel.Send(sel("setNameFieldStringValue:"), nsString(filepath.Base(defaultName)))
		if dir := filepath.Dir(defaultName); dir != "." && fileExists(dir) {
			panel.Send(sel("setDirectoryURL:"), cls("NSURL").Send(sel("fileURLWithPath:"), nsString(dir)))
		}
	}
	r := a.runPanel(panel)
	a.ui.FocusLost()
	if r != "" && !strings.HasSuffix(strings.ToLower(r), ".lkeys") {
		r += ".lkeys"
	}
	return r, r != ""
}

func (a *macApp) runPanel(panel objc.ID) string {
	if objc.Send[int](panel, sel("runModal")) != 1 { // NSModalResponseOK
		return ""
	}
	url := panel.Send(sel("URL"))
	if url == 0 {
		return ""
	}
	return goStr(url.Send(sel("path")))
}

// ---- dessin

func (a *macApp) setupGraphics() bool {
	a.canvas = NewCanvas(WinW, WinH)
	SharedTextRenderer().Attach(a.canvas)
	cs := cgColorSpaceCreateDeviceRGB()
	if cs == 0 {
		return false
	}
	// pixels 0x00RRGGBB en mémoire little-endian = « alpha ignoré en premier » + ordre 32 bits petit-boutiste
	const info = 6 | 8192
	a.cgCtx = cgBitmapContextCreate(addrOf(a.canvas.Pix), WinW, WinH, 8, WinW*4, cs, info)
	cfRelease(cs)
	return a.cgCtx != 0
}

func (a *macApp) cleanupGraphics() {
	if a.cgCtx != 0 {
		cgContextRelease(a.cgCtx)
		a.cgCtx = 0
	}
}

// dessine l'interface si quelque chose a changé
var firstPaintLogged int32

func (a *macApp) flush() {
	if a.canvas == nil || !atomic.CompareAndSwapInt32(&a.pending, 1, 0) {
		return
	}
	a.ui.Draw(a.canvas)
	img := cgBitmapContextCreateImage(a.cgCtx)
	if img == 0 {
		return
	}
	if a.plugin && atomic.CompareAndSwapInt32(&firstPaintLogged, 0, 1) {
		lklog("éditeur : première image dessinée")
	}
	tr := cls("CATransaction")
	tr.Send(sel("begin"))
	tr.Send(sel("setDisableActions:"), true) // pas de fondu entre deux images
	a.layer.Send(sel("setContents:"), objc.ID(img))
	tr.Send(sel("commit"))
	cgImageRelease(img)
}

// ---- événements

func (a *macApp) point(ev objc.ID) (int, int) {
	p := objc.Send[nsPoint](ev, sel("locationInWindow"))
	if a.view != 0 { // la vue d'un plugin n'est pas au coin de la fenêtre du DAW
		p = objc.Send[nsPoint](a.view, sel("convertPoint:fromView:"), p, objc.ID(0))
	}
	x, y := p.X, a.ch-p.Y // l'origine du Mac est en bas, celle de l'interface en haut
	if a.scale < 0.999 {
		x, y = x/a.scale, y/a.scale
	}
	return int(math.Floor(x + 0.5)), int(math.Floor(y + 0.5))
}

func (a *macApp) keyInfo(ev objc.ID) (vk int, sc uint32, ext, ok bool) {
	code := int(objc.Send[uint16](ev, sel("keyCode")))
	var ch rune
	if s := goStr(ev.Send(sel("charactersIgnoringModifiers"))); s != "" {
		ch, _ = utf8.DecodeRuneInString(s)
	}
	return macKeyToWin(code, ch)
}

func (a *macApp) keyDown(ev objc.ID) {
	flags := objc.Send[uint64](ev, sel("modifierFlags"))
	rep := objc.Send[bool](ev, sel("isARepeat"))
	if vk, sc, ext, ok := a.keyInfo(ev); ok {
		a.ui.KeyDown(vk, sc, rep, ext)
	}
	if flags&(macModCommand|macModControl) == 0 { // texte tapé (champs de saisie, filtres de recherche)
		for _, r := range goStr(ev.Send(sel("characters"))) {
			if r >= 32 && r != 127 && !(r >= 0xF700 && r <= 0xF8FF) {
				a.ui.Char(r)
			}
		}
	}
	a.flush()
}

func (a *macApp) scroll(ev objc.ID) {
	dy := objc.Send[float64](ev, sel("scrollingDeltaY"))
	if objc.Send[bool](ev, sel("hasPreciseScrollingDeltas")) { // pavé tactile : on cumule
		a.wheelAcc += dy
		for a.wheelAcc >= 14 {
			a.ui.Wheel(120)
			a.wheelAcc -= 14
		}
		for a.wheelAcc <= -14 {
			a.ui.Wheel(-120)
			a.wheelAcc += 14
		}
	} else if dy != 0 { // molette de souris
		n := int(math.Round(dy))
		if n == 0 {
			n = 1
			if dy < 0 {
				n = -1
			}
		}
		n = clamp(n, -5, 5)
		a.ui.Wheel(n * 120)
	}
	a.flush()
}

func (a *macApp) onTick() {
	a.tick++
	if a.autoDetach && a.view != 0 {
		// Fenêtre du plugin fermée par le logiciel hôte : Logic Pro 13 garde la même vue et la réaffiche telle quelle
		// à la réouverture. On ne la détruit donc plus : on arrête seulement de dessiner, et on reprend au retour.
		if a.view.Send(sel("window")) == 0 {
			if !a.hidden {
				a.hidden = true
				lklog("éditeur : fenêtre cachée par le logiciel hôte (gardée pour sa réouverture)")
			}
			a.ui.DrainMidi()
			return
		}
		if a.hidden {
			a.hidden = false
			lklog("éditeur : fenêtre de nouveau affichée")
			a.Invalidate()
		}
	}
	if atomic.CompareAndSwapInt32(&a.userFlag, 1, 0) && a.onUser != nil {
		a.onUser()
	}
	a.ui.DrainMidi()
	if a.tick%2 == 0 {
		a.ui.Tick()
	}
	if a.tick%120 == 0 && !a.plugin {
		a.midi.scan(a.ui, false)
	}
	a.flush()
}

func (a *macApp) cleanup() {
	if !a.ready {
		return
	}
	a.ready = false
	a.midi.close()
	if a.audio != nil {
		a.audio.Close()
		a.audio = nil
	}
	a.ui.Shutdown()
}

// ---- classes Objective-C (vue et délégué)

var viewClassOnce sync.Once

// classe « LKView » : la vue qui reçoit souris et clavier (une instance par fenêtre ou éditeur)
func registerViewClass() {
	viewClassOnce.Do(func() {
		yes := func(self objc.ID, _cmd objc.SEL) bool { return true }
		mouse := func(f func(a *macApp, x, y int)) func(self objc.ID, _cmd objc.SEL, ev objc.ID) {
			return func(self objc.ID, _cmd objc.SEL, ev objc.ID) {
				defer macRecover()
				a := appOf(self)
				if a == nil || a.ui == nil {
					return
				}
				if a.plugin { // dans un DAW, le clic donne le clavier à notre vue
					if w := self.Send(sel("window")); w != 0 {
						w.Send(sel("makeFirstResponder:"), self)
					}
				}
				x, y := a.point(ev)
				f(a, x, y)
				a.flush()
			}
		}
		_, err := objc.RegisterClass("LKView", objc.GetClass("NSView"), nil, nil, []objc.MethodDef{
			{Cmd: sel("acceptsFirstResponder"), Fn: yes},
			{Cmd: sel("isOpaque"), Fn: yes},
			// si le système redemande l'image (changement d'écran, Retina…), on redessine tout de suite
			{Cmd: sel("wantsUpdateLayer"), Fn: yes},
			{Cmd: sel("updateLayer"), Fn: func(self objc.ID, _cmd objc.SEL) {
				defer macRecover()
				if a := appOf(self); a != nil && a.ui != nil {
					a.Invalidate()
					a.flush()
				}
			}},
			// la vue revient dans une fenêtre (réouverture de la fenêtre du plugin) : on redessine tout de suite
			{Cmd: sel("viewDidMoveToWindow"), Fn: func(self objc.ID, _cmd objc.SEL) {
				defer macRecover()
				if a := appOf(self); a != nil && a.ui != nil && self.Send(sel("window")) != 0 {
					a.Invalidate()
					a.flush()
				}
			}},
			{Cmd: sel("acceptsFirstMouse:"), Fn: func(self objc.ID, _cmd objc.SEL, ev objc.ID) bool { return true }},
			{Cmd: sel("mouseDown:"), Fn: mouse(func(a *macApp, x, y int) { a.ui.MouseDown(x, y) })},
			{Cmd: sel("mouseUp:"), Fn: mouse(func(a *macApp, x, y int) { a.ui.MouseUp(x, y) })},
			{Cmd: sel("mouseDragged:"), Fn: mouse(func(a *macApp, x, y int) { a.ui.MouseMove(x, y, true) })},
			{Cmd: sel("mouseMoved:"), Fn: mouse(func(a *macApp, x, y int) { a.ui.MouseMove(x, y, false) })},
			{Cmd: sel("rightMouseDown:"), Fn: mouse(func(a *macApp, x, y int) { a.ui.RightDown(x, y) })},
			{Cmd: sel("scrollWheel:"), Fn: func(self objc.ID, _cmd objc.SEL, ev objc.ID) {
				defer macRecover()
				if a := appOf(self); a != nil && a.ui != nil {
					a.scroll(ev)
				}
			}},
			{Cmd: sel("keyDown:"), Fn: func(self objc.ID, _cmd objc.SEL, ev objc.ID) {
				defer macRecover()
				if a := appOf(self); a != nil && a.ui != nil {
					a.keyDown(ev)
				}
			}},
			{Cmd: sel("keyUp:"), Fn: func(self objc.ID, _cmd objc.SEL, ev objc.ID) {
				defer macRecover()
				a := appOf(self)
				if a == nil || a.ui == nil {
					return
				}
				if vk, sc, _, ok := a.keyInfo(ev); ok {
					a.ui.KeyUp(vk, sc)
				}
				a.flush()
			}},
			// Avec la touche Commande enfoncée, macOS n'envoie plus les relâchements de touches : on lâche tout.
			{Cmd: sel("flagsChanged:"), Fn: func(self objc.ID, _cmd objc.SEL, ev objc.ID) {
				defer macRecover()
				if objc.Send[uint64](ev, sel("modifierFlags"))&macModCommand != 0 {
					if a := appOf(self); a != nil && a.ui != nil {
						a.ui.FocusLost()
						a.flush()
					}
				}
			}},
			// minuterie : la vue est la cible, elle retrouve son application
			{Cmd: sel("tick:"), Fn: func(self objc.ID, _cmd objc.SEL, timer objc.ID) {
				defer macRecover()
				if a := appOf(self); a != nil && a.ui != nil {
					a.onTick()
				}
			}},
		})
		if err != nil {
			panic(err)
		}
	})
}

// crée la vue, sa couche d'image et la minuterie ; frame en points
func (a *macApp) makeView(frame nsRect) {
	registerViewClass()
	view := cls("LKView").Send(sel("alloc")).Send(sel("initWithFrame:"), frame)
	view.Send(sel("setWantsLayer:"), true)
	layer := view.Send(sel("layer"))
	layer.Send(sel("setContentsGravity:"), nsString("resize"))
	layer.Send(sel("setMinificationFilter:"), nsString("trilinear"))
	layer.Send(sel("setOpaque:"), true)
	a.view, a.layer = view, layer
	macApps.Store(view, a)
	// minuterie ajoutée aux modes « communs » de la boucle principale : elle continue de tourner quand le logiciel hôte
	// (Logic…) est dans un menu, un glisser-déposer ou un déplacement de fenêtre ; sinon l'interface se figerait
	a.timer = cls("NSTimer").Send(sel("timerWithTimeInterval:target:selector:userInfo:repeats:"),
		1.0/60, view, sel("tick:"), objc.ID(0), true)
	cls("NSRunLoop").Send(sel("mainRunLoop")).Send(sel("addTimer:forMode:"), a.timer, nsString("kCFRunLoopCommonModes"))
}

// ---- éditeur de plugin : la vue vit dans la fenêtre du DAW

// taille de l'éditeur (réduite si l'écran est petit)
func (a *macApp) editorSize() (int, int) {
	a.scale = 1
	vis := objc.Send[nsRect](cls("NSScreen").Send(sel("mainScreen")), sel("visibleFrame"))
	if vis.Size.W > 0 && vis.Size.H > 0 {
		if sc := math.Min((vis.Size.W-24)/WinW, (vis.Size.H-130)/WinH); sc < 1 && sc > 0.3 {
			a.scale = sc
		}
	}
	a.cw, a.ch = math.Floor(WinW*a.scale+0.5), math.Floor(WinH*a.scale+0.5)
	return int(a.cw), int(a.ch)
}

// crée la vue de l'éditeur (le logiciel hôte la place lui-même dans sa fenêtre) ; 0 en cas d'échec
func (a *macApp) createEditorView() objc.ID {
	a.plugin = true
	if a.view != 0 { // déjà ouvert : on repart proprement
		a.detachEditor()
	}
	if cocoaInit() != nil {
		return 0
	}
	atomic.StoreInt32(&a.pending, 0)
	w, h := a.editorSize()
	lklog("éditeur : taille %d x %d (échelle %.2f)", w, h, a.scale)
	a.makeView(nsRect{nsPoint{0, 0}, nsSize{float64(w), float64(h)}})
	if !a.setupGraphics() {
		lklog("éditeur : les graphiques n'ont pas pu être préparés")
		a.detachEditor()
		return 0
	}
	lklog("éditeur : graphiques prêts")
	a.ui.mu.Lock()
	a.ui.toast = ""
	a.ui.mu.Unlock()
	a.ui.RefreshKeyLabels()
	a.Invalidate()
	a.flush()
	return a.view
}

// parent : la NSView donnée par le DAW (VST3)
func (a *macApp) attachEditor(parent objc.ID) bool {
	if parent == 0 || a.createEditorView() == 0 {
		return false
	}
	parent.Send(sel("addSubview:"), a.view)
	return true
}

func (a *macApp) detachEditor() {
	if a.view == 0 {
		return
	}
	a.ui.FocusLost() // relâche les touches du clavier encore enfoncées
	if a.timer != 0 {
		a.timer.Send(sel("invalidate"))
		a.timer = 0
	}
	macApps.Delete(a.view)
	a.view.Send(sel("removeFromSuperview"))
	a.view, a.layer = 0, 0
	a.cleanupGraphics()
	atomic.StoreInt32(&a.pending, 0)
}
