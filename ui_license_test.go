package main

import (
	"crypto/ed25519"
	"encoding/hex"
	"os"
	"strings"
	"testing"
	"time"
)

// Les tests existants tournent avec un logiciel déjà autorisé.
func init() {
	licenseFactory = func() *License {
		return &License{Now: time.Now, MachineID: func() string { return "test" }, override: &LicStatus{State: LicLicensed}}
	}
}

// interface avec une vraie licence (dossiers temporaires, horloge simulée)
func licUI(t *testing.T) (*UI, *fakeHost, *Engine, *time.Time, ed25519.PrivateKey) {
	priv := testPriv(t)
	u, h, e := newTestUI(t, "")
	l, now, _ := testLicense(t, "pc-ui")
	u.lic, u.now = l, func() time.Time { return *now }
	u.refreshLicense()
	u.licShow = u.licSt.State != LicLicensed
	return u, h, e, now, priv
}

func TestLicenseScreenBlocksUntilActivated(t *testing.T) {
	u, h, e, _, priv := licUI(t)
	c := drawUI(u)
	txt := ""
	for _, tc := range c.AllTexts {
		txt += tc.S + "|"
	}
	for _, want := range []string{"Autoriser le logiciel", u.lic.Code(), "Mode démo", "Activer", "Layers Keys"} {
		if !strings.Contains(txt, want) {
			t.Errorf("l'écran de licence doit afficher %q", want)
		}
	}
	if u.find("liccopy") == nil || u.find("licpaste") == nil || u.find("licgo") == nil || u.find("licdemo") == nil {
		t.Fatal("boutons de l'écran de licence manquants")
	}
	if u.find("settings") != nil {
		t.Fatal("l'interface normale ne doit pas être accessible")
	}
	e.NoteOn(60, 100, 1)
	if len(e.voices) != 0 {
		t.Fatal("aucun son tant que le logiciel n'est pas autorisé")
	}
	// copier le code
	x, y := centre(u.find("liccopy").r)
	u.MouseDown(x, y)
	u.MouseUp(x, y)
	if h.clip != u.lic.Code() {
		t.Fatalf("le code doit être copié : %q", h.clip)
	}
	// mauvaise clé
	h.clip = "AAAAAAAA-BBBBBBBB"
	h.ctrl = true
	u.KeyDown('V', 0, false, false)
	h.ctrl = false
	u.KeyDown(vkReturn, 0, false, false)
	if u.licMsg == "" || u.licMsgOK || !u.licShow {
		t.Fatal("une mauvaise clé doit afficher une erreur et rester sur l'écran")
	}
	// bonne clé collée
	key, _ := SignCode(priv, u.lic.Code())
	h.clip = key
	drawUI(u)
	x, y = centre(u.find("licpaste").r)
	u.MouseDown(x, y)
	u.MouseUp(x, y)
	drawUI(u)
	x, y = centre(u.find("licgo").r)
	u.MouseDown(x, y)
	u.MouseUp(x, y)
	if u.licShow || !u.licenseOK() {
		t.Fatalf("la bonne clé doit autoriser le logiciel (%q)", u.licMsg)
	}
	drawUI(u)
	if u.find("settings") == nil {
		t.Fatal("l'interface normale doit apparaître")
	}
	e.NoteOn(60, 100, 1)
	if len(e.voices) == 0 && e.soundAllowed() == false {
		t.Fatal("le son doit être permis")
	}
	if !e.soundAllowed() {
		t.Fatal("son bloqué à tort")
	}
}

func TestLicenseDemoFlow(t *testing.T) {
	u, _, e, now, _ := licUI(t)
	drawUI(u)
	x, y := centre(u.find("licdemo").r)
	u.MouseDown(x, y)
	u.MouseUp(x, y)
	if u.licShow || u.licSt.State != LicDemo {
		t.Fatal("le bouton démo doit lancer la démo et ouvrir le logiciel")
	}
	if !e.soundAllowed() {
		t.Fatal("le son est permis pendant la démo")
	}
	drawUI(u)
	if u.find("licpill") == nil {
		t.Fatal("la pastille de démo doit être visible")
	}
	// 3 jours plus tard
	*now = now.Add(73 * time.Hour)
	u.licNext = time.Time{}
	if !u.tickLicense() {
		t.Fatal("la fin de démo doit redessiner l'écran")
	}
	if !u.licShow || u.licSt.State != LicLocked || e.soundAllowed() {
		t.Fatal("après 3 jours : écran de licence et plus de son")
	}
	drawUI(u)
	if u.find("licdemo") != nil {
		t.Fatal("pas de deuxième démo : le bouton ne doit plus être actif")
	}
}

func TestLicenseGateDeadline(t *testing.T) {
	e := NewEngine()
	e.SetLicenseGate(false, time.Now().Add(-time.Second))
	if e.soundAllowed() {
		t.Fatal("une démo échue doit couper le son même sans l'interface")
	}
	e.SetLicenseGate(false, time.Now().Add(time.Hour))
	if !e.soundAllowed() {
		t.Fatal("démo en cours")
	}
}

var _ = hex.EncodeToString
var _ = os.Stat
