package main

// Licence de Layers Keys : une clé = un ordinateur, sans connexion Internet.
//
//   - Chaque ordinateur a un « code » (JBK-XXXX-XXXX-XXXX-XXXX) calculé à partir de son identifiant interne.
//   - La clé de licence est la SIGNATURE (Ed25519) de ce code. Seul « JB Générateur de clés » connaît la clé secrète :
//     le logiciel ne contient que la clé publique, qui sert à vérifier — jamais à fabriquer — une licence.
//   - Mode démo : 3 jours à partir du clic sur « Mode démo ». La date de début est écrite à deux endroits et protégée
//     par une empreinte ; une horloge remise en arrière est repérée.
//   - La licence et la démo sont dans un dossier commun à l'ordinateur : l'application, le VST3 et l'AU partagent le même état.

import (
	"crypto/ed25519"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base32"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

// clé publique de vérification (la clé secrète n'est que dans « JB Générateur de clés »)
var licensePub = ed25519.PublicKey{
	0xa7, 0x9f, 0xd0, 0xbd, 0x77, 0xdf, 0x4f, 0xe5, 0xcd, 0xef, 0xc3, 0x66, 0xa4, 0xe0, 0x85, 0xe7,
	0xd3, 0x56, 0x97, 0x7a, 0x55, 0x68, 0xf1, 0x2f, 0x0f, 0xcd, 0xef, 0x7f, 0xa3, 0x94, 0x91, 0x8e,
}

const (
	licenseMsgPrefix = "LayersKeys|"
	demoDuration     = 72 * time.Hour
	demoSecret       = "JBPraise.LayersKeys.demo.v1"
)

type LicState int

const (
	LicLocked   LicState = iota // ni licence ni démo en cours
	LicDemo                     // démo en cours
	LicLicensed                 // licence valide pour cet ordinateur
)

// LicStatus : l'état de la licence à un instant donné.
type LicStatus struct {
	State    LicState
	DemoLeft time.Duration // temps de démo restant (State == LicDemo)
	DemoUsed bool          // la démo a déjà été lancée un jour (et elle est finie si State == LicLocked)
	Tamper   bool          // les fichiers de démo ont été modifiés à la main
}

// License gère l'état de la licence. Tout ce qui dépend de l'ordinateur est remplaçable pour les tests.
type License struct {
	Now       func() time.Time
	MachineID func() string   // identifiant brut de l'ordinateur
	Dirs      func() []string // dossiers où lire et écrire : commun à l'ordinateur, puis propre à l'utilisateur

	mu       sync.Mutex
	code     string
	licensed bool
	checked  bool
	lastTick time.Time
	override *LicStatus // seulement pour les tests
}

func NewLicense() *License {
	return &License{Now: time.Now, MachineID: machineIDRaw, Dirs: licenseDirs}
}

// licenseFactory : remplaçable dans les tests (pour que l'interface soit déjà « activée »)
var licenseFactory = NewLicense

// ---------------------------------------------------------------- code de l'ordinateur

func formatCode(raw string) string {
	h := sha256.Sum256([]byte("JBK1|" + strings.ToLower(strings.TrimSpace(raw))))
	x := strings.ToUpper(hex.EncodeToString(h[:8]))
	return "JBK-" + x[0:4] + "-" + x[4:8] + "-" + x[8:12] + "-" + x[12:16]
}

// Code : le code de cet ordinateur, à envoyer à JB Praise.
func (l *License) Code() string {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.code == "" {
		l.code = formatCode(l.MachineID())
	}
	return l.code
}

// NormalizeCode met un code saisi à la main sous la forme JBK-XXXX-XXXX-XXXX-XXXX ("" si invalide).
func NormalizeCode(s string) string {
	s = strings.ToUpper(strings.TrimSpace(s))
	s = strings.NewReplacer(" ", "", "-", "", "\n", "", "\r", "", "\t", "").Replace(s)
	// la lettre O est prise pour un zéro : les codes n'ont que 0-9 et A-F, la confusion est fréquente à la lecture
	if strings.HasPrefix(s, "JBK") {
		s = "JBK" + strings.ReplaceAll(s[3:], "O", "0")
	}
	if !strings.HasPrefix(s, "JBK") || len(s) != 19 {
		return ""
	}
	for _, r := range s[3:] {
		if !(r >= '0' && r <= '9' || r >= 'A' && r <= 'F') {
			return ""
		}
	}
	return "JBK-" + s[3:7] + "-" + s[7:11] + "-" + s[11:15] + "-" + s[15:19]
}

// ---------------------------------------------------------------- clé de licence

var keyEnc = base32.StdEncoding.WithPadding(base32.NoPadding)

func formatKey(sig []byte) string {
	s := keyEnc.EncodeToString(sig)
	var parts []string
	for len(s) > 8 {
		parts = append(parts, s[:8])
		s = s[8:]
	}
	parts = append(parts, s)
	return strings.Join(parts, "-")
}

func normalizeKey(s string) string {
	s = strings.ToUpper(s)
	return strings.NewReplacer(" ", "", "-", "", "\n", "", "\r", "", "\t", "").Replace(s)
}

// SignCode fabrique la clé de licence d'un code (utilisé par « JB Générateur de clés » et par les tests).
func SignCode(priv ed25519.PrivateKey, code string) (string, error) {
	c := NormalizeCode(code)
	if c == "" {
		return "", errors.New("code invalide")
	}
	return formatKey(ed25519.Sign(priv, []byte(licenseMsgPrefix+c))), nil
}

// VerifyKey : cette clé est-elle la bonne pour ce code ?
func VerifyKey(pub ed25519.PublicKey, code, key string) bool {
	c := NormalizeCode(code)
	if c == "" {
		return false
	}
	sig, err := keyEnc.DecodeString(normalizeKey(key))
	if err != nil || len(sig) != ed25519.SignatureSize {
		return false
	}
	return ed25519.Verify(pub, []byte(licenseMsgPrefix+c), sig)
}

// ---------------------------------------------------------------- fichiers

const licFile = "license.dat"
const demoFile = "demo.dat"

func (l *License) readLicenseFiles() bool {
	code := l.codeNoLock()
	for _, d := range l.Dirs() {
		b, err := os.ReadFile(filepath.Join(d, licFile))
		if err != nil {
			continue
		}
		lines := strings.Fields(string(b))
		if len(lines) >= 3 && lines[0] == "LK1" && NormalizeCode(lines[1]) == code && VerifyKey(licensePub, code, lines[2]) {
			return true
		}
	}
	return false
}

func (l *License) codeNoLock() string {
	if l.code == "" {
		l.code = formatCode(l.MachineID())
	}
	return l.code
}

// Activate vérifie la clé collée par le client et l'enregistre.
func (l *License) Activate(key string) error {
	l.mu.Lock()
	defer l.mu.Unlock()
	code := l.codeNoLock()
	k := normalizeKey(key)
	if k == "" {
		return errors.New("Colle d'abord la clé de licence.")
	}
	if !VerifyKey(licensePub, code, k) {
		return errors.New("Cette clé n'est pas valable pour cet ordinateur. Vérifie qu'elle est complète, ou redemande-la avec le code affiché.")
	}
	content := "LK1\n" + code + "\n" + formatKeyFromNorm(k) + "\n"
	ok := false
	for _, d := range l.Dirs() {
		if os.MkdirAll(d, 0o777) != nil {
			continue
		}
		if os.WriteFile(filepath.Join(d, licFile), []byte(content), 0o666) == nil {
			_ = os.Chmod(filepath.Join(d, licFile), 0o666)
			ok = true
		}
	}
	if !ok {
		return errors.New("Impossible d'enregistrer la licence sur cet ordinateur (droits d'écriture refusés).")
	}
	l.licensed, l.checked = true, true
	return nil
}

func formatKeyFromNorm(k string) string {
	sig, err := keyEnc.DecodeString(k)
	if err != nil {
		return k
	}
	return formatKey(sig)
}

// ---------------------------------------------------------------- démo

type demoRec struct {
	start, last int64 // secondes Unix
	ok          bool  // empreinte correcte
}

func (l *License) demoMAC(start, last int64) string {
	m := hmac.New(sha256.New, []byte(demoSecret+"|"+l.codeNoLock()))
	m.Write([]byte(strconv.FormatInt(start, 10) + "|" + strconv.FormatInt(last, 10)))
	return hex.EncodeToString(m.Sum(nil)[:12])
}

func (l *License) readDemo(dir string) (demoRec, bool) {
	b, err := os.ReadFile(filepath.Join(dir, demoFile))
	if err != nil {
		return demoRec{}, false
	}
	f := strings.Fields(string(b))
	if len(f) != 4 || f[0] != "LKD1" {
		return demoRec{ok: false}, true
	}
	s, e1 := strconv.ParseInt(f[1], 10, 64)
	la, e2 := strconv.ParseInt(f[2], 10, 64)
	if e1 != nil || e2 != nil {
		return demoRec{ok: false}, true
	}
	return demoRec{start: s, last: la, ok: hmac.Equal([]byte(f[3]), []byte(l.demoMAC(s, la)))}, true
}

func (l *License) writeDemo(start, last int64) {
	content := fmt.Sprintf("LKD1\n%d\n%d\n%s\n", start, last, l.demoMAC(start, last))
	for _, d := range l.Dirs() {
		if os.MkdirAll(d, 0o777) != nil {
			continue
		}
		if os.WriteFile(filepath.Join(d, demoFile), []byte(content), 0o666) == nil {
			_ = os.Chmod(filepath.Join(d, demoFile), 0o666)
		}
	}
}

// StartDemo lance la démo de 3 jours (sans effet si elle a déjà été lancée).
func (l *License) StartDemo() {
	l.mu.Lock()
	defer l.mu.Unlock()
	for _, d := range l.Dirs() {
		if _, found := l.readDemo(d); found {
			return
		}
	}
	now := l.Now().Unix()
	l.writeDemo(now, now)
}

// Status calcule l'état actuel. Elle met aussi à jour l'« horloge » enregistrée (détection du retour en arrière).
func (l *License) Status() LicStatus {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.override != nil {
		return *l.override
	}
	if !l.checked || !l.licensed {
		l.licensed = l.readLicenseFiles()
		l.checked = true
	}
	if l.licensed {
		return LicStatus{State: LicLicensed}
	}
	var st LicStatus
	var start, last int64
	have := false
	for _, d := range l.Dirs() {
		r, found := l.readDemo(d)
		if !found {
			continue
		}
		st.DemoUsed = true
		if !r.ok {
			st.Tamper = true
			continue
		}
		if !have || r.start < start {
			start = r.start
		}
		if !have || r.last > last {
			last = r.last
		}
		have = true
	}
	if st.Tamper || !have {
		return st // démo absente ou falsifiée : verrouillé
	}
	now := l.Now().Unix()
	if last > now {
		now = last // horloge remise en arrière : on garde la dernière heure connue
	}
	left := demoDuration - time.Duration(now-start)*time.Second
	if left <= 0 {
		return st
	}
	st.State, st.DemoLeft = LicDemo, left
	// l'horloge enregistrée avance (au plus une fois par minute)
	if now-last >= 60 {
		l.writeDemo(start, now)
	}
	return st
}

// ---------------------------------------------------------------- dossiers (par système)

func licenseUserDir() string {
	d, err := os.UserConfigDir()
	if err != nil || d == "" {
		d = os.TempDir()
	}
	return filepath.Join(d, "LayersKeys")
}

// FormatDemoLeft : « 2 j 14 h », « 5 h 12 min », « 40 min ».
func FormatDemoLeft(d time.Duration) string {
	if d < 0 {
		d = 0
	}
	m := int(d.Minutes())
	switch {
	case m >= 24*60:
		return fmt.Sprintf("%d j %d h", m/(24*60), (m%(24*60))/60)
	case m >= 60:
		return fmt.Sprintf("%d h %02d min", m/60, m%60)
	}
	return fmt.Sprintf("%d min", m+1)
}
