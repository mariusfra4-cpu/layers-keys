package main

// Interface de Layers Keys : logique (événements, réglages, presets, choix de sons).
// Le dessin est dans ui_draw.go. Rien ici ne dépend de Windows : tout est testable.

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	WinW = 1200
	WinH = 744

	kbX      = 150
	kbY      = 556
	whiteW   = 28
	whiteH   = 124
	blackW   = 17
	blackH   = 78
	lowNote  = 36
	highNote = 96

	rowX, rowY0, rowW, rowH, rowGap = 16, 124, 800, 48, 4
	panX, panY, panW, panH          = 832, 124, 352, 428
)

type rect struct{ x, y, w, h int }

func (r rect) has(px, py int) bool { return px >= r.x && px < r.x+r.w && py >= r.y && py < r.y+r.h }

// Ce que l'interface demande à la plateforme (Windows) de faire.
type Host interface {
	Invalidate()                                // demande un nouveau dessin (peut être appelé depuis n'importe quel thread)
	OpenFile(kind string) (string, bool)        // boîte « Ouvrir » : kind = "sound" (SF2/WAV/MP3), "project", "audio", "folder" ou "padfolder" (un dossier)
	SaveFile(defaultName string) (string, bool) // boîte « Enregistrer sous » d'un projet
	Mods() (ctrl, shift bool)                   // touches Ctrl et Maj enfoncées ?
	SetTitle(title string)                      // titre de la fenêtre
	Quit()                                      // ferme le logiciel

	AudioAPIs() []string                        // pilotes audio disponibles (asio, wasapi…)
	AudioDevices(api string) []string           // cartes son d'un pilote
	AudioOpen(cfg AudioCfg) (string, error)     // (ré)ouvre la sortie audio, renvoie une description
	AudioPanel()                                // panneau de réglages du pilote (ASIO)
	KeyLabel(sc uint32, fallback string) string // lettre écrite sur une touche du clavier du PC
}

// Presse-papiers (facultatif : la plateforme peut ne pas le proposer)
type Clipper interface {
	ClipGet() string
	ClipSet(s string)
}

type region struct {
	r     rect
	id    string
	down  func(x, y int)
	drag  func(x, y int)
	up    func(x, y int)
	wheel func(d int)
	dbl   func()
}

type blackKey struct {
	note int
	r    rect
}

type olItem struct {
	text, sub string
	header    bool
	action    func()
}

type overlay struct {
	title      string
	hint       string
	items      []olItem
	filterable bool
	filter     string
	scroll     int
	sel        int
	input      bool
	text       string
	onOK       func(string)
	back       func()                  // bouton « Retour » : revient à l'écran précédent
	custom     func(c *Canvas, r rect) // contenu dessiné à la place de la liste
	wide       bool                    // grande fenêtre (bibliothèque de sons, pads)
}

type UI struct {
	lic         *License
	licSt       LicStatus
	licShow     bool   // l'écran « Autoriser le logiciel » est affiché
	licKey      string // clé en cours de saisie
	licMsg      string
	licMsgOK    bool
	licNext     time.Time
	licLeft     string
	Plugin      bool   // vrai quand le logiciel tourne comme plugin VST3 dans un DAW
	OnPiano     func() // Android : ouvre le piano tactile (nil ailleurs)
	OnBluetooth func() // Android : cherche un clavier MIDI Bluetooth
	eng         *Engine
	host        Host
	cfg         *Config
	cfgPath     string
	fxBand      int // point de l'égaliseur sélectionné
	fxTr        int // piste dont les fenêtres EQ / EFFETS sont ouvertes
	now         func() time.Time

	mu       sync.Mutex
	toast    string
	toastEnd time.Time
	loading  [nCh]bool
	gen      [nCh]int

	srcCfg     [nCh]SrcCfg
	learn      learnState  // MIDI Learn : associations CC → réglages
	lastVel    [2]int32    // dernière vélocité reçue du clavier MIDI : avant / après la courbe
	icons      [nCh]int32  // icône de chaque piste, choisie automatiquement d'après le son
	labels     [nCh]string // noms des pistes choisis par l'utilisateur
	sf2s       map[string]*SF2
	libs       libState // bibliothèque de sons et pads (library.go, pads.go)
	pads       padUI
	libTrack   int   // piste pour laquelle la bibliothèque est ouverte
	splitLearn int32 // 1 : la prochaine note jouée devient le point de split (atomique)
	splitGot   int32 // note reçue + 1 (atomique)
	splitDef   int   // dernier point de split choisi

	namer     ChordNamer
	mqMu      sync.Mutex
	midiQ     []uint32
	chordKey  [2]uint64
	chord     ChordResult
	chordLive bool // des touches sont tenues en ce moment
	audioInfo string
	audioErr  string

	projPath  string // fichier projet en cours ("" = pas encore enregistré)
	changed   bool   // modifications non enregistrées dans le projet
	applying  bool   // on applique un ensemble de réglages (pas une modification de l'utilisateur)
	lastTitle string

	sel     int
	regions []region
	noReg   bool
	active  *region
	dragY   int
	dragV   float64
	mx, my  int
	lastID  string
	lastT   time.Time

	counts    map[int]int
	kbDown    map[uint32]int
	scanNote  map[uint32]int
	mouseNote int
	midiDown  map[int]bool
	spaceDown bool
	lblB      map[int]string
	lblT      map[int]string
	whites    []int
	whiteX    map[int]int
	blacks    []blackKey

	midiStatus string
	midiOK     bool
	OnRescan   func()
	ov         *overlay

	dirty    bool
	lastSave time.Time
	meterWas bool
}

