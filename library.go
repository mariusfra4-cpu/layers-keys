package main

// Bibliothèque de sons : l'utilisateur choisit UN dossier ; Layers Keys le parcourt (sous-dossiers compris),
// trouve tous les sons (SoundFont SF2 / SF3 : chaque preset est un son ; WAV / MP3 : un fichier = un son),
// les range par famille (piano, orgue, synthé, pad, strings, autres) et garde la liste dans un fichier.
//
// La liste est commune à l'application, au VST3 et à l'Audio Unit (fichier « bibliotheque.json » rangé à côté
// des réglages du logiciel) : on analyse le dossier une fois, puis les sons sont là à chaque ouverture.
// Les réglages des pads continus (pads.go) sont dans le même fichier.

import (
	"encoding/json"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	catPiano = iota
	catOrgan
	catSynth
	catPad
	catStrings
	catOther
	nCat
)

var catNames = [nCat]string{"PIANO", "ORGUE", "SYNTHÉ", "PAD", "STRINGS", "AUTRES"}

// icône représentative de chaque famille (titres des colonnes)
var catIcons = [nCat]int{icKeys, icOrgan, icWave, icCloud, icStrings, icLayers}

const (
	libVersion  = 1
	libMaxFiles = 4000 // garde-fou : un dossier énorme (disque entier) ne doit pas bloquer le logiciel
	libMaxDepth = 8
)

// LibSound : un son trouvé dans le dossier.
type LibSound struct {
	Name string `json:"n"`
	File string `json:"f"`
	Kind string `json:"k"` // "sf2" ou "audio"
	Bank int    `json:"b,omitempty"`
	Prog int    `json:"p,omitempty"`
	Cat  int    `json:"c"`
}

func (s LibSound) src() SrcCfg {
	if s.Kind == "sf2" {
		return SrcCfg{Kind: "sf2", File: s.File, Bank: s.Bank, Prog: s.Prog, Name: s.Name}
	}
	return SrcCfg{Kind: "audio", File: s.File, Name: s.Name}
}

// Library : contenu du fichier bibliotheque.json.
type Library struct {
	Version int        `json:"version"`
	Dir     string     `json:"dir,omitempty"`     // dossier de sons choisi
	Scanned string     `json:"scanned,omitempty"` // date de la dernière analyse
	Sounds  []LibSound `json:"sounds,omitempty"`
	Pads    PadCfg     `json:"pads"`
}

func libraryPath(cfgPath string) string {
	if cfgPath != "" {
		return filepath.Join(filepath.Dir(cfgPath), "bibliotheque.json")
	}
	return filepath.Join(filepath.Dir(configPath()), "bibliotheque.json")
}

func loadLibrary(path string) *Library {
	l := &Library{Version: libVersion, Pads: defaultPadCfg()}
	if b, err := os.ReadFile(path); err == nil {
		var x Library
		x.Pads = defaultPadCfg()
		if json.Unmarshal(b, &x) == nil && x.Version == libVersion {
			l = &x
		}
	}
	l.sanitize()
	return l
}

func (l *Library) sanitize() {
	out := l.Sounds[:0]
	for _, s := range l.Sounds {
		if s.File == "" || (s.Kind != "sf2" && s.Kind != "audio") {
			continue
		}
		if s.Cat < 0 || s.Cat >= nCat {
			s.Cat = catOther
		}
		out = append(out, s)
	}
	l.Sounds = out
	l.Pads.sanitize()
}

var libSaveMu sync.Mutex

