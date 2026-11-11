package main

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

type fakeHost struct {
	inval       int32
	open        string
	save        string
	ctrl, shift bool
	title       string
	quit        int32
	audio       []AudioCfg
	clip        string
}

func (h *fakeHost) Invalidate()                         { atomic.AddInt32(&h.inval, 1) }
func (h *fakeHost) OpenFile(kind string) (string, bool) { return h.open, h.open != "" }
func (h *fakeHost) SaveFile(n string) (string, bool)    { return h.save, h.save != "" }
func (h *fakeHost) Mods() (bool, bool)                  { return h.ctrl, h.shift }
func (h *fakeHost) SetTitle(t string)                   { h.title = t }
func (h *fakeHost) AudioAPIs() []string                 { return []string{apiASIO, apiWASAPI, apiMME} }
func (h *fakeHost) AudioDevices(api string) []string    { return []string{"Carte A", "Carte B"} }
func (h *fakeHost) AudioOpen(c AudioCfg) (string, error) {
	h.audio = append(h.audio, c)
	return "test audio", nil
}
func (h *fakeHost) AudioPanel()                                {}
func (h *fakeHost) Quit()                                      { atomic.AddInt32(&h.quit, 1) }
func (h *fakeHost) KeyLabel(sc uint32, fallback string) string { return fallback }

func (h *fakeHost) ClipGet() string  { return h.clip }
func (h *fakeHost) ClipSet(s string) { h.clip = s }

func newTestUI(t *testing.T, cfgPath string) (*UI, *fakeHost, *Engine) {
	e := NewEngine()
	e.revAmt = 0
	h := &fakeHost{}
	u := NewUI(e, h, cfgPath)
	return u, h, e
}

func drawUI(u *UI) *Canvas {
	c := NewCanvas(WinW, WinH)
	u.Draw(c)
	return c
}

func (u *UI) find(id string) *region {
	for i := len(u.regions) - 1; i >= 0; i-- {
		if u.regions[i].id == id {
			return &u.regions[i]
		}
	}
	return nil
}

func centre(r rect) (int, int) { return r.x + r.w/2, r.y + r.h/2 }

// clic complet sur l'élément d'identifiant id
func clickID(t *testing.T, u *UI, id string) {
	drawUI(u)
	r := u.find(id)
	if r == nil {
		t.Fatalf("élément %q introuvable à l'écran", id)
	}
	x, y := centre(r.r)
	u.MouseMove(x, y, false)
	u.MouseDown(x, y)
	u.MouseUp(x, y)
	drawUI(u)
}