var (
	scBottom = []uint32{0x2C, 0x1F, 0x2D, 0x20, 0x2E, 0x2F, 0x22, 0x30, 0x23, 0x31, 0x24, 0x32, 0x33, 0x26, 0x34, 0x27, 0x35}
	scTop    = []uint32{0x10, 0x03, 0x11, 0x04, 0x12, 0x13, 0x06, 0x14, 0x07, 0x15, 0x08, 0x16, 0x17, 0x0A, 0x18, 0x0B, 0x19}
	qBottom  = "ZSXDCVGBHNJM,L.;/"
	qTop     = "Q2W3ER5T6Y7UI9O0P"
)

const (
	vkBack   = 0x08
	vkReturn = 0x0D
	vkEscape = 0x1B
	vkSpace  = 0x20
	vkPrior  = 0x21
	vkNext   = 0x22
	vkEnd    = 0x23
	vkHome   = 0x24
	vkUp     = 0x26
	vkDown   = 0x28
)

// NewUI crée l'interface. cfgPath == "" : pas de sauvegarde sur disque.
func NewUI(eng *Engine, host Host, cfgPath string) *UI {
	u := &UI{eng: eng, host: host, cfgPath: cfgPath, now: time.Now,
		sf2s: map[string]*SF2{}, counts: map[int]int{}, kbDown: map[uint32]int{}, scanNote: map[uint32]int{},
		midiDown: map[int]bool{}, whiteX: map[int]int{}, mouseNote: -1, lastSave: time.Now(),
		pads: padUI{playing: -1, loading: -1, pendKey: -1}}
	if cfgPath != "" {
		u.cfg = loadConfig(cfgPath)
	} else {
		u.cfg = defaultConfig()
	}
	for i, sc := range scBottom {
		u.scanNote[sc] = 48 + i
	}
	for i, sc := range scTop {
		u.scanNote[sc] = 60 + i
	}
	wi := 0
	for n := lowNote; n <= highNote; n++ {
		switch n % 12 {
		case 0, 2, 4, 5, 7, 9, 11:
			u.whites = append(u.whites, n)
			u.whiteX[n] = kbX + wi*whiteW
			wi++
		}
	}
	for n := lowNote; n <= highNote; n++ {
		switch n % 12 {
		case 1, 3, 6, 8, 10:
			x := u.whiteX[n-1] + whiteW - blackW/2
			u.blacks = append(u.blacks, blackKey{n, rect{x, kbY, blackW, blackH}})
		}
	}
	u.RefreshKeyLabels()
	u.eng.SetMaster(u.cfg.Master)
	u.eng.SetReverb(u.cfg.Reverb)
	u.eng.SetLeslieFast(u.cfg.LeslieFast)
	u.libLoad()
	u.eng.Pads.SetVol(u.padCfg().Vol)
	u.applyFX()
	u.applyPlaying()
	u.learnLoad(u.cfg.Learn)
	u.applySetup(u.cfg.Setup)
	u.projPath, u.changed = u.cfg.Project, u.cfg.Changed
	SetLang(langFromCode(u.cfg.Lang))
	u.setNotation()
	u.lic = licenseFactory()
	u.refreshLicense()
	u.licShow = u.licSt.State != LicLicensed
	return u
}

