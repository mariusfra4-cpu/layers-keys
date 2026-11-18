package africanloops

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"
	"unicode"
)

var accents = strings.NewReplacer(
	"à", "a", "â", "a", "ä", "a", "á", "a", "ã", "a",
	"ç", "c", "é", "e", "è", "e", "ê", "e", "ë", "e",
	"î", "i", "ï", "i", "í", "i", "ì", "i",
	"ô", "o", "ö", "o", "ó", "o", "ò", "o", "õ", "o",
	"ù", "u", "û", "u", "ü", "u", "ú", "u", "ñ", "n", "ÿ", "y", "œ", "oe", "æ", "ae",
)

// Entry est une loop de la bibliothèque.
type Entry struct {
	Path     string    `json:"path"`     // chemin relatif au dossier de la bibliothèque
	Category string    `json:"category"` // style : Worship, Love, Afrobeats…
	Size     int64     `json:"size"`
	ModTime  time.Time `json:"mtime"`
	Info     LoopInfo  `json:"info"`
	Error    string    `json:"error,omitempty"`
}

// Library est un dossier de loops analysé.
type Library struct {
	Root    string   `json:"root"`
	Entries []*Entry `json:"entries"`
}

// Uncategorized : loops dont le style n'a pas pu être trouvé.
const Uncategorized = "Non classés"

// styles reconnus dans les noms de fichiers (sans accents, en minuscules)
var styleWords = []struct {
	words []string
	name  string
}{
	{[]string{"worship", "louange", "adoration", "praise"}, "Worship"},
	{[]string{"gospel"}, "Gospel"},
	{[]string{"love", "amour", "romance"}, "Love"},
	{[]string{"amapiano", "piano log", "logdrum", "log drum"}, "Amapiano"},
	{[]string{"afrobeat", "afrobeats", "afropop", "afro pop", "naija"}, "Afrobeats"},
	{[]string{"afrohouse", "afro house"}, "Afro House"},
	{[]string{"coupe decale", "coupe-decale", "coupedecale", "decale"}, "Coupé-Décalé"},
	{[]string{"ndombolo", "soukous", "sebene"}, "Ndombolo"},
	{[]string{"rumba"}, "Rumba"},
	{[]string{"makossa"}, "Makossa"},
	{[]string{"bikutsi"}, "Bikutsi"},
	{[]string{"highlife"}, "Highlife"},
	{[]string{"mbalax"}, "Mbalax"},
	{[]string{"zouk"}, "Zouk"},
	{[]string{"kizomba"}, "Kizomba"},
	{[]string{"gqom"}, "Gqom"},
	{[]string{"azonto"}, "Azonto"},
	{[]string{"bongo flava", "bongo"}, "Bongo Flava"},
}

// plainLower enlève les accents et met en minuscules (« Coupé-Décalé » → « coupe-decale »).
func plainLower(s string) string {
	s = accents.Replace(strings.ToLower(s))
	return strings.Join(strings.FieldsFunc(s, func(r rune) bool { return r == '_' || r == '.' || unicode.IsSpace(r) }), " ")
}

// CategoryOf trouve le style d'une loop. Priorité au nom du dossier où elle est
// rangée (« Worship/… » → Worship), sinon les mots du nom du fichier.
func CategoryOf(rel string) string {
	rel = filepath.ToSlash(rel)
	parts := strings.Split(rel, "/")
	if len(parts) > 1 {
		// le dossier où tu as rangé la loop décide : « I Love/… » → I Love
		return strings.TrimSpace(parts[0])
	}
	if c := styleFromText(strings.TrimSuffix(parts[0], filepath.Ext(parts[0]))); c != "" {
		return c
	}
	return Uncategorized
}

func styleFromText(s string) string {
	p := " " + plainLower(s) + " "
	for _, st := range styleWords {
		for _, w := range st.words {
			if strings.Contains(p, w) {
				return st.name
			}
		}
	}
	return ""
}

// IndexFile : fichier où les résultats sont gardés, dans le dossier des loops.
const IndexFile = ".africanloops-index.json"

// Progress est appelé pendant l'analyse : fait / total.
type Progress func(done, total int, current string)