func waitLoaded(t *testing.T, u *UI, ch int) {
	for i := 0; i < 400; i++ {
		if !u.isLoading(ch) {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("canal %d : chargement trop long", ch)
}

func TestUIDefaultsAndKeys(t *testing.T) {
	u, _, e := newTestUI(t, "")
	c := drawUI(u)
	if c.W != WinW || len(u.regions) < 50 {
		t.Fatalf("écran incomplet: %d zones", len(u.regions))
	}
	for i := 0; i < nCh; i++ {
		if srcOf(e, i) != nil {
			t.Fatalf("piste %d : aucun son ne doit être préchargé", i+1)
		}
	}
	u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand}) // un son de base pour les tests
	// touche Z (scan 0x2C) = note 48
	u.KeyDown(0x5A, 0x2C, false, false)
	if e.VoiceCount() != 1 || u.counts[48] != 1 {
		t.Fatalf("touche PC : %d voix", e.VoiceCount())
	}
	u.KeyDown(0x5A, 0x2C, true, false) // répétition automatique : ignorée
	if e.VoiceCount() != 1 {
		t.Fatal("la répétition de touche rejoue la note")
	}
	u.KeyUp(0x5A, 0x2C)
	if u.counts[48] != 0 {
		t.Fatal("touche non relâchée")
	}
	// espace = sustain, échap = tout arrêter
	u.KeyDown(vkSpace, 0, false, false)
	if !e.sustain {
		t.Fatal("espace n'active pas le sustain")
	}
	u.KeyUp(vkSpace, 0)
	if e.sustain {
		t.Fatal("sustain bloqué")
	}
	u.KeyDown(0x5A, 0x2C, false, false)
	u.KeyDown(vkEscape, 0, false, false)
	if e.VoiceCount() != 0 {
		t.Fatal("échap ne coupe pas les notes")
	}
	// clic souris sur une touche
	drawUI(u)
	x, y := u.whiteX[60]+5, kbY+blackH+12 // sous les touches noires
	u.MouseDown(x, y)
	if u.counts[60] != 1 || e.VoiceCount() == 0 {
		t.Fatal("clic sur le clavier sans effet")
	}
	u.MouseMove(u.whiteX[62]+5, y, true) // glissé vers la touche suivante
	if u.counts[60] != 0 || u.counts[62] != 1 {
		t.Fatalf("glissé : %v", u.counts)
	}
	u.MouseUp(0, 0)
	if u.counts[62] != 0 {
		t.Fatal("souris relâchée mais note active")
	}
	// MIDI
	u.MidiMsg(0x90 | 60<<8 | 100<<16)
	if u.counts[60] != 1 {
		t.Fatal("MIDI note on")
	}
	u.MidiMsg(0x80 | 60<<8)
	if u.counts[60] != 0 {
		t.Fatal("MIDI note off")
	}
	u.MidiMsg(0xB0 | 64<<8 | 127<<16)
	if !e.sustain {
		t.Fatal("MIDI sustain")
	}
}

func TestUIChannelControls(t *testing.T) {
	u, _, e := newTestUI(t, "")
	ch := e.ch[0]
	clickID(t, u, "oct0+")
	clickID(t, u, "oct0+")
	clickID(t, u, "tr0-")
	if ch.Oct != 2 || ch.Trans != -1 {
		t.Fatalf("octave/transpo : %d %d", ch.Oct, ch.Trans)
	}
	clickID(t, u, "mute0")
	clickID(t, u, "solo0")
	clickID(t, u, "hold0")
	if !ch.Mute || !ch.Solo || !ch.Hold {
		t.Fatalf("M/S/H : %v %v %v", ch.Mute, ch.Solo, ch.Hold)
	}
	// glisser le fader de volume à fond à gauche
	drawUI(u)
	r := u.find("vol0")
	u.MouseDown(r.r.x+6, r.r.y+8)
	u.MouseMove(r.r.x-100, r.r.y+8, true)
	u.MouseUp(0, 0)
	if ch.Vol > -59.9 {
		t.Fatalf("fader à gauche : %.1f dB", ch.Vol)
	}
	// bouton de pan : glisser vers le haut = droite
	drawUI(u)
	r = u.find("pan0")
	cx, cy := centre(r.r)
	u.MouseDown(cx, cy)
	u.MouseMove(cx, cy-70, true)
	u.MouseUp(0, 0)
	if ch.Pan < 0.3 {
		t.Fatalf("pan : %.2f", ch.Pan)
	}
	// double-clic : retour au centre
	u.MouseDown(cx, cy)
	u.MouseUp(cx, cy)
	u.MouseDown(cx, cy)
	u.MouseUp(cx, cy)
	if ch.Pan != 0 {
		t.Fatalf("double-clic : pan %.2f", ch.Pan)
	}
	// réglages du son sélectionné + molette
	u.sel = 0
	drawUI(u)
	r = u.find("p3")
	cx, cy = centre(r.r)
	u.mx, u.my = cx, cy
	before := ch.P[3]
	u.Wheel(1)
	if ch.P[3] <= before {
		t.Fatal("molette sans effet sur le bouton")
	}
	// volume général et réverbération
	drawUI(u)
	r = u.find("master")
	u.MouseDown(r.r.x+r.r.w/2, r.r.y+8)
	u.MouseUp(0, 0)
	if e.master < 0.4 || e.master > 0.6 {
		t.Fatalf("volume général : %.2f", e.master)
	}
	// choisir un autre canal
	drawUI(u)
	rr := u.find("row3")
	u.MouseDown(rr.r.x+120, rr.r.y+10)
	u.MouseUp(0, 0)
	if u.sel != 3 {
		t.Fatal("sélection de ligne")
	}
}