// applique les réglages de jeu au clavier MIDI : aftertouch et courbe de vélocité
func (u *UI) applyPlaying() {
	c := u.cfg
	if c.VelGamma == 0 || c.ATAmt == 0 || c.VelHi == 0 || c.VelLo == 0 { // ancienne configuration : valeurs par défaut
		c.sanitize()
	}
	u.eng.SetAftertouchMode(c.ATMode, c.ATAmt)
	u.eng.SetVelCurve(c.VelGamma, c.VelLo, c.VelHi)
}

func (u *UI) RefreshKeyLabels() {
	u.lblB, u.lblT = map[int]string{}, map[int]string{}
	for i, sc := range scBottom {
		u.lblB[48+i] = u.host.KeyLabel(sc, string(qBottom[i]))
	}
	for i, sc := range scTop {
		u.lblT[60+i] = u.host.KeyLabel(sc, string(qTop[i]))
	}
}

// ---------------------------------------------------------------- messages et état

func (u *UI) Toast(msg string) {
	u.mu.Lock()
	u.toast = T(msg)
	u.toastEnd = u.now().Add(5 * time.Second)
	u.mu.Unlock()
	u.host.Invalidate()
}

func (u *UI) SetMidiStatus(ok bool, s string) {
	u.midiOK, u.midiStatus = ok, s
	u.host.Invalidate()
}

// modifie un réglage de canal sans que le moteur audio ne le lise à moitié écrit
func (u *UI) with(f func()) {
	u.eng.mu.Lock()
	f()
	u.eng.mu.Unlock()
}

// sauvegarde automatique de la session
func (u *UI) markDirty() {
	u.dirty = true
	if !u.applying {
		u.changed = true
	}
}

// modification faite par l'utilisateur : le projet devient « non enregistré »
func (u *UI) touch() { u.markDirty() }

// instantané des 8 canaux (réglages + son choisi)
func (u *UI) snapshot() [nCh]ChanCfg {
	var s [nCh]ChanCfg
	for i, ch := range u.eng.ch {
		s[i] = ChanCfg{Label: u.labels[i], Src: u.srcCfg[i], Vol: ch.Vol, Pan: ch.Pan, Oct: ch.Oct, Trans: ch.Trans,
			Mute: ch.Mute, Solo: ch.Solo, Hold: ch.Hold, Pedal: ch.Pedal, Filter: ch.Filter, ModFX: ch.ModFX, ATOff: !ch.AT, P: ch.P,
			Zone: rangeZone(ch.KeyLo, ch.KeyHi)}
	}
	return s
}

func (u *UI) applySetup(s [nCh]ChanCfg) {
	u.applying = true
	defer func() { u.applying = false }()
	for i := range s {
		cc := s[i]
		cc.sanitize()
		u.labels[i] = cc.Label
		ch := u.eng.ch[i]
		ch.Vol, ch.Pan, ch.Oct, ch.Trans = cc.Vol, cc.Pan, cc.Oct, cc.Trans
		ch.Mute, ch.Solo = cc.Mute, cc.Solo
		ch.Filter, ch.ModFX, ch.P = cc.Filter, cc.ModFX, cc.P
		ch.AT = !cc.ATOff
		ch.KeyLo, ch.KeyHi = zoneRange(cc.Zone)
		u.eng.SetHold(i, cc.Hold)
		u.eng.SetPedalAware(i, cc.Pedal)
		if u.srcCfg[i] != cc.Src || (cc.Src.Kind != "" && u.eng.ch[i].src == nil && !u.isLoading(i)) {
			u.loadSource(i, cc.Src)
		}
	}
	u.markDirty()
	u.host.Invalidate()
}

func (u *UI) isLoading(i int) bool {
	u.mu.Lock()
	defer u.mu.Unlock()
	return u.loading[i]
}

// ---------------------------------------------------------------- sons : intégrés, SF2, WAV, MP3

func (u *UI) getSF2(path string) (*SF2, error) {
	u.mu.Lock()
	sf, ok := u.sf2s[path]
	u.mu.Unlock()
	if ok {
		return sf, nil
	}
	sf, err := OpenSF2(path)
	if err != nil {
		return nil, err
	}
	u.mu.Lock()
	u.sf2s[path] = sf
	u.mu.Unlock()
	return sf, nil
}

func baseNoExt(p string) string { return strings.TrimSuffix(filepath.Base(p), filepath.Ext(p)) }

