package main

// Sauvegarde des réglages (sons choisis, presets...) dans le dossier de l'utilisateur.

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strings"
)

type SrcCfg struct {
	Kind    string `json:"kind,omitempty"` // "", "builtin", "sf2", "audio"
	Builtin int    `json:"builtin,omitempty"`
	File    string `json:"file,omitempty"`
	Bank    int    `json:"bank,omitempty"`
	Prog    int    `json:"prog,omitempty"`
	Name    string `json:"name,omitempty"`
}

type ChanCfg struct {
	Label  string          `json:"label,omitempty"` // nom de la piste choisi par l'utilisateur
	Src    SrcCfg          `json:"src"`
	Vol    float64         `json:"vol"`
	Pan    float64         `json:"pan"`
	Oct    int             `json:"oct"`
	Trans  int             `json:"trans"`
	Mute   bool            `json:"mute"`
	Solo   bool            `json:"solo"`
	Hold   bool            `json:"hold"`
	Pedal  bool            `json:"pedal"`
	Filter bool            `json:"filter"`
	ModFX  bool            `json:"modfx,omitempty"`
	ATOff  bool            `json:"atoff,omitempty"` // aftertouch désactivé pour cette piste
	P      [nParam]float64 `json:"p"`
}

type PresetCfg struct {
	Name  string       `json:"name"`
	Chans [nCh]ChanCfg `json:"chans"`
}

const nPresets = 8

type Config struct {
	Version int                 `json:"version"`
	Setup   [nCh]ChanCfg        `json:"setup"`
	Presets [nPresets]PresetCfg `json:"presets"`
	Cur     int                 `json:"cur"`
	Master  float64             `json:"master"`
	Reverb  float64             `json:"reverb"`
	// Leslie : vitesse rapide (commune à toutes les pistes dont le Leslie est allumé)
	LeslieFast bool     `json:"leslieFast,omitempty"`
	FX         *FXCfg   `json:"fx,omitempty"`  // ancien format (un seul jeu pour tout) : converti au chargement
	FXT        []*FXCfg `json:"fxt,omitempty"` // égaliseur et effets de chaque piste
	Recent     []string `json:"recent"`

	Format     string   `json:"format,omitempty"`     // présent seulement dans un fichier projet
	Project    string   `json:"project,omitempty"`    // projet en cours (dernière session)
	Changed    bool     `json:"changed,omitempty"`    // modifications non enregistrées
	RecentProj []string `json:"recentProj,omitempty"` // projets récents

	// réglages du logiciel (pas du projet)
	Audio        AudioCfg `json:"audio"`
	Lang         string   `json:"lang,omitempty"`         // "fr", "en", "es"
	Notation     string   `json:"notation,omitempty"`     // "abc" ou "solfege"
	AudioPending bool     `json:"audioPending,omitempty"` // vrai pendant l'ouverture d'un pilote audio (détection de plantage)

	// jeu au clavier MIDI (réglages du logiciel) ; 0 = valeur par défaut
	ATMode   int        `json:"atmode,omitempty"`   // aftertouch : 0 vibrato + brillance, 1 vibrato, 2 brillance, 3 désactivé
	ATAmt    float64    `json:"atamt,omitempty"`    // intensité de l'aftertouch (1 = normal)
	VelGamma float64    `json:"velgamma,omitempty"` // courbe de vélocité : <1 plus douce, >1 plus dure
	VelLo    int        `json:"vello,omitempty"`    // vélocité minimale envoyée aux sons
	VelHi    int        `json:"velhi,omitempty"`    // vélocité maximale
	Learn    []LearnCfg `json:"learn,omitempty"`    // MIDI Learn : associations CC → réglages
}

const (
	cfgVersion    = 2
	projectFormat = "LayersKeys-Project"
	projectExt    = ".lkeys"
)

func newChanCfg() ChanCfg {
	return ChanCfg{Pedal: true, Filter: true, P: paramDefault}
}

func defaultSetup() [nCh]ChanCfg {
	var s [nCh]ChanCfg
	for i := range s {
		s[i] = newChanCfg()
	}
	return s
}

func defaultConfig() *Config {
	c := &Config{Version: cfgVersion, Setup: defaultSetup(), Master: 0.8, Reverb: 0.15, Audio: AudioCfg{Rate: 48000}, Lang: "fr"}
	for i := range c.Presets {
		c.Presets[i] = PresetCfg{Name: fmt.Sprintf("Preset %d", i+1), Chans: defaultSetup()}
	}
	return c
}

func clampf(v, lo, hi float64) float64 {
	if math.IsNaN(v) {
		return lo
	}
	return math.Max(lo, math.Min(hi, v))
}