func TestUIPresetsAndPersistence(t *testing.T) {
	dir := t.TempDir()
	cp := filepath.Join(dir, "cfg", "config.json")
	u, _, e := newTestUI(t, cp)
	clickID(t, u, "preset2")
	u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
	u.loadSource(2, SrcCfg{Kind: "builtin", Builtin: iPad})
	e.ch[1].Mute = true
	e.ch[2].Vol = -6
	clickID(t, u, "savepreset")
	clickID(t, u, "preset0")
	if srcOf(e, 0) != nil || srcOf(e, 2) != nil {
		t.Fatal("un preset vide doit vider les pistes")
	}
	clickID(t, u, "preset2")
	if srcOf(e, 0) == nil || srcOf(e, 2) == nil || e.ch[0].Mute || !e.ch[1].Mute || e.ch[2].Vol != -6 {
		t.Fatal("preset 3 mal appliqué")
	}
	u.KeyDown(0x5A, 0x2C, false, false)
	if e.VoiceCount() != 2 {
		t.Fatalf("piano + pad : %d voix", e.VoiceCount())
	}
	u.KeyUp(0x5A, 0x2C)
	// modifier puis enregistrer dans le preset
	e.ch[2].Vol = -12
	e.ch[2].Pan = 0.5
	clickID(t, u, "savepreset")
	clickID(t, u, "preset0")
	clickID(t, u, "preset2")
	if e.ch[2].Vol != -12 || e.ch[2].Pan != 0.5 {
		t.Fatalf("preset enregistré non restitué : %.1f %.2f", e.ch[2].Vol, e.ch[2].Pan)
	}
	// renommer par double-clic
	drawUI(u)
	r := u.find("preset5")
	x, y := centre(r.r)
	u.MouseDown(x, y)
	u.MouseUp(x, y)
	u.MouseDown(x, y)
	u.MouseUp(x, y)
	if u.ov == nil || !u.ov.input {
		t.Fatal("renommage : fenêtre absente")
	}
	u.ov.text = ""
	for _, r := range "Mon son" {
		u.Char(r)
	}
	u.KeyDown(vkReturn, 0, false, false)
	if u.cfg.Presets[5].Name != "Mon son" || u.ov != nil {
		t.Fatalf("renommage : %q", u.cfg.Presets[5].Name)
	}
	clickID(t, u, "preset2")
	u.eng.SetMaster(0.33)
	u.Shutdown()

	// redémarrage : tout est retrouvé
	u2, _, e2 := newTestUI(t, cp)
	if u2.cfg.Presets[5].Name != "Mon son" || u2.cfg.Cur != 2 {
		t.Fatalf("relecture : %q cur=%d", u2.cfg.Presets[5].Name, u2.cfg.Cur)
	}
	if e2.ch[2].Vol != -12 || e2.ch[2].Pan != 0.5 || e2.ch[1].Mute != true || math.Abs(e2.master-0.33) > 1e-9 {
		t.Fatalf("réglages non restaurés : %.1f %.2f %v %.2f", e2.ch[2].Vol, e2.ch[2].Pan, e2.ch[1].Mute, e2.master)
	}
	// fichier de config corrompu / invalide : démarrage normal
	os.WriteFile(cp, []byte("{pas du json"), 0o644)
	u3, _, e3 := newTestUI(t, cp)
	if srcOf(e3, 0) != nil || u3.cfg.Presets[0].Name != "Preset 1" {
		t.Fatal("config corrompue : mauvais démarrage")
	}
	os.WriteFile(cp, []byte(`{"version":1,"setup":[{"vol":999,"pan":-50,"oct":99,"p":[5,5,5,5,5,5,5,5]}],"cur":99,"master":7}`), 0o644)
	_, _, e4 := newTestUI(t, cp)
	if e4.ch[0].Vol > 6 || e4.ch[0].Pan < -1 || e4.ch[0].Oct > 3 || e4.master > 1 {
		t.Fatal("valeurs hors limites non corrigées")
	}
}