func (u *UI) setIcon(i, k int) { atomic.StoreInt32(&u.icons[i], int32(k)) }
func (u *UI) icon(i int) int   { return int(atomic.LoadInt32(&u.icons[i])) }

func (u *UI) loadSource(i int, sc SrcCfg) {
	u.srcCfg[i] = sc
	u.mu.Lock()
	u.gen[i]++
	g := u.gen[i]
	u.mu.Unlock()
	switch sc.Kind {
	case "sf2":
		u.setIcon(i, soundIcon(sc.Name, sc.File, true, sc.Bank, sc.Prog)) // provisoire, affiné quand le son est chargé
	case "audio":
		u.setIcon(i, soundIcon(sc.Name, sc.File, false, 0, 0))
	default:
		u.setIcon(i, icNote)
	}
	switch sc.Kind {
	case "builtin":
		b := sc.Builtin
		if b < 0 || b >= nInst {
			b = 0
		}
		if k := iconFromName(instLabels[b]); k >= 0 {
			u.setIcon(i, k)
		} else {
			u.setIcon(i, iconForCat("INTÉGRÉ · "+strings.ToUpper(instCats[b]), false))
		}
		u.eng.SetSource(i, builtinSource{b}, instLabels[b], "INTÉGRÉ · "+strings.ToUpper(instCats[b]))
		u.mu.Lock()
		u.loading[i] = false
		u.mu.Unlock()
	case "sf2", "audio":
		u.mu.Lock()
		u.loading[i] = true
		u.mu.Unlock()
		go func() {
			t0 := time.Now()
			startDiag(u.eng)
			atomic.AddInt32(&loadsNow, 1)
			defer func() {
				atomic.AddInt32(&loadsNow, -1)
				lklog("chargement du son « %s » terminé en %d ms", filepath.Base(sc.File), time.Since(t0).Milliseconds())
			}()
			var src Source
			var name, cat string
			var err error
			ic := -1
			if sc.Kind == "sf2" {
				src, name, cat, ic, err = u.loadSF2Preset(sc)
			} else {
				var ss *sampleSource
				ss, name, err = LoadAudioFile(sc.File)
				src = ss
				cat = "AUDIO · " + strings.ToUpper(strings.TrimPrefix(filepath.Ext(sc.File), "."))
				if err == nil {
					ic = soundIcon(name, sc.File, false, 0, 0)
				}
			}
			u.mu.Lock()
			if u.gen[i] != g { // un choix plus récent a pris la place
				u.mu.Unlock()
				return
			}
			u.loading[i] = false
			u.mu.Unlock()
			if err != nil {
				fallback := sc.Name
				if fallback == "" {
					fallback = baseNoExt(sc.File)
				}
				u.eng.SetSource(i, nil, fallback, "ERREUR · NON CHARGÉ")
				u.Toast(err.Error())
			} else {
				u.eng.SetSource(i, src, name, cat)
				if ic >= 0 {
					u.setIcon(i, ic)
				}
			}
			u.host.Invalidate()
		}()
	default:
		u.eng.SetSource(i, nil, "", "")
		u.mu.Lock()
		u.loading[i] = false
		u.mu.Unlock()
	}
	u.markDirty()
	u.host.Invalidate()
}

func (u *UI) loadSF2Preset(sc SrcCfg) (Source, string, string, int, error) {
	sf, err := u.getSF2(sc.File)
	if err != nil {
		return nil, "", "", -1, fmt.Errorf("%s : %v", filepath.Base(sc.File), err)
	}
	idx := -1
	for _, p := range sf.Presets() {
		if p.Bank == sc.Bank && p.Num == sc.Prog {
			idx = p.Index
			break
		}
	}
	if idx < 0 {
		for _, p := range sf.Presets() { // à défaut, on cherche par le nom
			if sc.Name != "" && p.Name == sc.Name {
				idx = p.Index
				break
			}
		}
	}
	if idx < 0 {
		return nil, "", "", -1, fmt.Errorf("son introuvable dans %s", filepath.Base(sc.File))
	}
	regs, err := sf.LoadPreset(idx)
	if err != nil {
		return nil, "", "", -1, fmt.Errorf("%s : %v", sf.phdr[idx].name, err)
	}
	title := sf.Name
	if title == "" {
		title = baseNoExt(sc.File)
	}
	ph := sf.phdr[idx]
	return &sampleSource{regions: regs}, ph.name, "SF2 · " + strings.ToUpper(title),
		soundIcon(ph.name, baseNoExt(sc.File), true, int(ph.bank), int(ph.num)), nil
}

