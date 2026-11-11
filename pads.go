package main

// Pads continus : une nappe de son tenue en fond, dans la tonalité du morceau.
//
// L'utilisateur choisit un dossier qui contient un pad par tonalité (majeur et mineur : jusqu'à 24 fichiers WAV / MP3).
// Layers Keys lit le nom de chaque fichier (« C Major Pad.wav », « F#m.mp3 », « Pad La mineur.wav », « Bb.wav »…)
// pour savoir quelle tonalité il contient, puis :
//   - en mode « suit les accords » : le pad change tout seul quand un accord est tenu un court instant ;
//   - en mode « manuel » : on choisit la tonalité avec les 24 boutons de la fenêtre des pads.
// Le passage d'un pad à l'autre se fait en fondu enchaîné (pas de coupure), chaque pad boucle sans coupure,
// et l'arrêt se fait en fondu.
// Le son des pads est mélangé à la sortie, après les pistes (comme le lecteur audio). Il respecte la licence.

import (
	"errors"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	padFade      = 2.5  // secondes du fondu enchaîné entre deux pads
	padStopFade  = 2.0  // secondes du fondu à l'arrêt
	padLoopXfade = 1.5  // secondes de fondu à la boucle (fin du fichier → début)
	padFollowMin = 0.35 // secondes : un accord doit être tenu ce temps-là avant que le pad change (mode automatique)
	padCacheMax  = 3    // pads gardés décodés en mémoire
)

// PadCfg : réglages des pads continus (enregistrés dans bibliotheque.json, communs à l'application et aux plugins).
type PadCfg struct {
	Dir    string     `json:"dir,omitempty"`
	Files  [24]string `json:"files"`            // [tonalité] : 0-11 majeur (do … si), 12-23 mineur
	On     bool       `json:"on,omitempty"`     // pads allumés
	Manual bool       `json:"manual,omitempty"` // faux : suit les accords joués
	Key    int        `json:"key"`              // tonalité choisie (0-23)
	Vol    float64    `json:"vol"`
}

func defaultPadCfg() PadCfg { return PadCfg{Vol: 0.6} }

func (p *PadCfg) sanitize() {
	if p.Key < 0 || p.Key > 23 {
		p.Key = 0
	}
	if math.IsNaN(p.Vol) || p.Vol < 0 || p.Vol > 1 {
		p.Vol = 0.6
	}
}

func (p *PadCfg) count() int {
	n := 0
	for _, f := range p.Files {
		if f != "" {
			n++
		}
	}
	return n
}

// padKeyName : « Do », « Do# »… + « majeur / mineur » (ou lettres selon la notation choisie)
func padKeyName(n ChordNamer, key int) string {
	pc, minor := key%12, key >= 12
	name := n.nm(pc, sharpFamily(pc, minor))
	if minor {
		return name + " " + T("mineur")
	}
	return name + " " + T("majeur")
}

// ---------------------------------------------------------------- lecture des noms de fichiers

var (
	padLetterRe = regexp.MustCompile(`(^|[^a-z0-9])([a-g])(#|b|sharp|flat| sharp| flat| diese| bemol)?(m|min|minor|mineur|moll|maj|major|majeur|dur)?($|[^a-z0-9])`)
	padSolfRe   = regexp.MustCompile(`(^|[^a-z0-9])(do|re|mi|fa|sol|la|si)(#|b| diese| bemol|diese|bemol)?( ?m| ?min| ?mineur| ?minor| ?maj| ?majeur| ?major)?($|[^a-z0-9])`)
	padMinorRe  = regexp.MustCompile(`(^|[^a-z])(minor|mineur|moll|min)($|[^a-z])`)
)