func TestUIFilesSF2WAV(t *testing.T) {
	dir := t.TempDir()
	sfp := filepath.Join(dir, "test.sf2")
	os.WriteFile(sfp, testBank(), 0o644)
	wavp := filepath.Join(dir, "la.wav")
	os.WriteFile(wavp, makeWAV(440, 44100, 1, 16, false, 1.5, false, 69), 0o644)
	cp := filepath.Join(dir, "config.json")

	u, h, e := newTestUI(t, cp)
	// canal 1 : ouvrir le SoundFont, choisir « TestB »
	h.open = sfp
	u.openSoundMenu(0)
	if u.ov == nil || u.ov.custom == nil || !u.ov.wide {
		t.Fatal("fenêtre des sons absente")
	}
	drawUI(u)        // la fenêtre (vide, sans dossier de sons) se dessine sans planter
	u.libPickFile(0) // « Ouvrir un fichier… »
	if u.ov == nil || !u.ov.filterable || len(u.ov.items) != 8 {
		t.Fatalf("liste du SoundFont : %+v", u.ov)
	}
	drawUI(u)
	for _, r := range "testb" {
		u.Char(r)
	}
	if v := u.ov.visible(); len(v) != 1 {
		t.Fatalf("filtre : %d résultats", len(v))
	}
	u.KeyDown(vkReturn, 0, false, false)
	waitLoaded(t, u, 0)
	if nameOf(e, 0) != "TestB" || !strings.HasPrefix(catOf(e, 0), "SF2") {
		t.Fatalf("canal 1 : %q %q", nameOf(e, 0), catOf(e, 0))
	}
	for i := 1; i < nCh; i++ {
		e.ch[i].Mute = true
	}
	e.NoteOn(69, 100, 1)
	if cents := peakCents(run(e, 1.3), 880); math.Abs(cents) > 10 {
		t.Fatalf("son SF2 : %.1f cents", cents)
	}
	e.NoteOff(1)
	run(e, 0.5)

	// canal 2 : WAV
	h.open = wavp
	u.openSoundMenu(1)
	u.libPickFile(1)
	waitLoaded(t, u, 1)
	if nameOf(e, 1) != "la" || !strings.HasPrefix(catOf(e, 1), "AUDIO") {
		t.Fatalf("canal 2 : %q %q", nameOf(e, 1), catOf(e, 1))
	}
	e.ch[0].Mute, e.ch[1].Mute = true, false
	e.NoteOn(69, 100, 2)
	if cents := peakCents(run(e, 1.3), 440); math.Abs(cents) > 12 {
		t.Fatalf("son WAV : %.1f cents", cents)
	}
	if len(u.cfg.Recent) != 2 || u.cfg.Recent[0] != wavp {
		t.Fatalf("fichiers récents : %v", u.cfg.Recent)
	}
	u.Shutdown()

	// redémarrage : les sons chargés depuis les fichiers reviennent seuls
	_, _, e2 := newTestUI(t, cp)
	time.Sleep(300 * time.Millisecond)
	if nameOf(e2, 0) != "TestB" || nameOf(e2, 1) != "la" || srcOf(e2, 0) == nil || srcOf(e2, 1) == nil {
		t.Fatalf("restauration : %q %q", nameOf(e2, 0), nameOf(e2, 1))
	}
	// un fichier supprimé ne fait pas planter : message d'erreur, canal conservé
	os.Remove(sfp)
	os.Remove(wavp)
	u3, _, e3 := newTestUI(t, cp)
	time.Sleep(300 * time.Millisecond)
	u3.mu.Lock()
	toast3 := u3.toast
	u3.mu.Unlock()
	if srcOf(e3, 0) != nil || !strings.HasPrefix(catOf(e3, 0), "ERREUR") || toast3 == "" {
		t.Fatalf("fichier absent : %q %q toast=%q", nameOf(e3, 0), catOf(e3, 0), toast3)
	}
	e3.NoteOn(60, 100, 1) // ne doit pas planter
	run(e3, 0.1)
	drawUI(u3)
}