func (u *UI) addRecent(path string) {
	out := []string{path}
	for _, r := range u.cfg.Recent {
		if r != path && len(out) < 6 {
			out = append(out, r)
		}
	}
	u.cfg.Recent = out
	u.markDirty()
}

// ouvre un fichier choisi par l'utilisateur pour le canal ch
func (u *UI) openFileFor(ch int, path string) {
	switch strings.ToLower(filepath.Ext(path)) {
	case ".sf2", ".sf3":
		sf, err := u.getSF2(path)
		if err != nil {
			u.Toast(filepath.Base(path) + " : " + err.Error())
			return
		}
		u.addRecent(path)
		u.openPresetList(ch, sf, path)
	case ".wav", ".wave", ".mp3":
		u.addRecent(path)
		u.loadSource(ch, SrcCfg{Kind: "audio", File: path, Name: baseNoExt(path)})
	default:
		u.Toast("Format non pris en charge : choisis un fichier SF2, SF3, WAV ou MP3.")
	}
}

// ---------------------------------------------------------------- fenêtres de choix (overlay)

func (u *UI) closeOverlay() { u.ov = nil; u.host.Invalidate() }

func (u *UI) openPresetList(ch int, sf *SF2, path string) {
	var items []olItem
	for _, p := range sf.Presets() {
		p := p
		items = append(items, olItem{text: p.Name, sub: fmt.Sprintf("%03d:%03d", p.Bank, p.Num), action: func() {
			u.loadSource(ch, SrcCfg{Kind: "sf2", File: path, Bank: p.Bank, Prog: p.Num, Name: p.Name})
		}})
	}
	title := sf.Name
	if title == "" {
		title = baseNoExt(path)
	}
	u.ov = &overlay{title: title + "  ·  " + Tf("%d sons", len(items)), items: items, filterable: true,
		hint: "Tape pour filtrer · ↑ ↓ pour choisir · Entrée pour valider · Échap pour fermer"}
	u.host.Invalidate()
}

// fenêtre de saisie d'un texte
func (u *UI) promptText(title, initial string, onOK func(string)) {
	u.ov = &overlay{title: title, input: true, text: initial,
		hint: "Entrée pour valider · Échap pour annuler", onOK: onOK}
	u.host.Invalidate()
}

func (u *UI) openRename(preset int) {
	u.promptText(Tf("Nom du preset %d", preset+1), u.cfg.Presets[preset].Name, func(s string) {
		if s = cleanName(s, 28); s != "" {
			u.cfg.Presets[preset].Name = s
			u.touch()
		}
	})
}

func (u *UI) openRenameTrack(i int) {
	u.promptText(Tf("Nom de la piste %d  (vide = nom du son)", i+1), u.labels[i], func(s string) {
		u.labels[i] = cleanName(s, 28)
		u.touch()
	})
}

// nom affiché d'une piste : le nom choisi, sinon celui du son
func (u *UI) trackTitle(i int) string {
	if u.labels[i] != "" {
		return u.labels[i]
	}
	if _, n, _ := u.eng.ChInfo(i); n != "" {
		return n
	}
	return Tf("Piste %d", i+1)
}

func (o *overlay) visible() []int { // indices des éléments affichés
	var out []int
	f := strings.ToLower(o.filter)
	for i, it := range o.items {
		if f != "" {
			if it.header || !strings.Contains(strings.ToLower(it.text), f) {
				continue
			}
		}
		out = append(out, i)
	}
	return out
}

func (o *overlay) firstSelectable() int {
	for i, it := range o.items {
		if !it.header {
			return i
		}
	}
	return 0
}

const ovRows = 14

func (o *overlay) clampScroll(vis []int) {
	pos := -1
	for k, i := range vis {
		if i == o.sel {
			pos = k
		}
	}
	if pos < 0 {
		return
	}
	if pos < o.scroll {
		o.scroll = pos
	}
	if pos >= o.scroll+ovRows {
		o.scroll = pos - ovRows + 1
	}
	if o.scroll > len(vis)-ovRows {
		o.scroll = len(vis) - ovRows
	}
	if o.scroll < 0 {
		o.scroll = 0
	}
}

