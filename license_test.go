package main

import (
	"crypto/ed25519"
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func testPriv(t *testing.T) ed25519.PrivateKey {
	b, err := os.ReadFile("keys/private.seed")
	if err != nil {
		t.Skip("clé secrète absente (normal dans le dépôt)")
	}
	seed, _ := hex.DecodeString(strings.TrimSpace(string(b)))
	return ed25519.NewKeyFromSeed(seed)
}

func testLicense(t *testing.T, id string) (*License, *time.Time, string) {
	dir := t.TempDir()
	now := time.Now().UTC().Truncate(time.Second)
	l := &License{Now: func() time.Time { return now }, MachineID: func() string { return id },
		Dirs: func() []string { return []string{filepath.Join(dir, "a"), filepath.Join(dir, "b")} }}
	return l, &now, dir
}

func TestLicenseCodeStable(t *testing.T) {
	l1, _, _ := testLicense(t, "pc-1")
	l2, _, _ := testLicense(t, "pc-1")
	l3, _, _ := testLicense(t, "pc-2")
	if l1.Code() != l2.Code() || l1.Code() == l3.Code() {
		t.Fatal("le code doit être stable et propre à chaque ordinateur")
	}
	if NormalizeCode(l1.Code()) != l1.Code() {
		t.Fatalf("code mal formé : %q", l1.Code())
	}
	if NormalizeCode("jbk 7f3a 92c1 d04e") != "" || NormalizeCode("JBK-ZZZZ-0000-0000-0000") != "" {
		t.Fatal("un code invalide doit être refusé")
	}
	if NormalizeCode(" jbk-7f3a-92c1-d04e-0a1b ") != "JBK-7F3A-92C1-D04E-0A1B" {
		t.Fatal("un code saisi à la main doit être normalisé")
	}
}

func TestLicenseKeyOnlyForThisComputer(t *testing.T) {
	priv := testPriv(t)
	l, _, _ := testLicense(t, "pc-1")
	other, _, _ := testLicense(t, "pc-2")
	key, err := SignCode(priv, l.Code())
	if err != nil {
		t.Fatal(err)
	}
	if l.Status().State == LicLicensed {
		t.Fatal("pas de licence au départ")
	}
	if err := other.Activate(key); err == nil {
		t.Fatal("la clé d'un autre ordinateur doit être refusée")
	}
	if err := l.Activate("n'importe quoi"); err == nil {
		t.Fatal("une clé fausse doit être refusée")
	}
	if err := l.Activate(strings.ToLower(key[:len(key)-1])); err == nil {
		t.Fatal("une clé tronquée doit être refusée")
	}
	if err := l.Activate("  " + strings.ReplaceAll(key, "-", " ") + "\n"); err != nil {
		t.Fatalf("la clé collée avec des espaces doit marcher : %v", err)
	}
	if l.Status().State != LicLicensed {
		t.Fatal("licence non reconnue après activation")
	}
	// un nouveau lancement (nouvel objet, mêmes fichiers) reste activé
	l2 := &License{Now: l.Now, MachineID: l.MachineID, Dirs: l.Dirs}
	if l2.Status().State != LicLicensed {
		t.Fatal("la licence doit rester après redémarrage")
	}
	// les fichiers copiés sur un autre ordinateur ne comptent pas
	l3 := &License{Now: l.Now, MachineID: func() string { return "pc-3" }, Dirs: l.Dirs}
	if l3.Status().State == LicLicensed {
		t.Fatal("la licence copiée sur un autre ordinateur doit être refusée")
	}
}

func TestLicenseKeyLength(t *testing.T) {
	priv := testPriv(t)
	l, _, _ := testLicense(t, "pc-1")
	key, _ := SignCode(priv, l.Code())
	if n := len(normalizeKey(key)); n != 103 {
		t.Fatalf("longueur de clé inattendue : %d", n)
	}
	if !VerifyKey(licensePub, l.Code(), key) {
		t.Fatal("la clé publique du logiciel ne correspond pas à la clé secrète du générateur")
	}
}

func TestDemo3Days(t *testing.T) {
	l, now, dir := testLicense(t, "pc-1")
	if s := l.Status(); s.State != LicLocked || s.DemoUsed {
		t.Fatalf("au départ : verrouillé, démo non utilisée (%+v)", s)
	}
	l.StartDemo()
	if s := l.Status(); s.State != LicDemo || s.DemoLeft < 71*time.Hour {
		t.Fatalf("démo lancée : %+v", s)
	}
	*now = now.Add(47 * time.Hour)
	if s := l.Status(); s.State != LicDemo || s.DemoLeft < 24*time.Hour || s.DemoLeft > 25*time.Hour {
		t.Fatalf("après 47 h : %+v", s)
	}
	// relancer la démo ne la remet pas à zéro
	l.StartDemo()
	if s := l.Status(); s.DemoLeft > 25*time.Hour {
		t.Fatalf("StartDemo ne doit pas remettre à zéro : %+v", s)
	}
	// on supprime un des deux fichiers : la démo continue de compter
	os.Remove(filepath.Join(dir, "a", demoFile))
	if s := l.Status(); s.State != LicDemo || s.DemoLeft > 25*time.Hour {
		t.Fatalf("un fichier supprimé ne doit rien remettre à zéro : %+v", s)
	}
	*now = now.Add(26 * time.Hour)
	if s := l.Status(); s.State != LicLocked || !s.DemoUsed {
		t.Fatalf("après 73 h : démo terminée (%+v)", s)
	}
	l.StartDemo() // pas de deuxième démo
	if s := l.Status(); s.State != LicLocked {
		t.Fatalf("pas de deuxième démo : %+v", s)
	}
	// supprimer les deux fichiers remet la démo à zéro : limite connue
}

func TestDemoClockRollback(t *testing.T) {
	l, now, _ := testLicense(t, "pc-1")
	l.StartDemo()
	*now = now.Add(60 * time.Hour)
	l.Status()                      // l'horloge enregistrée avance
	*now = now.Add(-50 * time.Hour) // l'utilisateur recule la date
	if s := l.Status(); s.State != LicDemo || s.DemoLeft > 13*time.Hour {
		t.Fatalf("une horloge reculée ne doit pas rendre de temps : %+v", s)
	}
	*now = now.Add(100 * time.Hour)
	if s := l.Status(); s.State != LicLocked {
		t.Fatalf("démo finie : %+v", s)
	}
}

func TestDemoTamper(t *testing.T) {
	l, _, dir := testLicense(t, "pc-1")
	l.StartDemo()
	p := filepath.Join(dir, "a", demoFile)
	b, _ := os.ReadFile(p)
	f := strings.Fields(string(b))
	f[1] = "1000" // date de début falsifiée
	os.WriteFile(p, []byte(strings.Join(f, "\n")+"\n"), 0o644)
	if s := l.Status(); s.State != LicLocked || !s.Tamper {
		t.Fatalf("un fichier falsifié doit verrouiller : %+v", s)
	}
	// un fichier de démo copié d'un autre ordinateur est refusé aussi
	l2, _, dir2 := testLicense(t, "pc-2")
	l2.StartDemo()
	b2, _ := os.ReadFile(filepath.Join(dir2, "a", demoFile))
	l3, _, dir3 := testLicense(t, "pc-3")
	os.MkdirAll(filepath.Join(dir3, "a"), 0o755)
	os.WriteFile(filepath.Join(dir3, "a", demoFile), b2, 0o644)
	if s := l3.Status(); s.State != LicLocked {
		t.Fatalf("démo copiée d'un autre ordinateur : %+v", s)
	}
}

func TestFormatDemoLeft(t *testing.T) {
	for d, w := range map[time.Duration]string{
		62*time.Hour + 20*time.Minute:   "2 j 14 h",
		5*time.Hour + 12*time.Minute:    "5 h 12 min",
		39*time.Minute + 30*time.Second: "40 min",
	} {
		if g := FormatDemoLeft(d); g != w {
			t.Errorf("%v : %q (attendu %q)", d, g, w)
		}
	}
}