func TestUIBadFiles(t *testing.T) {
	dir := t.TempDir()
	u, _, _ := newTestUI(t, "")
	for name, wantSub := range map[string]string{"a.sf3": "SoundFont", "b.ogg": "Format", "c.sf2": "SoundFont", "d.wav": "WAV"} {
		p := filepath.Join(dir, name)
		os.WriteFile(p, []byte("rien de valide"), 0o644)
		u.mu.Lock()
		u.toast = ""
		u.mu.Unlock()
		u.openFileFor(2, p)
		waitLoaded(t, u, 2)
		time.Sleep(20 * time.Millisecond)
		u.mu.Lock()
		got := u.toast
		u.mu.Unlock()
		if got == "" || !strings.Contains(got, wantSub) {
			t.Errorf("%s : message %q (attendu « %s »)", name, got, wantSub)
		}
	}
	// charger un son plus ancien ne doit jamais écraser un choix plus récent
	u.loadSource(3, SrcCfg{Kind: "audio", File: filepath.Join(dir, "d.wav")})
	u.loadSource(3, SrcCfg{Kind: "builtin", Builtin: iPad})
	time.Sleep(100 * time.Millisecond)
	if nameOf(u.eng, 3) != "Synth Pad" {
		t.Fatalf("course entre deux chargements : %q", nameOf(u.eng, 3))
	}
}

func TestOverlayNavigation(t *testing.T) {
	u, _, _ := newTestUI(t, "")
	u.OpenSettings()
	o := u.ov
	start := o.sel
	if o.items[start].header {
		t.Fatal("la sélection démarre sur un titre")
	}
	for i := 0; i < 40; i++ {
		u.overlayKey(vkDown)
	}
	if o.items[o.sel].header || o.sel <= start {
		t.Fatalf("navigation : %d", o.sel)
	}
	u.overlayKey(vkHome)
	u.overlayKey(vkEnd)
	if o.sel != len(o.items)-1 {
		t.Fatalf("fin de liste : %d/%d", o.sel, len(o.items))
	}
	drawUI(u) // ne doit pas planter avec la fenêtre ouverte
	u.KeyDown(vkEscape, 0, false, false)
	if u.ov != nil {
		t.Fatal("échap ne ferme pas la fenêtre")
	}
	// les sons intégrés ne sont plus proposés dans la fenêtre des sons (seul le piano de départ reste)
	u.openSoundMenu(4)
	if u.ov == nil || u.ov.custom == nil {
		t.Fatal("fenêtre des sons absente")
	}
	u.KeyDown(vkEscape, 0, false, false)
}

func TestConfigMigrate(t *testing.T) {
	c := defaultConfig()
	b, _ := json.Marshal(c)
	var c2 Config
	if err := json.Unmarshal(b, &c2); err != nil || c2.Presets[2].Name != "Preset 3" {
		t.Fatalf("aller-retour JSON : %v", err)
	}
	if binary.Size(c.Master) != 8 {
		t.Fatal("?")
	}
	_ = fmt.Sprint(c2)
}

