package main

// Petit journal de diagnostic : ~/Library/Logs/LayersKeys.log sur Mac (dossier de configuration ailleurs).
// Il ne contient que des étapes techniques (démarrage, sortie audio, erreurs), aucune donnée personnelle.

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"time"
)

var logMu sync.Mutex

func logPath() string {
	if runtime.GOOS == "darwin" {
		if h, err := os.UserHomeDir(); err == nil {
			return filepath.Join(h, "Library", "Logs", "LayersKeys.log")
		}
	}
	d, err := os.UserConfigDir()
	if err != nil || d == "" {
		d = os.TempDir()
	}
	return filepath.Join(d, "LayersKeys", "journal.txt")
}

func lklog(format string, a ...interface{}) {
	defer func() { _ = recover() }()
	logMu.Lock()
	defer logMu.Unlock()
	p := logPath()
	_ = os.MkdirAll(filepath.Dir(p), 0o755)
	if st, err := os.Stat(p); err == nil && st.Size() > 300000 {
		_ = os.Remove(p)
	}
	f, err := os.OpenFile(p, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		return
	}
	defer f.Close()
	fmt.Fprintf(f, "%s  %s\n", time.Now().Format("2006-01-02 15:04:05"), fmt.Sprintf(format, a...))
}