func (o *overlay) move(d int) {
	vis := o.visible()
	if len(vis) == 0 {
		return
	}
	pos := -1
	for k, i := range vis {
		if i == o.sel {
			pos = k
		}
	}
	step := 1
	if d < 0 {
		step = -1
	}
	n := d
	if n < 0 {
		n = -n
	}
	for ; n > 0; n-- {
		next := pos + step
		for next >= 0 && next < len(vis) && o.items[vis[next]].header {
			next += step
		}
		if next < 0 || next >= len(vis) {
			break
		}
		pos = next
	}
	if pos < 0 {
		pos = 0
		for pos < len(vis) && o.items[vis[pos]].header {
			pos++
		}
	}
	if pos < len(vis) {
		o.sel = vis[pos]
	}
	o.clampScroll(vis)
}

func (u *UI) overlayChoose(idx int) {
	o := u.ov
	if o == nil || idx < 0 || idx >= len(o.items) || o.items[idx].header || o.items[idx].action == nil {
		return
	}
	act := o.items[idx].action
	u.ov = nil
	act()
	u.host.Invalidate()
}

func (u *UI) overlayKey(vk int) {
	o := u.ov
	switch vk {
	case vkEscape:
		u.closeOverlay()
	case vkReturn:
		if o.input {
			ok, txt := o.onOK, o.text
			u.ov = nil
			if ok != nil {
				ok(txt)
			}
			u.host.Invalidate()
		} else {
			vis := o.visible()
			if len(vis) > 0 {
				found := false
				for _, i := range vis {
					if i == o.sel {
						found = true
					}
				}
				if !found {
					o.sel = vis[0]
				}
				u.overlayChoose(o.sel)
			}
		}
	case vkBack:
		if o.input {
			if r := []rune(o.text); len(r) > 0 {
				o.text = string(r[:len(r)-1])
			}
		} else if o.back != nil && !o.filterable {
			o.back()
		} else if o.filterable {
			if r := []rune(o.filter); len(r) > 0 {
				o.filter = string(r[:len(r)-1])
				o.scroll = 0
				if v := o.visible(); len(v) > 0 {
					o.sel = v[0]
				}
			}
		}
	case vkUp:
		o.move(-1)
	case vkDown:
		o.move(1)
	case vkPrior:
		o.move(-ovRows)
	case vkNext:
		o.move(ovRows)
	case vkHome:
		o.move(-100000)
	case vkEnd:
		o.move(100000)
	}
	u.host.Invalidate()
}

// ---------------------------------------------------------------- événements : souris

func (u *UI) MouseDown(x, y int) {
	u.mx, u.my = x, y
	for i := len(u.regions) - 1; i >= 0; i-- {
		r := u.regions[i]
		if !r.r.has(x, y) || (r.down == nil && r.dbl == nil) {
			continue
		}
		now := u.now()
		if r.dbl != nil && r.id != "" && r.id == u.lastID && now.Sub(u.lastT) < 400*time.Millisecond {
			u.lastID = ""
			u.active = nil
			r.dbl()
			u.host.Invalidate()
			return
		}
		u.lastID, u.lastT = r.id, now
		rr := r
		u.active = &rr
		if r.down != nil {
			r.down(x, y)
		}
		u.host.Invalidate()
		return
	}
	if u.ov != nil {
		u.closeOverlay()
	}
}

func (u *UI) MouseMove(x, y int, left bool) {
	u.mx, u.my = x, y
	if u.active != nil && left && u.active.drag != nil {
		u.active.drag(x, y)
	}
	u.host.Invalidate()
}

func (u *UI) MouseUp(x, y int) {
	u.mx, u.my = x, y
	if u.active != nil {
		a := u.active
		u.active = nil
		if a.up != nil {
			a.up(x, y)
		}
	}
	u.host.Invalidate()
}

// delta > 0 : molette vers le haut
func (u *UI) Wheel(delta int) {
	for i := len(u.regions) - 1; i >= 0; i-- {
		r := u.regions[i]
		if r.wheel != nil && r.r.has(u.mx, u.my) {
			r.wheel(delta)
			u.host.Invalidate()
			return
		}
	}
}

// ---------------------------------------------------------------- événements : clavier de l'ordinateur

