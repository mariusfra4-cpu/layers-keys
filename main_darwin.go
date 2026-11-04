//go:build darwin && !vst3 && !au

package main

// Layers Keys pour Mac : application autonome (le cœur Cocoa est dans macapp_darwin.go).

import (
	"math"
	"os"
	"runtime"
	"runtime/debug"

	"github.com/ebitengine/purego/objc"
)

func init() { runtime.LockOSThread() } // Cocoa exige le thread principal

func registerDelegateClass() {
	_, err := objc.RegisterClass("LKDelegate", objc.GetClass("NSObject"), nil, nil, []objc.MethodDef{
		{Cmd: sel("applicationShouldTerminateAfterLastWindowClosed:"), Fn: func(self objc.ID, _cmd objc.SEL, app objc.ID) bool { return true }},
		// 0 = rester ouvert, 1 = quitter maintenant
		{Cmd: sel("applicationShouldTerminate:"), Fn: func(self objc.ID, _cmd objc.SEL, app objc.ID) uint {
			a := theMac
			if a.closing || a.ui.RequestClose() {
				return 1
			}
			a.flush()
			return 0
		}},
		{Cmd: sel("applicationWillTerminate:"), Fn: func(self objc.ID, _cmd objc.SEL, n objc.ID) { theMac.cleanup() }},
		{Cmd: sel("windowShouldClose:"), Fn: func(self objc.ID, _cmd objc.SEL, w objc.ID) bool {
			a := theMac
			ok := a.closing || a.ui.RequestClose()
			a.flush()
			return ok
		}},
		{Cmd: sel("windowDidResignKey:"), Fn: func(self objc.ID, _cmd objc.SEL, n objc.ID) {
			theMac.ui.FocusLost()
			theMac.flush()
		}},
		{Cmd: sel("windowDidBecomeKey:"), Fn: func(self objc.ID, _cmd objc.SEL, n objc.ID) {
			theMac.ui.RefreshKeyLabels()
			theMac.Invalidate()
		}},
	})
	if err != nil {
		panic(err)
	}
}

func buildMenu(app objc.ID) {
	menuItem := func(title, action, key string) objc.ID {
		it := cls("NSMenuItem").Send(sel("alloc"))
		var act objc.SEL // 0 = pas d'action (élément qui ne fait que porter un sous-menu)
		if action != "" {
			act = sel(action)
		}
		return it.Send(sel("initWithTitle:action:keyEquivalent:"), nsString(title), act, nsString(key))
	}
	newMenu := func(title string) objc.ID {
		return cls("NSMenu").Send(sel("alloc")).Send(sel("initWithTitle:"), nsString(title))
	}
	bar := newMenu("")
	// menu de l'application
	appItem := menuItem("", "", "")
	bar.Send(sel("addItem:"), appItem)
	appMenu := newMenu("Layers Keys")
	appMenu.Send(sel("addItem:"), menuItem("Masquer Layers Keys", "hide:", "h"))
	appMenu.Send(sel("addItem:"), cls("NSMenuItem").Send(sel("separatorItem")))
	appMenu.Send(sel("addItem:"), menuItem("Quitter Layers Keys", "terminate:", "q"))
	appItem.Send(sel("setSubmenu:"), appMenu)
	// menu Fenêtre
	winItem := menuItem("", "", "")
	bar.Send(sel("addItem:"), winItem)
	winMenu := newMenu("Fenêtre")
	winMenu.Send(sel("addItem:"), menuItem("Réduire", "performMiniaturize:", "m"))
	winItem.Send(sel("setSubmenu:"), winMenu)
	app.Send(sel("setMainMenu:"), bar)
	app.Send(sel("setWindowsMenu:"), winMenu)
}

func main() {
	if err := cocoaInit(); err != nil {
		println("Layers Keys : impossible de charger les bibliothèques du Mac :", err.Error())
		os.Exit(1)
	}
	pool := cls("NSAutoreleasePool").Send(sel("alloc")).Send(sel("init"))
	_ = pool

	eng := NewEngine()
	a := &macApp{eng: eng, scale: 1}
	theMac = a
	a.ui = NewUI(eng, a, configPath())
	a.ui.OnRescan = func() { a.midi.scan(a.ui, true) }
	if !a.setupGraphics() {
		println("Layers Keys : impossible de préparer l'affichage.")
		os.Exit(1)
	}

	app := cls("NSApplication").Send(sel("sharedApplication"))
	app.Send(sel("setActivationPolicy:"), 0) // application normale (icône dans le Dock)
	registerDelegateClass()
	delegate := cls("LKDelegate").Send(sel("alloc")).Send(sel("init"))
	app.Send(sel("setDelegate:"), delegate)
	buildMenu(app)

	// écran petit (MacBook 13") : la fenêtre est réduite pour tenir entièrement
	vis := objc.Send[nsRect](cls("NSScreen").Send(sel("mainScreen")), sel("visibleFrame"))
	if vis.Size.W > 0 && vis.Size.H > 0 {
		if s := math.Min((vis.Size.W-16)/WinW, (vis.Size.H-40)/WinH); s < 1 && s > 0.3 {
			a.scale = s
		}
	}
	a.cw, a.ch = math.Floor(WinW*a.scale+0.5), math.Floor(WinH*a.scale+0.5)

	frame := nsRect{nsPoint{0, 0}, nsSize{a.cw, a.ch}}
	const styleMask = 1 | 2 | 4 // titre, fermer, réduire (fenêtre à taille fixe)
	win := cls("NSWindow").Send(sel("alloc")).Send(sel("initWithContentRect:styleMask:backing:defer:"), frame, styleMask, 2, false)
	win.Send(sel("setReleasedWhenClosed:"), false)
	win.Send(sel("setTitle:"), nsString("Layers Keys  ·  By JB Praise"))
	a.makeView(frame)
	view := a.view
	a.win = win
	win.Send(sel("setContentView:"), view)
	win.Send(sel("setAcceptsMouseMovedEvents:"), true)
	win.Send(sel("setDelegate:"), delegate)
	win.Send(sel("makeFirstResponder:"), view)
	win.Send(sel("center"))

	// empêche macOS de ralentir le logiciel quand une autre fenêtre (le DAW) est au premier plan
	act := cls("NSProcessInfo").Send(sel("processInfo")).Send(sel("beginActivityWithOptions:reason:"),
		uint64(0x00EFFFFF|0xFF00000000), nsString("Audio en temps réel"))
	act.Send(sel("retain"))

	debug.SetGCPercent(400) // moins de pauses du ramasse-miettes = moins de craquements
	a.ready = true
	a.ui.RefreshKeyLabels()
	macStall = func() { // le Mac ne réclame pas de son : on passe aux gros tampons (100 ms) et on relance une fois
		a.ui.cfg.Audio.Buffer = 6144
		a.ui.Save()
		macRelaunch()
	}
	a.ui.StartAudio()
	a.midi.init(a.ui)
	a.midi.scan(a.ui, true)

	a.Invalidate()
	a.flush()
	win.Send(sel("makeKeyAndOrderFront:"), objc.ID(0))
	app.Send(sel("activateIgnoringOtherApps:"), true)
	app.Send(sel("run"))
}