// saveLibrary écrit le fichier d'un coup (fichier temporaire puis renommage : jamais de fichier à moitié écrit).
func saveLibrary(path string, l *Library) error {
	libSaveMu.Lock()
	defer libSaveMu.Unlock()
	b, err := json.MarshalIndent(l, "", " ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// famille d'un son d'après son icône (soundkind.go)
func catFromIcon(ic int) int {
	switch ic {
	case icKeys, icUpright:
		return catPiano
	case icOrgan:
		return catOrgan
	case icWave, icSquare:
		return catSynth
	case icCloud, icPad:
		return catPad
	case icStrings:
		return catStrings
	}
	return catOther
}

// famille d'un son : nom du son, puis nom du fichier, puis nom des dossiers (« Pianos/… », « Pads/… »),
// et pour un SoundFont le numéro de programme General MIDI.
func libCat(name, file string, isSF bool, bank, prog int, dirs []string) int {
	if isSF && bank == 128 {
		return catOther // batteries
	}
	if k := iconFromName(name); k >= 0 {
		return catFromIcon(k)
	}
	if isSF && bank == 0 && !genericName(name) {
		if k := iconFromGM(prog); k >= 0 {
			return catFromIcon(k)
		}
	}
	if k := iconFromName(baseNoExt(file)); k >= 0 {
		return catFromIcon(k)
	}
	for i := len(dirs) - 1; i >= 0; i-- { // du dossier le plus proche au plus lointain
		if k := iconFromName(dirs[i]); k >= 0 {
			return catFromIcon(k)
		}
	}
	if isSF && bank == 0 {
		if k := iconFromGM(prog); k >= 0 {
			return catFromIcon(k)
		}
	}
	if isSF {
		return catPiano // un SoundFont sans indice est le plus souvent un piano
	}
	return catOther
}

func isLibFile(name string) (sf, audio bool) {
	switch strings.ToLower(filepath.Ext(name)) {
	case ".sf2", ".sf3":
		return true, false
	case ".wav", ".wave", ".mp3":
		return false, true
	}
	return false, false
}

// scanLibrary parcourt dir et renvoie tous les sons trouvés, rangés (famille, puis nom).
// progress (facultatif) reçoit le nombre de fichiers déjà lus.
func scanLibrary(dir string, progress func(files int)) ([]LibSound, error) {
	st, err := os.Stat(dir)
	if err != nil {
		return nil, err
	}
	if !st.IsDir() {
		return nil, fs.ErrInvalid
	}
	root := filepath.Clean(dir)
	var out []LibSound
	files := 0
	err = filepath.WalkDir(root, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			if d != nil && d.IsDir() && p != root {
				return filepath.SkipDir // dossier illisible : on passe au suivant
			}
			return nil
		}
		rel, _ := filepath.Rel(root, p)
		var dirs []string
		if rel != "." {
			dirs = strings.Split(filepath.Dir(rel), string(filepath.Separator))
			if len(dirs) == 1 && dirs[0] == "." {
				dirs = nil
			}
		}
		if d.IsDir() {
			if p != root && (strings.HasPrefix(d.Name(), ".") || len(dirs) >= libMaxDepth) {
				return filepath.SkipDir
			}
			return nil
		}
		if strings.HasPrefix(d.Name(), ".") || strings.HasPrefix(d.Name(), "._") {
			return nil
		}
		sf, audio := isLibFile(d.Name())
		if !sf && !audio {
			return nil
		}
		if files >= libMaxFiles {
			return filepath.SkipAll
		}
		files++
		if progress != nil {
			progress(files)
		}
		if audio {
			name := baseNoExt(p)
			out = append(out, LibSound{Name: name, File: p, Kind: "audio", Cat: libCat(name, p, false, 0, 0, dirs)})
			return nil
		}
		s, err := OpenSF2(p)
		if err != nil {
			return nil // fichier abîmé : on l'ignore
		}
		for _, pr := range s.Presets() {
			out = append(out, LibSound{Name: pr.Name, File: p, Kind: "sf2", Bank: pr.Bank, Prog: pr.Num,
				Cat: libCat(pr.Name, p, true, pr.Bank, pr.Num, dirs)})
		}
		s.Close()
		return nil
	})
	sortLib(out)
	return out, err
}

func sortLib(s []LibSound) {
	sort.SliceStable(s, func(a, b int) bool {
		if s[a].Cat != s[b].Cat {
			return s[a].Cat < s[b].Cat
		}
		na, nb := foldName(s[a].Name), foldName(s[b].Name)
		if na != nb {
			return na < nb
		}
		return s[a].File < s[b].File
	})
}

