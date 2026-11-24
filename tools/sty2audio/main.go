// sty2audio : convertit les styles Yamaha (.sty, .prs, .bcs, .sst, .fps…)
// en fichiers audio MP3 et WAV, section par section, avec une SoundFont SF2.
//
// Utilisation la plus simple (Windows) : glisser un ou plusieurs fichiers .sty
// (ou un dossier) sur sty2audio.exe. Les fichiers audio sont créés dans un
// dossier « <nom du style> (audio) » à côté de chaque style.
package main

import (
	"bufio"
	"flag"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"
)

var styleExt = map[string]bool{
	".sty": true, ".prs": true, ".bcs": true, ".sst": true, ".fps": true,
	".pcs": true, ".pst": true, ".pls": true, ".bls": true,
}

type job struct {
	name string
	secs []Section
}

func main() {
	fl := flag.NewFlagSet("sty2audio", flag.ExitOnError)
	sf2Path := fl.String("sf2", "", "SoundFont .sf2 à utiliser (sinon recherche automatique)")
	formats := fl.String("format", "mp3,wav", "formats de sortie : mp3, wav ou mp3,wav")
	outDir := fl.String("sortie", "", "dossier de sortie (par défaut : à côté du style)")
	repeats := fl.Int("repetitions", 2, "nombre de passages de chaque Main (1 = une fois)")
	fillRep := fl.Int("repetitions-fill", 1, "nombre de passages de chaque Fill In / Break")
	bpm := fl.Float64("tempo", 0, "tempo en BPM (0 = tempo d'origine du style)")
	key := fl.String("tonalite", "", "tonalité : Do, Do#, Re, Mib… ou C, C#, D, Eb… (vide = d'origine)")
	xpose := fl.Int("transposer", 0, "transposition en demi-tons (-12..12)")
	sections := fl.String("sections", "", "sections à convertir, séparées par des virgules (ex. \"Main A,Intro A\")")
	complet := fl.Bool("complet", true, "créer aussi un enchaînement complet Intro → Main → Fill → Ending")
	seules := fl.Bool("sections-separees", true, "créer un fichier par section")
	bits := fl.Int("bits", 16, "résolution WAV : 16 ou 24 bits")
	kbps := fl.Int("mp3-debit", 320, "débit MP3 en kbit/s (128, 192, 256, 320)")
	rate := fl.Int("frequence", 44100, "fréquence d'échantillonnage (44100 ou 48000)")
	reverb := fl.Float64("reverb", 1.0, "quantité de réverbération (0 = aucune, 1 = normale, 2 = forte)")
	norm := fl.Bool("normaliser", true, "ajuster le volume au maximum sans saturer (même gain pour tout le style)")
	pause := fl.Bool("pause", runtime.GOOS == "windows", "attendre Entrée avant de fermer la fenêtre")
	tailSec := fl.Float64("queue", 4, "durée maximale (s) de la résonance après la fin de la section")
	ouvrir := fl.Bool("ouvrir", true, "ouvrir le dossier des fichiers audio à la fin (Windows)")
	list := fl.Bool("infos", false, "afficher seulement le contenu des styles, sans convertir")
	fl.Usage = func() {
		fmt.Fprintln(os.Stderr, "sty2audio — conversion des styles Yamaha en MP3 et WAV")
		fmt.Fprintln(os.Stderr, "\nUtilisation : sty2audio [options] fichier.sty [autres fichiers ou dossiers…]")
		fmt.Fprintln(os.Stderr, "Sous Windows : glissez simplement vos fichiers .sty sur sty2audio.exe.\n\nOptions :")
		fl.PrintDefaults()
	}
	args := os.Args[1:]
	fl.Parse(args)
	code := run(fl.Args(), runOpts{
		sf2: *sf2Path, formats: *formats, outDir: *outDir, sections: *sections,
		complet: *complet, seules: *seules, ouvrir: *ouvrir, bits: *bits, kbps: *kbps, list: *list, norm: *norm,
		opt: Options{Rate: *rate, Repeats: *repeats, FillRepeats: *fillRep, BPM: *bpm,
			Transpose: *xpose, Reverb: *reverb, TailMax: *tailSec},
		key: *key,
	})
	if *pause {
		fmt.Print("\nAppuyez sur Entrée pour fermer…")
		bufio.NewReader(os.Stdin).ReadString('\n')
	}
	os.Exit(code)
}

var lastOutDir string