func (u *UI) KeyDown(vk int, sc uint32, repeat, ext bool) {
	if u.licShow {
		u.licKeyDown(vk)
		return
	}
	if vk == vkEscape && u.learnCancel() {
		return
	}
	if u.ov != nil {
		u.overlayKey(vk)
		return
	}
	if ctrl, shift := u.host.Mods(); ctrl {
		if !repeat && !u.Plugin {
			switch vk {
			case 'S':
				if shift {
					u.SaveProjectAs()
				} else {
					u.SaveProject()
				}
			case 'O':
				u.OpenProjectMenu()
			case 'N':
				u.NewProject()
			}
		}
		return // Ctrl + lettre ne joue jamais de note
	}
	if vk == vkSpace {
		if !repeat {
			u.spaceDown = true
			u.eng.SetSustain(true)
			u.host.Invalidate()
		}
		return
	}
	if vk == vkEscape {
		u.PanicAll()
		return
	}
	if repeat || ext {
		return
	}
	n, ok := u.scanNote[sc]
	if !ok {
		return
	}
	if _, down := u.kbDown[sc]; down {
		return
	}
	u.kbDown[sc] = n
	u.press(2000+int(sc), n, 100)
	u.host.Invalidate()
}

func (u *UI) KeyUp(vk int, sc uint32) {
	if vk == vkSpace {
		if u.spaceDown {
			u.spaceDown = false
			u.eng.SetSustain(false)
			u.host.Invalidate()
		}
		return
	}
	if n, ok := u.kbDown[sc]; ok {
		delete(u.kbDown, sc)
		u.release(2000+int(sc), n)
		u.host.Invalidate()
	}
}

// caractère tapé (pour le filtre de la liste et la saisie d'un nom)
func (u *UI) Char(r rune) {
	if u.licShow {
		u.licChar(r)
		return
	}
	o := u.ov
	if o == nil || r < 32 || r == 127 {
		return
	}
	switch {
	case o.input:
		if len([]rune(o.text)) < 28 {
			o.text += string(r)
		}
	case o.filterable:
		o.filter += string(r)
		o.scroll = 0
		if v := o.visible(); len(v) > 0 {
			o.sel = v[0]
		}
	}
	u.host.Invalidate()
}

// vrai quand l'interface attend du texte (champ de saisie, filtre, clé de licence) : Android ouvre alors le clavier
func (u *UI) WantsText() bool {
	if u.licShow {
		return true
	}
	return u.ov != nil && (u.ov.input || u.ov.filterable)
}

func (u *UI) FocusLost() {
	for sc, n := range u.kbDown {
		u.release(2000+int(sc), n)
	}
	u.kbDown = map[uint32]int{}
	if u.spaceDown {
		u.spaceDown = false
		u.eng.SetSustain(false)
	}
	u.host.Invalidate()
}

// ---------------------------------------------------------------- notes

func (u *UI) press(tag, note, vel int) {
	if u.noSound() {
		u.Toast("Aucun son chargé : double-clique une piste (ou « SON ») pour en ajouter un.")
	}
	u.splitCatch(note)
	u.eng.NoteOn(note, vel, tag)
	u.counts[note]++
}

func (u *UI) noSound() bool {
	for i := range u.eng.ch {
		if src, _, _ := u.eng.ChInfo(i); src != nil {
			return false
		}
	}
	return true
}

func (u *UI) release(tag, note int) {
	u.eng.NoteOff(tag)
	if u.counts[note] > 0 {
		u.counts[note]--
	}
}

func (u *UI) PanicAll() {
	u.eng.Panic()
	u.kbDown = map[uint32]int{}
	u.counts = map[int]int{}
	u.midiDown = map[int]bool{}
	u.mouseNote = -1
	u.spaceDown = false
	u.host.Invalidate()
}

func (u *UI) keyAt(x, y int) int {
	if y < kbY || y >= kbY+whiteH {
		return -1
	}
	if y < kbY+blackH {
		for _, b := range u.blacks {
			if b.r.has(x, y) {
				return b.note
			}
		}
	}
	idx := (x - kbX) / whiteW
	if x >= kbX && idx >= 0 && idx < len(u.whites) {
		return u.whites[idx]
	}
	return -1
}

func mouseVel(y int) int {
	v := 45 + 82*(y-kbY)/whiteH
	if v < 40 {
		v = 40
	}
	if v > 127 {
		v = 127
	}
	return v
}

// ---------------------------------------------------------------- MIDI