var padLetterPC = map[string]int{"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
var padSolfPC = map[string]int{"do": 0, "re": 2, "mi": 4, "fa": 5, "sol": 7, "la": 9, "si": 11}

func padFold(s string) string {
	s = strings.ToLower(s)
	s = strings.NewReplacer("♯", "#", "♭", "b", "é", "e", "è", "e", "ê", "e", "_", " ", "-", " ", ".", " ").Replace(s)
	return " " + strings.Join(strings.Fields(s), " ") + " "
}

func padAcc(a string) int {
	switch strings.TrimSpace(a) {
	case "#", "sharp", "diese":
		return 1
	case "b", "flat", "bemol":
		return -1
	}
	return 0
}

func padQual(q string, minorHint bool) bool {
	switch strings.TrimSpace(q) {
	case "m", "min", "minor", "mineur", "moll":
		return true
	case "maj", "major", "majeur", "dur":
		return false
	}
	return minorHint
}

// padKeyFromName : tonalité (0-23) écrite dans un nom de fichier ; -1 si on ne la trouve pas.
// dirs = noms des dossiers parents (un dossier « Minor » ou « Mineur » rend mineurs les fichiers sans précision).
func padKeyFromName(file string, dirs ...string) int {
	minorHint := false
	for _, d := range dirs {
		if padMinorRe.MatchString(padFold(d)) {
			minorHint = true
		}
	}
	n := padFold(baseNoExt(file))
	// les mots « pad », « warm », « ambient »… contiennent des lettres a-g : on n'accepte qu'une note isolée
	best, bestScore := -1, -1
	for _, m := range padLetterRe.FindAllStringSubmatchIndex(n, -1) {
		letter := n[m[4]:m[5]]
		acc, qual := "", ""
		if m[6] >= 0 {
			acc = n[m[6]:m[7]]
		}
		if m[8] >= 0 {
			qual = n[m[8]:m[9]]
		}
		// « b » seul après une note (« Bb ») est un bémol, mais « b » seul est la note si
		score := 1
		if acc != "" || qual != "" {
			score = 3
		}
		if letter == "a" && acc == "" && qual == "" {
			score = 0 // « a » peut être un mot anglais (« a pad ») : dernier recours
		}
		if score >= bestScore {
			pc := (padLetterPC[letter] + padAcc(acc) + 12) % 12
			key := pc
			if padQual(qual, minorHint) {
				key += 12
			}
			best, bestScore = key, score
		}
	}
	// « mineur » / « minor » écrit plus loin dans le nom (« C Pad Minor »)
	if best >= 0 && best < 12 && padMinorRe.MatchString(n) {
		best += 12
	}
	if best >= 0 && bestScore > 0 {
		return best
	}
	// noms en solfège (« La mineur », « Sib majeur »)
	for _, m := range padSolfRe.FindAllStringSubmatchIndex(n, -1) {
		note := n[m[4]:m[5]]
		acc, qual := "", ""
		if m[6] >= 0 {
			acc = n[m[6]:m[7]]
		}
		if m[8] >= 0 {
			qual = n[m[8]:m[9]]
		}
		pc := (padSolfPC[note] + padAcc(acc) + 12) % 12
		key := pc
		if padQual(qual, minorHint) || padMinorRe.MatchString(n) {
			key += 12
		}
		return key
	}
	return best
}

// scanPads range les fichiers d'un dossier par tonalité (le premier trouvé gagne ; sous-dossiers sur 3 niveaux).
func scanPads(dir string) ([24]string, int, error) {
	var out [24]string
	st, err := os.Stat(dir)
	if err != nil {
		return out, 0, err
	}
	if !st.IsDir() {
		return out, 0, errors.New("ce n'est pas un dossier")
	}
	root := filepath.Clean(dir)
	var paths []string
	_ = filepath.WalkDir(root, func(p string, d os.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		rel, _ := filepath.Rel(root, p)
		if d.IsDir() {
			if p != root && (strings.HasPrefix(d.Name(), ".") || strings.Count(rel, string(filepath.Separator)) >= 3) {
				return filepath.SkipDir
			}
			return nil
		}
		if _, audio := isLibFile(d.Name()); audio && !strings.HasPrefix(d.Name(), ".") {
			paths = append(paths, p)
		}
		return nil
	})
	sort.Strings(paths)
	n := 0
	for _, p := range paths {
		rel, _ := filepath.Rel(root, filepath.Dir(p))
		var dirs []string
		if rel != "." {
			dirs = strings.Split(rel, string(filepath.Separator))
		}
		if k := padKeyFromName(p, dirs...); k >= 0 && out[k] == "" {
			out[k] = p
			n++
		}
	}
	return out, n, nil
}

// ---------------------------------------------------------------- lecture (thread audio)

type padVoice struct {
	data   []float32 // stéréo entrelacé
	rate   float64
	pos    float64
	gain   float64
	target float64
	step   float64 // variation de gain par image
}

type padPlayer struct {
	mu   sync.Mutex
	cur  *padVoice
	old  []*padVoice // pads en train de disparaître
	vol  float64
	gate func() bool // licence : faux = silence
}

func newPadPlayer() *padPlayer { return &padPlayer{vol: 0.6} }

func (p *padPlayer) SetVol(v float64) { p.mu.Lock(); p.vol = clamp01(v); p.mu.Unlock() }

// start fait entrer un nouveau pad (fondu enchaîné avec celui qui joue)
func (p *padPlayer) start(data []float32, rate float64) {
	v := &padVoice{data: data, rate: rate, target: 1, step: 1 / (padFade * SR)}
	p.mu.Lock()
	if p.cur != nil {
		p.cur.target, p.cur.step = 0, 1/(padFade*SR)
		p.old = append(p.old, p.cur)
		if len(p.old) > 3 {
			p.old = p.old[len(p.old)-3:]
		}
	}
	p.cur = v
	p.mu.Unlock()
}

// stop : fondu jusqu'au silence
func (p *padPlayer) stop() {
	p.mu.Lock()
	if p.cur != nil {
		p.cur.target, p.cur.step = 0, 1/(padStopFade*SR)
		p.old = append(p.old, p.cur)
		p.cur = nil
	}
	p.mu.Unlock()
}

func (p *padPlayer) active() bool {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.cur != nil || len(p.old) > 0
}

func (v *padVoice) render(l, r []float64, g float64) bool {
	n := len(v.data) / 2
	if n < 4 {
		return false
	}
	inc := v.rate / SR
	xf := padLoopXfade * v.rate
	if xf > float64(n)/3 {
		xf = float64(n) / 3
	}
	at := func(pos float64) (float64, float64) {
		k := int(pos)
		if k >= n-1 {
			k = n - 2
		}
		f := float32(pos - float64(k))
		a, b := v.data[2*k], v.data[2*k+2]
		c, d := v.data[2*k+1], v.data[2*k+3]
		return float64(a + (b-a)*f), float64(c + (d-c)*f)
	}
	for i := range l {
		if v.gain < v.target {
			v.gain = math.Min(v.target, v.gain+v.step)
		} else if v.gain > v.target {
			v.gain = math.Max(v.target, v.gain-v.step)
			if v.gain <= 0 && v.target <= 0 {
				return false
			}
		}
		sl, sr := at(v.pos)
		if loopStart := float64(n-1) - xf; v.pos > loopStart { // fin du fichier : fondu vers le début
			t := (v.pos - loopStart) / xf
			bl, br := at(v.pos - loopStart)
			a, b := math.Cos(t*math.Pi/2), math.Sin(t*math.Pi/2)
			sl, sr = sl*a+bl*b, sr*a+br*b
		}
		gg := math.Sin(v.gain*math.Pi/2) * g // fondu à puissance constante : pas de creux au milieu du fondu enchaîné
		l[i] += sl * gg
		r[i] += sr * gg
		v.pos += inc
		if v.pos >= float64(n-1) {
			v.pos -= float64(n-1) - xf
		}
	}
	return true
}

// mix ajoute les pads à la sortie (thread audio ; ne bloque jamais)
func (p *padPlayer) mix(l, r []float64) {
	if !p.mu.TryLock() {
		return
	}
	defer p.mu.Unlock()
	if p.cur == nil && len(p.old) == 0 {
		return
	}
	if p.gate != nil && !p.gate() {
		return
	}
	g := p.vol * p.vol * 0.9
	if p.cur != nil && !p.cur.render(l, r, g) {
		p.cur = nil
	}
	keep := p.old[:0]
	for _, v := range p.old {
		if v.render(l, r, g) {
			keep = append(keep, v)
		}
	}
	for i := len(keep); i < len(p.old); i++ {
		p.old[i] = nil
	}
	p.old = keep
}

// decodeStereo lit un WAV / MP3 en stéréo entrelacé (hors du thread audio)
func decodeStereo(path string) ([]float32, float64, error) {
	var a *pcmAudio
	var err error
	switch strings.ToLower(filepath.Ext(path)) {
	case ".wav", ".wave":
		a, err = readWAV(path)
	case ".mp3":
		var pcm []int16
		var ch, rate int
		pcm, ch, rate, err = decodeMP3(path)
		if err == nil {
			a = &pcmAudio{ch: ch, rate: rate, data: pcm}
		}
	default:
		return nil, 0, errors.New("les pads doivent être des fichiers WAV ou MP3")
	}
	if err != nil {
		return nil, 0, err
	}
	if a.ch < 1 || a.rate < 1000 || len(a.data) < a.ch*64 {
		return nil, 0, errors.New("fichier audio vide ou invalide")
	}
	frames := len(a.data) / a.ch
	d := make([]float32, frames*2)
	for i := 0; i < frames; i++ {
		lv := float32(a.data[i*a.ch]) / 32768
		rv := lv
		if a.ch > 1 {
			rv = float32(a.data[i*a.ch+1]) / 32768
		}
		d[2*i], d[2*i+1] = lv, rv
	}
	return d, float64(a.rate), nil
}

// ---------------------------------------------------------------- côté interface

type padUI struct {
	mu       sync.Mutex
	playing  int // tonalité qui joue (-1 : rien)
	loading  int // tonalité en cours de chargement (-1 : rien)
	cache    map[string]padData
	order    []string
	pendKey  int // mode automatique : accord vu, en attente de confirmation
	pendAt   time.Time
	lastSeen [2]uint64
}

type padData struct {
	d    []float32
	rate float64
}

func (u *UI) padCfg() PadCfg {
	l := u.library()
	u.libs.mu.Lock()
	defer u.libs.mu.Unlock()
	return l.Pads
}

func (u *UI) padSet(f func(p *PadCfg)) {
	u.library()
	u.libs.mu.Lock()
	cp := *u.libs.lib
	f(&cp.Pads)
	cp.Pads.sanitize()
	u.libs.lib = &cp
	u.libs.mu.Unlock()
	u.eng.Pads.SetVol(cp.Pads.Vol)
	u.libSave()
	u.host.Invalidate()
}

// padChooseDir : choisir le dossier des pads, puis le ranger par tonalité
func (u *UI) padChooseDir() {
	p, ok := u.host.OpenFile("padfolder")
	if !ok {
		return
	}
	u.padScanDir(p)
}

func (u *UI) padScanDir(dir string) {
	files, n, err := scanPads(dir)
	if err != nil {
		u.Toast(Tf("Dossier illisible : %s", err.Error()))
		return
	}
	u.padSet(func(p *PadCfg) { p.Dir, p.Files = dir, files })
	lklog("pads : %s → %d tonalités reconnues", dir, n)
	if n == 0 {
		u.Toast(T("Aucun pad reconnu : le nom de chaque fichier doit contenir sa tonalité (ex. « C Major », « F#m », « La mineur »)."))
		return
	}
	u.Toast(Tf("%d pads rangés par tonalité.", n))
	if cfg := u.padCfg(); cfg.On {
		u.padPlay(cfg.Key)
	}
}

// padPlay fait jouer la tonalité key (0-23) : chargement en arrière-plan, puis fondu enchaîné.
func (u *UI) padPlay(key int) {
	cfg := u.padCfg()
	if key < 0 || key > 23 {
		return
	}
	file := cfg.Files[key]
	u.pads.mu.Lock()
	if u.pads.playing == key || u.pads.loading == key {
		u.pads.mu.Unlock()
		return
	}
	if file == "" {
		u.pads.mu.Unlock()
		u.Toast(Tf("Pas de pad pour %s dans le dossier.", padKeyName(u.namer, key)))
		return
	}
	if pd, ok := u.pads.cache[file]; ok {
		u.pads.playing = key
		u.pads.mu.Unlock()
		u.eng.Pads.start(pd.d, pd.rate)
		u.host.Invalidate()
		return
	}
	u.pads.loading = key
	u.pads.mu.Unlock()
	u.host.Invalidate()
	go func() {
		defer recoverLib()
		d, rate, err := decodeStereo(file)
		u.pads.mu.Lock()
		stillWanted := u.pads.loading == key
		if u.pads.loading == key {
			u.pads.loading = -1
		}
		if err == nil {
			if u.pads.cache == nil {
				u.pads.cache = map[string]padData{}
			}
			u.pads.cache[file] = padData{d, rate}
			u.pads.order = append(u.pads.order, file)
			for len(u.pads.order) > padCacheMax {
				delete(u.pads.cache, u.pads.order[0])
				u.pads.order = u.pads.order[1:]
			}
			if stillWanted {
				u.pads.playing = key
			}
		}
		u.pads.mu.Unlock()
		if err != nil {
			u.Toast(filepath.Base(file) + " : " + err.Error())
		} else if stillWanted && u.padCfg().On {
			u.eng.Pads.start(d, rate)
		}
		u.host.Invalidate()
	}()
}

func (u *UI) padStop() {
	u.pads.mu.Lock()
	u.pads.playing, u.pads.loading = -1, -1
	u.pads.mu.Unlock()
	u.eng.Pads.stop()
	u.host.Invalidate()
}

// padToggle : allumer / éteindre les pads
func (u *UI) padToggle() {
	on := !u.padCfg().On
	u.padSet(func(p *PadCfg) { p.On = on })
	if !on {
		u.padStop()
		return
	}
	cfg := u.padCfg()
	if cfg.count() == 0 {
		u.Toast(T("Choisis d'abord le dossier des pads."))
		return
	}
	u.padPlay(cfg.Key)
}

// padChoose : bouton d'une tonalité (passe aussi en mode manuel)
func (u *UI) padChoose(key int) {
	u.padSet(func(p *PadCfg) { p.Key, p.On = key, true })
	u.padPlay(key)
}

// padFollow : appelé quand l'accord tenu change (mode automatique)
func (u *UI) padFollow(ch ChordResult, notes int) {
	cfg := u.padCfg()
	if !cfg.On || cfg.Manual || ch.Root < 0 || notes < 3 {
		u.pads.mu.Lock()
		u.pads.pendKey = -1
		u.pads.mu.Unlock()
		return
	}
	key := ch.Root
	if ch.Minor {
		key += 12
	}
	u.pads.mu.Lock()
	if u.pads.pendKey != key {
		u.pads.pendKey, u.pads.pendAt = key, u.now()
	}
	u.pads.mu.Unlock()
}

// padTick : confirme le changement automatique quand l'accord est tenu assez longtemps
func (u *UI) padTick() {
	u.pads.mu.Lock()
	key, at := u.pads.pendKey, u.pads.pendAt
	u.pads.mu.Unlock()
	if key < 0 || !u.chordLive || u.now().Sub(at) < time.Duration(padFollowMin*float64(time.Second)) {
		return
	}
	u.pads.mu.Lock()
	u.pads.pendKey = -1
	u.pads.mu.Unlock()
	cfg := u.padCfg()
	if cfg.Files[key] == "" || key == cfg.Key && u.padPlaying() == key {
		return
	}
	u.padSet(func(p *PadCfg) { p.Key = key })
	u.padPlay(key)
}

func (u *UI) padPlaying() int {
	u.pads.mu.Lock()
	defer u.pads.mu.Unlock()
	return u.pads.playing
}

func (u *UI) padLoading() int {
	u.pads.mu.Lock()
	defer u.pads.mu.Unlock()
	return u.pads.loading
}