type runOpts struct {
	sf2, formats, outDir, sections, key string
	complet, seules, list, norm, ouvrir bool
	bits, kbps                          int
	opt                                 Options
}

func run(paths []string, ro runOpts) int {
	fmt.Println("=== sty2audio : styles Yamaha -> MP3 / WAV ===")
	if ro.key != "" {
		k, ok := parseKey(ro.key)
		if !ok {
			fmt.Printf("Tonalité inconnue : %q (exemples : Do, Re#, Mib, Sol, C, F#, Bb)\n", ro.key)
			return 2
		}
		ro.opt.Key, ro.opt.KeySet = k, true
	}
	if ro.opt.Rate != 44100 && ro.opt.Rate != 48000 && ro.opt.Rate != 32000 {
		fmt.Println("Fréquence non prise en charge (44100, 48000 ou 32000).")
		return 2
	}
	if ro.bits != 16 && ro.bits != 24 {
		ro.bits = 16
	}
	wantMP3 := strings.Contains(strings.ToLower(ro.formats), "mp3")
	wantWAV := strings.Contains(strings.ToLower(ro.formats), "wav")
	if !wantMP3 && !wantWAV {
		fmt.Println("Aucun format choisi : utilisez -format mp3, wav ou mp3,wav")
		return 2
	}
	switch ro.kbps {
	case 128, 160, 192, 224, 256, 320:
	default:
		ro.kbps = 320
	}

	recursive := true
	if len(paths) == 0 && runtime.GOOS == "windows" {
		// double-clic : fenêtre de choix des fichiers
		fmt.Println("Choisissez vos styles dans la fenêtre qui vient de s'ouvrir…")
		paths = pickStyles()
		if len(paths) == 0 {
			fmt.Println("Aucun fichier choisi.")
			return 1
		}
	}
	if len(paths) == 0 {
		// lancé sans fichier : on cherche à côté du programme
		dirs := []string{"."}
		if exe, err := os.Executable(); err == nil {
			dirs = append(dirs, filepath.Dir(exe))
		}
		paths = dirs
		recursive = false
	}
	files := collectStyles(paths, recursive)
	if len(files) == 0 {
		fmt.Println("\nAucun fichier de style trouvé.")
		fmt.Println("Glissez vos fichiers .sty sur sty2audio.exe, ou placez-les dans le même dossier que le programme.")
		return 1
	}

	var sf *SoundFont
	if !ro.list {
		p := ro.sf2
		if p == "" {
			p = findSoundFont()
		}
		if p == "" {
			fmt.Println("\nAucune SoundFont (.sf2) trouvée : il en faut une pour produire le son des instruments.")
			if askYes("Télécharger maintenant GeneralUser GS (gratuite, 32 Mo) ? [O/n] ") {
				var err error
				if p, err = downloadGeneralUser(); err != nil {
					fmt.Printf("Échec : %v\n", err)
				}
			}
		}
		if p == "" {
			fmt.Println("Placez un fichier .sf2 General MIDI (par exemple GeneralUser GS ou FluidR3_GM)")
			fmt.Println("dans le même dossier que sty2audio.exe, ou indiquez-le avec -sf2 \"chemin\\son.sf2\".")
			return 1
		}
		t0 := time.Now()
		fmt.Printf("Chargement de la SoundFont %s…\n", filepath.Base(p))
		var err error
		sf, err = LoadSoundFont(p)
		if err != nil {
			fmt.Printf("Impossible de lire la SoundFont : %v\n", err)
			return 1
		}
		fmt.Printf("SoundFont : %s, chargée en %.1f s\n", sf, time.Since(t0).Seconds())
	}

	ffmpeg := ""
	if wantMP3 && !ro.list {
		ffmpeg = findFFmpeg()
		if ffmpeg != "" {
			fmt.Println("Encodage MP3 : LAME (via ffmpeg)")
		} else {
			fmt.Println("Encodage MP3 : encodeur intégré")
		}
	}

	failed := 0
	for _, f := range files {
		if err := convertOne(f, sf, ro, wantMP3, wantWAV, ffmpeg); err != nil {
			fmt.Printf("  ERREUR : %v\n", err)
			failed++
		}
	}
	fmt.Printf("\nTerminé : %d style(s) traité(s)", len(files)-failed)
	if failed > 0 {
		fmt.Printf(", %d en erreur", failed)
	}
	fmt.Println(".")
	if ro.ouvrir && lastOutDir != "" {
		openFolder(lastOutDir)
	}
	if failed > 0 {
		return 1
	}
	return 0
}

