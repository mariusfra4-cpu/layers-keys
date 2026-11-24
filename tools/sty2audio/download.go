package main

// Téléchargement de la SoundFont gratuite GeneralUser GS (S. Christian Collins,
// licence libre d'utilisation et de redistribution) quand aucune n'est trouvée.

import (
	"bufio"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	guName   = "GeneralUser-GS.sf2"
	guURL    = "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/684543d5e5efaef08d02be50dcda8d552478fa60/GeneralUser-GS.sf2"
	guSHA256 = "9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe"
	guSize   = 32319396
)

// soundFontDir : dossier où ranger la SoundFont téléchargée.
func soundFontDir() string {
	if exe, err := os.Executable(); err == nil {
		d := filepath.Dir(exe)
		if f, err := os.CreateTemp(d, ".ecriture"); err == nil {
			f.Close()
			os.Remove(f.Name())
			return d
		}
	}
	if d, err := os.UserConfigDir(); err == nil {
		d = filepath.Join(d, "sty2audio")
		if os.MkdirAll(d, 0o755) == nil {
			return d
		}
	}
	return "."
}

func askYes(question string) bool {
	fmt.Print(question)
	line, err := bufio.NewReader(os.Stdin).ReadString('\n')
	if err != nil && line == "" {
		return true // pas de clavier (lancement automatique) : oui par défaut
	}
	a := strings.ToLower(strings.TrimSpace(line))
	return a == "" || a == "o" || a == "oui" || a == "y" || a == "yes"
}

type progress struct {
	n, total int64
	last     time.Time
}

func (p *progress) Write(b []byte) (int, error) {
	p.n += int64(len(b))
	if time.Since(p.last) > 300*time.Millisecond || p.n == p.total {
		p.last = time.Now()
		fmt.Printf("\r  %5.1f / %.1f Mo (%3.0f %%)", float64(p.n)/1e6, float64(p.total)/1e6, 100*float64(p.n)/float64(p.total))
	}
	return len(b), nil
}

// downloadGeneralUser : télécharge et vérifie la SoundFont, renvoie son chemin.
func downloadGeneralUser() (string, error) {
	dir := soundFontDir()
	dst := filepath.Join(dir, guName)
	tmp := dst + ".partiel"
	fmt.Println("Téléchargement de GeneralUser GS (SoundFont gratuite, 32 Mo)…")
	cl := &http.Client{Timeout: 15 * time.Minute}
	resp, err := cl.Get(guURL)
	if err != nil {
		return "", fmt.Errorf("pas de connexion Internet ? (%v)", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return "", fmt.Errorf("le serveur a répondu %s", resp.Status)
	}
	f, err := os.Create(tmp)
	if err != nil {
		return "", err
	}
	h := sha256.New()
	_, err = io.Copy(io.MultiWriter(f, h, &progress{total: guSize}), resp.Body)
	f.Close()
	fmt.Println()
	if err != nil {
		os.Remove(tmp)
		return "", fmt.Errorf("téléchargement interrompu : %v", err)
	}
	if hex.EncodeToString(h.Sum(nil)) != guSHA256 {
		os.Remove(tmp)
		return "", fmt.Errorf("fichier téléchargé endommagé, réessayez")
	}
	if err := os.Rename(tmp, dst); err != nil {
		return "", err
	}
	fmt.Printf("SoundFont enregistrée : %s\n", dst)
	return dst, nil
}
