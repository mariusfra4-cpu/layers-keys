//go:build windows

// Installeur de Layers Keys : copie le programme, crée les raccourcis
// et ajoute l'entrée « Applications installées » de Windows.
// Le logiciel s'installe dans le profil de l'utilisateur (sans droits administrateur).
// Le plugin VST3 va dans le dossier VST3 commun de Windows : Windows demande alors une autorisation.
package main

import (
	_ "embed"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"unsafe"
)

//go:embed payload/LayersKeys64.exe
var exe64 []byte

//go:embed payload/LayersKeys32.exe
var exe32 []byte

//go:embed payload/LayersKeys.vst3
var vst3dll []byte

const (
	appName    = "Layers Keys"
	appVersion = "1.1.0"
	regPath    = `Software\Microsoft\Windows\CurrentVersion\Uninstall\LayersKeys`
)

var user32 = syscall.NewLazyDLL("user32.dll")
var pMessageBox = user32.NewProc("MessageBoxW")

const (
	mbOK        = 0x0
	mbYesNo     = 0x4
	mbIconInfo  = 0x40
	mbIconQuest = 0x20
	mbIconError = 0x10
	idYes       = 6
)

func msgBox(text, title string, flags uintptr) int {
	t, _ := syscall.UTF16PtrFromString(text)
	c, _ := syscall.UTF16PtrFromString(title)
	r, _, _ := pMessageBox.Call(0, uintptr(unsafe.Pointer(t)), uintptr(unsafe.Pointer(c)), flags)
	return int(r)
}

func installDir() string {
	base := os.Getenv("LOCALAPPDATA")
	if base == "" {
		base = filepath.Join(os.Getenv("USERPROFILE"), "AppData", "Local")
	}
	return filepath.Join(base, "Programs", "Layers Keys")
}

// dossier VST3 commun à tous les logiciels de musique
func vst3Dir() string {
	base := os.Getenv("CommonProgramW6432") // le vrai dossier 64 bits, même depuis un programme 32 bits
	if base == "" {
		base = os.Getenv("CommonProgramFiles")
	}
	if base == "" {
		base = `C:\Program Files\Common Files`
	}
	return filepath.Join(base, "VST3", "Layers Keys.vst3")
}

func installVST3() error {
	d := filepath.Join(vst3Dir(), "Contents", "x86_64-win")
	if err := os.MkdirAll(d, 0o755); err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(d, "Layers Keys.vst3"), vst3dll, 0o644)
}

// relance l'installateur avec les droits administrateur pour une seule tâche (/vst3 ou /removevst3)
func elevated(arg string) bool {
	self, _ := os.Executable()
	ps := fmt.Sprintf(`try { $p = Start-Process -FilePath '%s' -ArgumentList '%s' -Verb RunAs -Wait -PassThru; exit $p.ExitCode } catch { exit 99 }`, self, arg)
	return hidden("powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", ps).Run() == nil
}

func is64() bool {
	return os.Getenv("PROCESSOR_ARCHITEW6432") != "" || os.Getenv("PROCESSOR_ARCHITECTURE") == "AMD64" || os.Getenv("PROCESSOR_ARCHITECTURE") == "ARM64"
}

func hidden(name string, args ...string) *exec.Cmd {
	c := exec.Command(name, args...)
	c.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: 0x08000000}
	return c
}

const psShortcut = `
$w = New-Object -ComObject WScript.Shell
foreach ($d in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {
  if (-not $d) { continue }
  $s = $w.CreateShortcut((Join-Path $d 'Layers Keys.lnk'))
  $s.TargetPath = $env:MP_TARGET
  $s.WorkingDirectory = $env:MP_DIR
  $s.IconLocation = $env:MP_TARGET + ',0'
  $s.Description = 'Layers Keys'
  $s.Save()
}
`

const psRemove = `
foreach ($d in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {
  if ($d) { Remove-Item -LiteralPath (Join-Path $d 'Layers Keys.lnk') -Force -ErrorAction SilentlyContinue }
}
`

var (
	advapi32        = syscall.NewLazyDLL("advapi32.dll")
	pRegCreateKeyEx = advapi32.NewProc("RegCreateKeyExW")
	pRegSetValueEx  = advapi32.NewProc("RegSetValueExW")
	pRegDeleteKey   = advapi32.NewProc("RegDeleteKeyW")
	pRegCloseKey    = advapi32.NewProc("RegCloseKey")
	hkcu            = uintptr(0x80000001) // HKEY_CURRENT_USER
	keyWrite64      = uintptr(0x20006 | 0x100)
	hKey            uintptr // variable globale : son adresse ne bouge jamais
	dwordBuf        uint32
	keepAlive       []interface{}
)

func setReg(key uintptr, name, val string) {
	n, _ := syscall.UTF16PtrFromString(name)
	v, _ := syscall.UTF16FromString(val)
	keepAlive = append(keepAlive, n, v)
	pRegSetValueEx.Call(key, uintptr(unsafe.Pointer(n)), 0, 1 /*REG_SZ*/, uintptr(unsafe.Pointer(&v[0])), uintptr(len(v)*2))
}