func collectStyles(paths []string, recursive bool) []string {
	seen := map[string]bool{}
	var out []string
	add := func(p string) {
		abs, err := filepath.Abs(p)
		if err != nil {
			abs = p
		}
		if !seen[abs] {
			seen[abs] = true
			out = append(out, abs)
		}
	}
	for _, p := range paths {
		fi, err := os.Stat(p)
		if err != nil {
			fmt.Printf("Introuvable : %s\n", p)
			continue
		}
		if !fi.IsDir() {
			add(p)
			continue
		}
		filepath.Walk(p, func(q string, info os.FileInfo, err error) error {
			if err != nil {
				return nil
			}
			if info.IsDir() && q != p && (!recursive || strings.HasSuffix(info.Name(), "(audio)")) {
				return filepath.SkipDir
			}
			if !info.IsDir() && styleExt[strings.ToLower(filepath.Ext(q))] {
				add(q)
			}
			return nil
		})
	}
	sort.Strings(out)
	return out
}

// findSoundFont : recherche d'une SoundFont dans les endroits habituels.
func findSoundFont() string {
	var dirs []string
	if exe, err := os.Executable(); err == nil {
		d := filepath.Dir(exe)
		dirs = append(dirs, d, filepath.Join(d, "soundfonts"), filepath.Join(d, "SoundFonts"))
	}
	if wd, err := os.Getwd(); err == nil {
		dirs = append(dirs, wd, filepath.Join(wd, "soundfonts"))
	}
	if env := os.Getenv("STY2AUDIO_SF2"); env != "" {
		if _, err := os.Stat(env); err == nil {
			return env
		}
	}
	if home, err := os.UserHomeDir(); err == nil {
		dirs = append(dirs, filepath.Join(home, "Documents", "SoundFonts"), filepath.Join(home, "SoundFonts"),
			filepath.Join(home, "Documents", "Layers Keys"), filepath.Join(home, "Music", "SoundFonts"))
	}
	if d, err := os.UserConfigDir(); err == nil {
		dirs = append(dirs, filepath.Join(d, "sty2audio"))
	}
	dirs = append(dirs, `C:\SoundFonts`, `C:\soundfonts`, "/usr/share/sounds/sf2", "/usr/share/soundfonts",
		"/Library/Audio/Sounds/Banks")
	type cand struct {
		path  string
		score int64
	}
	var cs []cand
	for _, d := range dirs {
		m, _ := filepath.Glob(filepath.Join(d, "*.sf2"))
		m2, _ := filepath.Glob(filepath.Join(d, "*.SF2"))
		for _, p := range append(m, m2...) {
			fi, err := os.Stat(p)
			if err != nil {
				continue
			}
			sc := fi.Size()
			n := strings.ToLower(filepath.Base(p))
			for _, good := range []string{"generaluser", "fluidr3", "gm", "gs", "xg", "yamaha", "arachno", "timbres"} {
				if strings.Contains(n, good) {
					sc += 1 << 40
					break
				}
			}
			cs = append(cs, cand{p, sc})
		}
		if len(cs) > 0 && len(dirs) > 0 && d == dirs[0] {
			break // à côté du programme : prioritaire
		}
	}
	if len(cs) == 0 {
		return ""
	}
	sort.Slice(cs, func(i, j int) bool { return cs[i].score > cs[j].score })
	return cs[0].path
}