// MidiFast : appelé directement par le pilote MIDI (hors du thread de l'interface) :
// la note part tout de suite vers le moteur, sans attendre l'affichage. L'écran est mis à jour ensuite.
func (u *UI) MidiFast(msg uint32) {
	status := msg & 0xFF
	d1 := int((msg >> 8) & 0x7F)
	d2 := int((msg >> 16) & 0x7F)
	switch status & 0xF0 {
	case 0x90:
		if d2 > 0 {
			in := d2
			d2 = u.eng.MapVel(d2) // courbe de vélocité réglée dans les Paramètres
			u.lastVel[0], u.lastVel[1] = int32(in), int32(d2)
			u.splitCatch(d1)
			u.eng.NoteOn(d1, d2, 1000+d1)
		} else {
			u.eng.NoteOff(1000 + d1)
		}
	case 0x80:
		u.eng.NoteOff(1000 + d1)
	case 0xB0:
		if u.learnMidi(d1, d2) {
			return // consommé par l'apprentissage
		}
		u.eng.Control(d1, d2)
	case 0xE0:
		u.eng.SetBend((d2<<7 | d1) - 8192)
	case 0xD0: // aftertouch de canal
		u.eng.SetAftertouch(d1)
		return
	case 0xA0: // aftertouch par touche
		u.eng.PolyPressure(1000+d1, d2)
		return
	default:
		return
	}
	u.mqMu.Lock()
	if len(u.midiQ) < 4096 {
		u.midiQ = append(u.midiQ, msg)
	}
	u.mqMu.Unlock()
}

// l'interface rattrape les messages MIDI reçus (touches allumées, molettes)
func (u *UI) DrainMidi() {
	u.mqMu.Lock()
	q := u.midiQ
	u.midiQ = nil
	u.mqMu.Unlock()
	for _, msg := range q {
		status := msg & 0xFF
		d1 := int((msg >> 8) & 0x7F)
		d2 := int((msg >> 16) & 0x7F)
		switch status & 0xF0 {
		case 0x90:
			if d2 > 0 {
				if !u.midiDown[d1] {
					u.midiDown[d1] = true
					u.counts[d1]++
				}
			} else {
				u.midiMark(d1)
			}
		case 0x80:
			u.midiMark(d1)
		}
	}
	if len(q) > 0 {
		u.host.Invalidate()
	}
}

// pour les tests et les appels depuis l'interface
func (u *UI) MidiMsg(msg uint32) {
	u.MidiFast(msg)
	u.DrainMidi()
}

func (u *UI) midiMark(n int) {
	if u.midiDown[n] {
		delete(u.midiDown, n)
		if u.counts[n] > 0 {
			u.counts[n]--
		}
	}
}

// ---------------------------------------------------------------- horloge et sauvegarde

// appelé régulièrement (environ 25 fois par seconde) par la plateforme
func (u *UI) Tick() {
	if u.tickLicense() {
		u.host.Invalidate()
	}
	redraw := false
	u.mu.Lock()
	if u.toast != "" && u.now().After(u.toastEnd) {
		u.toast = ""
		redraw = true
	}
	anyLoading := false
	for _, l := range u.loading {
		anyLoading = anyLoading || l
	}
	u.mu.Unlock()
	active := anyLoading
	for _, ch := range u.eng.ch {
		if ch.peak > 0.0003 {
			active = true
		}
	}
	if active || u.meterWas {
		redraw = true
	}
	u.meterWas = active
	if t := u.windowTitle(); t != u.lastTitle {
		u.lastTitle = t
		u.host.SetTitle(t)
		redraw = true
	}
	if redraw {
		u.host.Invalidate()
	}
	u.updateChord() // (aussi quand rien n'est redessiné : les pads suivent les accords)
	u.padTick()
	u.splitTick()
	if u.dirty && u.now().Sub(u.lastSave) > 1500*time.Millisecond {
		u.Save()
	}
}

func (u *UI) Save() {
	u.dirty = false
	u.lastSave = u.now()
	if u.cfgPath == "" {
		return
	}
	u.cfg.Setup = u.snapshot()
	u.cfg.Learn = u.learnList()
	u.cfg.Master = u.eng.master
	u.cfg.Reverb = u.eng.revAmt
	u.cfg.LeslieFast = u.eng.LeslieFast()
	u.cfg.Project, u.cfg.Changed = u.projPath, u.changed
	u.cfg.Lang = langCode(CurLang())
	_ = saveConfig(u.cfgPath, u.cfg)
}

func (u *UI) Shutdown() {
	u.Save()
	for _, sf := range u.sf2s {
		sf.Close()
	}
}

// dernier garde-fou : un chemin qui n'existe plus ne doit pas faire planter
func fileExists(p string) bool { _, err := os.Stat(p); return err == nil }