// Scan analyse toutes les loops d'un dossier (et de ses sous-dossiers).
// Les loops déjà analysées et non modifiées ne sont pas refaites.
func Scan(root string, progress Progress) (*Library, error) {
	root, err := filepath.Abs(root)
	if err != nil {
		return nil, err
	}
	old := map[string]*Entry{}
	if prev, err := LoadIndex(root); err == nil {
		for _, e := range prev.Entries {
			old[e.Path] = e
		}
	}
	var todo []*Entry
	lib := &Library{Root: root}
	err = filepath.WalkDir(root, func(p string, d os.DirEntry, err error) error {
		if err != nil {
			return nil // dossier illisible : on continue
		}
		if d.IsDir() {
			if p != root && strings.HasPrefix(d.Name(), ".") {
				return filepath.SkipDir
			}
			return nil
		}
		if !IsAudioFile(p) {
			return nil
		}
		fi, err := d.Info()
		if err != nil {
			return nil
		}
		rel, _ := filepath.Rel(root, p)
		rel = filepath.ToSlash(rel)
		e := &Entry{Path: rel, Category: CategoryOf(rel), Size: fi.Size(), ModTime: fi.ModTime().UTC().Truncate(time.Second)}
		if o := old[rel]; o != nil && o.Size == e.Size && o.ModTime.Equal(e.ModTime) && o.Error == "" {
			e.Info = o.Info
			if o.Category != "" {
				e.Category = o.Category // garde les corrections faites à la main
			}
		} else {
			todo = append(todo, e)
		}
		lib.Entries = append(lib.Entries, e)
		return nil
	})
	if err != nil {
		return nil, err
	}
	workers := runtime.NumCPU() / 2
	if workers < 1 {
		workers = 1
	}
	jobs := make(chan *Entry)
	var wg sync.WaitGroup
	var mu sync.Mutex
	done := 0
	for w := 0; w < workers; w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for e := range jobs {
				a, err := Load(filepath.Join(root, filepath.FromSlash(e.Path)))
				if err != nil {
					e.Error = err.Error()
				} else {
					e.Info = Analyze(a, e.Path)
					e.Info.Hits = nil // pas besoin de garder chaque coup dans l'index
				}
				mu.Lock()
				done++
				if progress != nil {
					progress(done, len(todo), e.Path)
				}
				mu.Unlock()
			}
		}()
	}
	for _, e := range todo {
		jobs <- e
	}
	close(jobs)
	wg.Wait()
	sort.Slice(lib.Entries, func(i, j int) bool {
		a, b := lib.Entries[i], lib.Entries[j]
		if a.Category != b.Category {
			return a.Category < b.Category
		}
		return strings.ToLower(a.Path) < strings.ToLower(b.Path)
	})
	return lib, lib.Save()
}

// Save écrit les résultats à côté des loops.
func (l *Library) Save() error {
	b, err := json.MarshalIndent(l, "", " ")
	if err != nil {
		return err
	}
	tmp := filepath.Join(l.Root, IndexFile+".tmp")
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, filepath.Join(l.Root, IndexFile))
}

// LoadIndex relit les résultats d'une analyse précédente.
func LoadIndex(root string) (*Library, error) {
	b, err := os.ReadFile(filepath.Join(root, IndexFile))
	if err != nil {
		return nil, err
	}
	var l Library
	if err := json.Unmarshal(b, &l); err != nil {
		return nil, err
	}
	l.Root = root
	return &l, nil
}

// Categories renvoie les styles et le nombre de loops de chacun.
func (l *Library) Categories() []CategoryCount {
	count := map[string]int{}
	for _, e := range l.Entries {
		count[e.Category]++
	}
	var out []CategoryCount
	for c, n := range count {
		out = append(out, CategoryCount{c, n})
	}
	sort.Slice(out, func(i, j int) bool {
		if (out[i].Name == Uncategorized) != (out[j].Name == Uncategorized) {
			return out[j].Name == Uncategorized // « Non classés » à la fin
		}
		return out[i].Name < out[j].Name
	})
	return out
}

// CategoryCount : un style et son nombre de loops.
type CategoryCount struct {
	Name  string
	Count int
}

// SetCategory range une loop dans un autre style (correction à la main).
func (l *Library) SetCategory(path, category string) bool {
	for _, e := range l.Entries {
		if e.Path == path {
			e.Category = category
			return true
		}
	}
	return false
}

// Search renvoie les loops d'un style (vide = tous) dont le nom contient le texte.
func (l *Library) Search(category, text string) []*Entry {
	q := plainLower(text)
	var out []*Entry
	for _, e := range l.Entries {
		if category != "" && e.Category != category {
			continue
		}
		if q != "" && !strings.Contains(plainLower(e.Path), q) {
			continue
		}
		out = append(out, e)
	}
	return out
}