func cleanName(n string, max int) string {
	r := []rune(strings.TrimSpace(n))
	if len(r) > max {
		r = r[:max]
	}
	return string(r)
}

func (c *ChanCfg) sanitize() {
	c.Label = cleanName(c.Label, 28)
	c.Vol = clampf(c.Vol, -60, 6)
	c.Pan = clampf(c.Pan, -1, 1)
	c.Oct = clamp(c.Oct, -3, 3)
	c.Trans = clamp(c.Trans, -12, 12)
	for i := range c.P {
		c.P[i] = clampf(c.P[i], 0, 1)
	}
	if c.Src.Builtin < 0 || c.Src.Builtin >= nInst {
		c.Src.Builtin = 0
	}
}

func (c *Config) sanitize() {
	for i := range c.Setup {
		c.Setup[i].sanitize()
	}
	for i := range c.Presets {
		for j := range c.Presets[i].Chans {
			c.Presets[i].Chans[j].sanitize()
		}
	}
	c.Audio.sanitize()
	if c.Lang != "en" && c.Lang != "es" {
		c.Lang = "fr"
	}
	if c.Notation != "abc" && c.Notation != "solfege" {
		c.Notation = ""
	}
	c.ATMode = clamp(c.ATMode, 0, 3)
	if c.ATAmt == 0 {
		c.ATAmt = 1
	}
	c.ATAmt = clampf(c.ATAmt, 0.1, 2)
	if c.VelGamma == 0 {
		c.VelGamma = 1
	}
	c.VelGamma = clampf(c.VelGamma, 0.3, 3)
	if c.VelHi == 0 {
		c.VelHi = 127
	}
	c.VelHi = clamp(c.VelHi, 1, 127)
	if c.VelLo == 0 {
		c.VelLo = 1
	}
	c.VelLo = clamp(c.VelLo, 1, c.VelHi)
	c.Cur = clamp(c.Cur, 0, nPresets-1)
	c.Master = clampf(c.Master, 0, 1)
	c.Reverb = clampf(c.Reverb, 0, 1)
	if len(c.FXT) == 0 && c.FX != nil { // ancien fichier : les mêmes effets sur toutes les pistes
		for i := 0; i < nCh; i++ {
			cp := *c.FX
			c.FXT = append(c.FXT, &cp)
		}
	}
	c.FX = nil
	if len(c.FXT) > nCh {
		c.FXT = c.FXT[:nCh]
	}
	for _, f := range c.FXT {
		if f != nil {
			f.sanitize()
		}
	}
	if len(c.Recent) > 8 {
		c.Recent = c.Recent[:8]
	}
	if len(c.RecentProj) > 8 {
		c.RecentProj = c.RecentProj[:8]
	}
	for i := range c.Presets {
		c.Presets[i].Name = cleanName(c.Presets[i].Name, 28)
		if c.Presets[i].Name == "" {
			c.Presets[i].Name = fmt.Sprintf("Preset %d", i+1)
		}
	}
}

func configPath() string {
	dir, err := os.UserConfigDir()
	if err != nil || dir == "" {
		dir = os.TempDir()
	}
	return filepath.Join(dir, "LayersKeys", "config.json")
}

func loadConfig(path string) *Config {
	b, err := os.ReadFile(path)
	if err != nil {
		return defaultConfig()
	}
	c := defaultConfig()
	if err := json.Unmarshal(b, c); err != nil || c.Version != cfgVersion {
		return defaultConfig()
	}
	c.sanitize()
	return c
}

func saveConfig(path string, c *Config) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	b, err := json.MarshalIndent(c, "", " ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// ---------------------------------------------------------------- fichiers projet (.mpiano)

func saveProject(path string, c *Config) error {
	p := *c
	p.Format, p.Version = projectFormat, cfgVersion
	p.Recent, p.RecentProj, p.Project, p.Changed = nil, nil, "", false
	p.Audio, p.Lang, p.Notation, p.AudioPending = AudioCfg{}, "", "", false
	p.ATMode, p.ATAmt, p.VelGamma, p.VelLo, p.VelHi = 0, 0, 0, 0, 0
	p.Learn = nil
	return saveConfig(path, &p)
}

func loadProject(path string) (*Config, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	c := defaultConfig()
	if err := json.Unmarshal(b, c); err != nil {
		return nil, errors.New("ce fichier n'est pas un projet Layers Keys valide")
	}
	if c.Format != projectFormat && c.Format != "MiniPiano-Project" {
		return nil, errors.New("ce fichier n'est pas un projet Layers Keys")
	}
	if c.Version > cfgVersion {
		return nil, errors.New("ce projet a été créé avec une version plus récente de Layers Keys")
	}
	c.Version = cfgVersion
	c.sanitize()
	return c, nil
}