// byCat : les sons de chaque famille
func (l *Library) byCat() [nCat][]LibSound {
	var out [nCat][]LibSound
	for _, s := range l.Sounds {
		out[s.Cat] = append(out[s.Cat], s)
	}
	return out
}

// ---------------------------------------------------------------- côté interface

// libState : la bibliothèque chargée par l'interface, et l'analyse en cours.
type libState struct {
	mu       sync.Mutex
	lib      *Library
	path     string
	scanning bool
	files    int // fichiers lus pendant l'analyse
	scroll   [nCat]int
}

func (u *UI) libLoad() {
	u.libs.mu.Lock()
	u.libs.path = libraryPath(u.cfgPath)
	u.libs.lib = loadLibrary(u.libs.path)
	u.libs.lib.Pads.On = false // au démarrage, les pads attendent qu'on les rallume (pas de son surprise)
	u.libs.mu.Unlock()
}

// library renvoie la bibliothèque (jamais nil)
func (u *UI) library() *Library {
	u.libs.mu.Lock()
	defer u.libs.mu.Unlock()
	if u.libs.lib == nil {
		u.libs.lib = &Library{Version: libVersion, Pads: defaultPadCfg()}
	}
	return u.libs.lib
}

func (u *UI) libSave() {
	u.libs.mu.Lock()
	l, p := u.libs.lib, u.libs.path
	u.libs.mu.Unlock()
	if l == nil || p == "" {
		return
	}
	if err := saveLibrary(p, l); err != nil {
		lklog("bibliothèque : enregistrement impossible : %v", err)
	}
}

func (u *UI) libScanning() (bool, int) {
	u.libs.mu.Lock()
	defer u.libs.mu.Unlock()
	return u.libs.scanning, u.libs.files
}

// libChooseDir : boîte « choisir un dossier », puis analyse.
func (u *UI) libChooseDir() {
	p, ok := u.host.OpenFile("folder")
	if !ok {
		return
	}
	u.libScan(p)
}

// libScan analyse dir en arrière-plan (l'interface reste utilisable), puis enregistre la liste.
func (u *UI) libScan(dir string) {
	u.libs.mu.Lock()
	if u.libs.scanning {
		u.libs.mu.Unlock()
		return
	}
	u.libs.scanning, u.libs.files = true, 0
	u.libs.mu.Unlock()
	u.Toast(T("Analyse du dossier de sons…"))
	u.host.Invalidate()
	go func() {
		defer recoverLib()
		t0 := time.Now()
		sounds, err := scanLibrary(dir, func(n int) {
			u.libs.mu.Lock()
			u.libs.files = n
			u.libs.mu.Unlock()
			if n%10 == 0 {
				u.host.Invalidate()
			}
		})
		u.libs.mu.Lock()
		u.libs.scanning = false
		if err == nil || len(sounds) > 0 {
			l := u.libs.lib
			if l == nil {
				l = &Library{Version: libVersion, Pads: defaultPadCfg()}
			}
			cp := *l
			cp.Dir, cp.Sounds, cp.Scanned = dir, sounds, time.Now().Format("2006-01-02 15:04")
			u.libs.lib = &cp
			u.libs.scroll = [nCat]int{}
		}
		u.libs.mu.Unlock()
		lklog("bibliothèque : %s → %d sons en %v (erreur : %v)", dir, len(sounds), time.Since(t0).Round(time.Millisecond), err)
		if err != nil && len(sounds) == 0 {
			u.Toast(Tf("Dossier illisible : %s", err.Error()))
		} else {
			u.libSave()
			u.Toast(Tf("%d sons trouvés et rangés.", len(sounds)))
		}
		u.host.Invalidate()
	}()
}

func recoverLib() {
	if r := recover(); r != nil {
		lklog("bibliothèque : erreur attrapée : %v", r)
	}
}