var badChars = strings.NewReplacer(`\`, "-", "/", "-", ":", "-", "*", "", "?", "", "\"", "'", "<", "", ">", "", "|", "-")

func safeName(s string) string {
	s = strings.TrimSpace(badChars.Replace(s))
	if s == "" {
		s = "style"
	}
	return s
}

func convertOne(path string, sf *SoundFont, ro runOpts, wantMP3, wantWAV bool, ffmpeg string) error {
	fmt.Printf("\n>> %s\n", filepath.Base(path))
	st, err := LoadStyle(path)
	if err != nil {
		return err
	}
	base := strings.TrimSuffix(filepath.Base(path), filepath.Ext(path))
	title := st.Name
	if title == "" {
		title = base
	}
	fmt.Printf("  Nom : %s — tempo %.0f BPM — mesure %d/%d — %d sections\n",
		title, st.BPM(), st.TimeSig[0], st.TimeSig[1], len(st.Sections))
	for _, s := range st.Sections {
		bars := float64(s.End-s.Start) / float64(st.PPQN) / (4 * float64(st.TimeSig[0]) / float64(st.TimeSig[1]))
		fmt.Printf("    - %-12s %.1f mesure(s)\n", s.Name, bars)
	}
	if ro.list {
		return nil
	}

	var jobs []job
	if ro.seules {
		want := map[string]bool{}
		for _, s := range strings.Split(ro.sections, ",") {
			if s = strings.TrimSpace(s); s != "" {
				want[strings.ToUpper(s)] = true
			}
		}
		for i, s := range st.Sections {
			if len(want) > 0 && !want[strings.ToUpper(s.Name)] {
				continue
			}
			jobs = append(jobs, job{fmt.Sprintf("%02d %s", i+1, s.Name), []Section{s}})
		}
		if len(want) > 0 && len(jobs) == 0 {
			return fmt.Errorf("aucune des sections demandées (%s) n'existe dans ce style", ro.sections)
		}
	}
	if ro.complet && len(st.Sections) > 1 && ro.sections == "" {
		jobs = append(jobs, job{"00 Complet", st.DemoChain()})
	}
	if len(jobs) == 0 {
		return fmt.Errorf("rien à convertir")
	}

	dir := ro.outDir
	if dir == "" {
		dir = filepath.Join(filepath.Dir(path), safeName(base)+" (audio)")
	} else {
		dir = filepath.Join(dir, safeName(base))
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}

	// rendu en parallèle (un synthétiseur par fichier)
	t0 := time.Now()
	res := make([]*Audio, len(jobs))
	miss := map[int]bool{}
	var mu sync.Mutex
	var wg sync.WaitGroup
	sem := make(chan struct{}, max(1, runtime.NumCPU()))
	for i := range jobs {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			a, m := RenderChain(st, sf, jobs[i].secs, ro.opt)
			mu.Lock()
			res[i] = a
			for k := range m {
				miss[k] = true
			}
			mu.Unlock()
		}(i)
	}
	wg.Wait()
	for k := range miss {
		fmt.Printf("  ! son absent de la SoundFont : canal %d, banque %d, programme %d\n", k>>16+1, k>>8&0xFF, k&0xFF+1)
	}

	// gain commun : toutes les sections gardent leurs volumes relatifs
	gain := 1.0
	if ro.norm {
		pk := 0.0
		for _, a := range res {
			pk = math.Max(pk, a.Peak())
		}
		if pk > 1e-6 {
			gain = 0.891 / pk // -1 dBFS
		}
	} else {
		gain = 0.5
	}

	for i, j := range jobs {
		a := res[i]
		name := safeName(base + " - " + j.name)
		secs := float64(a.Len()) / float64(ro.opt.Rate)
		var done []string
		if wantWAV {
			p := filepath.Join(dir, name+".wav")
			if err := WriteWAV(p, a, ro.opt.Rate, ro.bits, gain); err != nil {
				return err
			}
			done = append(done, "WAV")
		}
		if wantMP3 {
			p := filepath.Join(dir, name+".mp3")
			if err := WriteMP3(p, a, ro.opt.Rate, ro.kbps, gain, ffmpeg, title+" - "+j.name[3:], "Style Yamaha"); err != nil {
				return err
			}
			done = append(done, "MP3")
		}
		fmt.Printf("  OK %-28s %5.1f s  [%s]\n", j.name, secs, strings.Join(done, "+"))
	}
	fmt.Printf("  -> %s  (%.1f s)\n", dir, time.Since(t0).Seconds())
	if lastOutDir == "" {
		lastOutDir = dir
	}
	return nil
}

// parseKey : « Do », « Ré# », « Mib », « C », « F# », « Bb »… → 0..11
func parseKey(s string) (int, bool) {
	s = strings.ToLower(strings.TrimSpace(s))
	s = strings.NewReplacer("é", "e", "è", "e", "♯", "#", "♭", "b", " ", "", "majeur", "", "maj", "").Replace(s)
	names := []struct {
		n string
		v int
	}{{"do", 0}, {"re", 2}, {"mi", 4}, {"fa", 5}, {"sol", 7}, {"la", 9}, {"si", 11},
		{"c", 0}, {"d", 2}, {"e", 4}, {"f", 5}, {"g", 7}, {"a", 9}, {"b", 11}, {"h", 11}}
	for _, n := range names {
		if !strings.HasPrefix(s, n.n) {
			continue
		}
		rest := s[len(n.n):]
		v := n.v
		switch rest {
		case "":
		case "#", "diese":
			v++
		case "b", "bemol":
			v--
		default:
			continue
		}
		return (v + 12) % 12, true
	}
	return 0, false
}
