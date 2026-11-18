// africanloops-analyse : outil en ligne de commande du moteur African Loops.
//
//	africanloops-analyse <dossier>                 analyse et classe toutes les loops
//	africanloops-analyse -separer <loop.wav> -sortie <dossier> [-tempo 120]
//	                                               sépare une loop en pistes WAV
package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"minipiano/africanloops"
)

func main() {
	sep := flag.String("separer", "", "loop à séparer en pistes (WAV ou MP3)")
	out := flag.String("sortie", "pistes", "dossier où écrire les pistes séparées")
	tempo := flag.Float64("tempo", 0, "écrire aussi la loop rejouée à ce tempo (BPM)")
	flag.Parse()

	if *sep != "" {
		if err := separate(*sep, *out, *tempo); err != nil {
			fmt.Fprintln(os.Stderr, "Erreur :", err)
			os.Exit(1)
		}
		return
	}
	if flag.NArg() != 1 {
		fmt.Fprintln(os.Stderr, "Utilisation : africanloops-analyse <dossier de loops>")
		fmt.Fprintln(os.Stderr, "          ou africanloops-analyse -separer <loop.wav> -sortie <dossier> [-tempo 120]")
		os.Exit(2)
	}
	lib, err := africanloops.Scan(flag.Arg(0), func(done, total int, cur string) {
		fmt.Fprintf(os.Stderr, "\rAnalyse %d / %d  %-60.60s", done, total, cur)
	})
	fmt.Fprintln(os.Stderr)
	if err != nil {
		fmt.Fprintln(os.Stderr, "Erreur :", err)
		os.Exit(1)
	}
	for _, c := range lib.Categories() {
		fmt.Printf("\n== %s (%d) ==\n", c.Name, c.Count)
		for _, e := range lib.Search(c.Name, "") {
			if e.Error != "" {
				fmt.Printf("  %-44.44s  ERREUR : %s\n", filepath.Base(e.Path), e.Error)
				continue
			}
			t := e.Info.Tempo
			bars := ""
			if t.Bars > 0 {
				bars = fmt.Sprintf("%d mes.", t.Bars)
			}
			var kinds []string
			for _, p := range e.Info.Percussions {
				kinds = append(kinds, p.Kind)
			}
			fmt.Printf("  %-44.44s %7.2f BPM (%-7s %3.0f%%) %-7s %s\n",
				filepath.Base(e.Path), t.BPM, t.Source+")", 100*t.Confidence, bars, strings.Join(kinds, ", "))
		}
	}
	fmt.Printf("\nRésultats gardés dans %s\n", filepath.Join(lib.Root, africanloops.IndexFile))
}

func separate(path, outDir string, tempo float64) error {
	a, err := africanloops.Load(path)
	if err != nil {
		return err
	}
	info, stems := africanloops.Separate(a, path)
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return err
	}
	base := strings.TrimSuffix(filepath.Base(path), filepath.Ext(path))
	fmt.Printf("%s : %.2f BPM (%s), %d temps, %.2f s\n", base, info.Tempo.BPM, info.Tempo.Source, info.Tempo.Beats, info.Seconds)
	for _, s := range stems {
		name := strings.NewReplacer("/", "-", " ", "_").Replace(s.Kind)
		p := filepath.Join(outDir, base+"_"+name+".wav")
		if err := os.WriteFile(p, africanloops.EncodeWAV(s.Audio, info.Tempo.BPM, info.Tempo.Beats), 0o644); err != nil {
			return err
		}
		fmt.Println("  piste :", p)
	}
	if tempo > 0 && info.Tempo.BPM > 0 {
		l := africanloops.NewLooper(a.Rate)
		l.Load(info, stems)
		l.SetGlobalBPM(tempo)
		l.Play()
		n := int(float64(a.Frames()) * info.Tempo.BPM / tempo * 2) // deux tours de boucle
		r := &africanloops.Audio{Rate: a.Rate, L: make([]float32, n), R: make([]float32, n)}
		l.Render(r.L, r.R)
		p := filepath.Join(outDir, fmt.Sprintf("%s_a_%gBPM.wav", base, tempo))
		if err := os.WriteFile(p, africanloops.EncodeWAV(r, tempo, 0), 0o644); err != nil {
			return err
		}
		fmt.Println("  au nouveau tempo :", p)
	}
	return nil
}