func setRegDword(key uintptr, name string, val uint32) {
	n, _ := syscall.UTF16PtrFromString(name)
	dwordBuf = val
	keepAlive = append(keepAlive, n)
	pRegSetValueEx.Call(key, uintptr(unsafe.Pointer(n)), 0, 4 /*REG_DWORD*/, uintptr(unsafe.Pointer(&dwordBuf)), 4)
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, in)
	return err
}

func install() {
	dir := installDir()
	if msgBox("Installer Layers Keys sur cet ordinateur ?\n\nDossier : "+dir, "Installation de Layers Keys", mbYesNo|mbIconQuest) != idYes {
		return
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		msgBox("Impossible de créer le dossier :\n"+err.Error(), appName, mbIconError)
		return
	}
	data := exe32
	if is64() {
		data = exe64
	}
	target := filepath.Join(dir, "LayersKeys.exe")
	if err := os.WriteFile(target, data, 0o755); err != nil {
		msgBox("Impossible d'écrire LayersKeys.exe.\nSi Layers Keys est ouvert, ferme-le puis recommence.\n\n"+err.Error(), appName, mbIconError)
		return
	}
	self, _ := os.Executable()
	setup := filepath.Join(dir, "Desinstaller.exe")
	if self != setup {
		_ = copyFile(self, setup)
	}

	// raccourcis (bureau + menu Démarrer)
	c := hidden("powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", psShortcut)
	c.Env = append(os.Environ(), "MP_TARGET="+target, "MP_DIR="+dir)
	_ = c.Run()

	// entrée de désinstallation dans Windows
	rp, _ := syscall.UTF16PtrFromString(regPath)
	keepAlive = append(keepAlive, rp)
	if r, _, _ := pRegCreateKeyEx.Call(hkcu, uintptr(unsafe.Pointer(rp)), 0, 0, 0, keyWrite64, 0, uintptr(unsafe.Pointer(&hKey)), 0); r == 0 {
		setReg(hKey, "DisplayName", appName)
		setReg(hKey, "DisplayVersion", appVersion)
		setReg(hKey, "InstallLocation", dir)
		setReg(hKey, "DisplayIcon", target)
		setReg(hKey, "UninstallString", `"`+setup+`" /uninstall`)
		setRegDword(hKey, "NoModify", 1)
		setRegDword(hKey, "NoRepair", 1)
		pRegCloseKey.Call(hKey)
	}

	vst := ""
	if is64() {
		if installVST3() != nil && !elevated("/vst3") {
			vst = "\n\nLe plugin VST3 n'a pas pu être installé (autorisation refusée). Relance l'installation et accepte la demande de Windows."
		} else if _, err := os.Stat(filepath.Join(vst3Dir(), "Contents", "x86_64-win", "Layers Keys.vst3")); err == nil {
			vst = "\n\nLe plugin VST3 est aussi installé : dans Ableton, FL Studio, Reaper… fais « rechercher les plugins » et choisis « Layers Keys »."
		} else {
			vst = "\n\nLe plugin VST3 n'a pas pu être installé."
		}
	} else {
		vst = "\n\n(Le plugin VST3 demande un Windows 64 bits.)"
	}

	if msgBox("Layers Keys est installé !\n\nTu le trouves sur le Bureau et dans le menu Démarrer."+vst+"\n\nLe lancer maintenant ?", appName, mbYesNo|mbIconInfo) == idYes {
		_ = exec.Command(target).Start()
	}
}

func uninstall() {
	dir := installDir()
	if msgBox("Désinstaller Layers Keys de cet ordinateur ?", "Désinstallation", mbYesNo|mbIconQuest) != idYes {
		return
	}
	_ = hidden("taskkill.exe", "/IM", "LayersKeys.exe", "/F").Run()
	if _, err := os.Stat(vst3Dir()); err == nil {
		if os.RemoveAll(vst3Dir()) != nil {
			elevated("/removevst3")
		}
	}
	_ = hidden("powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", psRemove).Run()
	rp, _ := syscall.UTF16PtrFromString(regPath)
	pRegDeleteKey.Call(hkcu, uintptr(unsafe.Pointer(rp)))
	keepAlive = append(keepAlive, rp)
	// le programme de désinstallation ne peut pas se supprimer lui-même : on le fait juste après sa fermeture
	_ = hidden("cmd.exe", "/C", fmt.Sprintf(`ping -n 3 127.0.0.1 >nul & rmdir /S /Q "%s"`, dir)).Start()
	msgBox("Layers Keys a été désinstallé.", appName, mbIconInfo)
}

func main() {
	for _, a := range os.Args[1:] {
		switch a {
		case "/vst3": // tâche lancée avec les droits administrateur
			if installVST3() != nil {
				os.Exit(1)
			}
			return
		case "/removevst3":
			if os.RemoveAll(vst3Dir()) != nil {
				os.Exit(1)
			}
			return
		}
		if a == "/uninstall" || a == "--uninstall" {
			uninstall()
			return
		}
	}
	install()
}