// ---- aperçu visuel (facultatif) : PREVIEW_DIR=/chemin go test -run Preview
// Python (Pillow) remplace ici le texte de Windows (GDI), dans le même ordre que sur Windows.
func previewHook(t *testing.T, dir string, c *Canvas) {
	c.FlushHook = func(texts []TextCmd) {
		raw := make([]byte, 0, len(c.Pix)*3)
		for _, p := range c.Pix {
			raw = append(raw, byte(p>>16), byte(p>>8), byte(p))
		}
		in, tj, out := filepath.Join(dir, "in.rgb"), filepath.Join(dir, "texts.json"), filepath.Join(dir, "out.rgb")
		os.WriteFile(in, raw, 0o644)
		j, _ := json.Marshal(texts)
		os.WriteFile(tj, j, 0o644)
		if b, err := exec.Command("python3", "/tmp/rendertext.py", in, tj, fmt.Sprint(c.W), fmt.Sprint(c.H), out).CombinedOutput(); err != nil {
			t.Fatalf("rendertext : %v %s", err, b)
		}
		res, _ := os.ReadFile(out)
		for i := range c.Pix {
			c.Pix[i] = uint32(res[3*i])<<16 | uint32(res[3*i+1])<<8 | uint32(res[3*i+2])
		}
	}
}

func savePreview(t *testing.T, dir, name string, u *UI) {
	c := NewCanvas(WinW, WinH)
	previewHook(t, dir, c)
	u.Draw(c)
	raw := make([]byte, 0, len(c.Pix)*3)
	for _, p := range c.Pix {
		raw = append(raw, byte(p>>16), byte(p>>8), byte(p))
	}
	rgb := filepath.Join(dir, name+".rgb")
	os.WriteFile(rgb, raw, 0o644)
	if b, err := exec.Command("python3", "/tmp/rgb2png.py", rgb, fmt.Sprint(c.W), fmt.Sprint(c.H), filepath.Join(dir, name+".png")).CombinedOutput(); err != nil {
		t.Fatalf("png : %v %s", err, b)
	}
}

func TestPreview(t *testing.T) {
	dir := os.Getenv("PREVIEW_DIR")
	if dir == "" {
		t.Skip("PREVIEW_DIR non défini")
	}
	u, _, e := newTestUI(t, "")
	e.ch[0].Vol = -1.6
	e.ch[0].peak = 0.12
	e.ch[2].Pan = 0.4
	e.ch[2].Vol = -6
	e.ch[2].peak = 0.04
	e.ch[1].Mute = true
	e.ch[3].Solo = true
	e.ch[4].Hold = true
	u.eng.sustain = true
	u.counts[60], u.counts[64], u.counts[67] = 1, 1, 1
	u.sel = 0
	u.midiOK, u.midiStatus = true, "MIDI connecté : Digital Piano"
	u.loadSource(0, SrcCfg{Kind: "builtin", Builtin: iGrand})
	u.loadSource(2, SrcCfg{Kind: "builtin", Builtin: iPad})
	u.labels[2] = "Nappe Gospel"
	u.counts[71] = 1
	savePreview(t, dir, "main", u)
	u.OpenSettings()
	savePreview(t, dir, "settings", u)
	u.lastVel = [2]int32{50, 61}
	u.OpenVelocity()
	savePreview(t, dir, "velocity", u)
	u.OpenSettings()
	u.openAudioMenu()
	savePreview(t, dir, "audio", u)
	u.openSoundMenu(0)
	savePreview(t, dir, "menu", u)
	sf, _ := OpenSF2(writeTmp(t, testBank()))
	u.openPresetList(0, sf, "x.sf2")
	for _, r := range "te" {
		u.Char(r)
	}
	savePreview(t, dir, "sf2", u)
	u.ov = nil
	u.toast, u.toastEnd = "Exemple de message d'erreur : fichier illisible", time.Now().Add(time.Hour)
	savePreview(t, dir, "toast", u)
}

func nameOf(e *Engine, i int) string { _, n, _ := e.ChInfo(i); return n }
func catOf(e *Engine, i int) string  { _, _, c := e.ChInfo(i); return c }
func srcOf(e *Engine, i int) Source  { s, _, _ := e.ChInfo(i); return s }
