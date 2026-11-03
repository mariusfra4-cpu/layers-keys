package main

// Réglages de la sortie audio (communs à toutes les plateformes).

const (
	apiAuto      = ""          // choix automatique (WASAPI)
	apiWASAPI    = "wasapi"    // Windows moderne, partagé
	apiWASAPX    = "wasapix"   // Windows moderne, exclusif (latence minimale)
	apiASIO      = "asio"      // pilote ASIO de la carte son
	apiMME       = "mme"       // ancien système Windows (compatibilité)
	apiCoreAudio = "coreaudio" // Mac
	apiAAudio    = "aaudio"    // Android
)

// AudioCfg : sortie audio choisie par l'utilisateur (enregistrée avec ses réglages).
type AudioCfg struct {
	API    string `json:"api,omitempty"`
	Device string `json:"device,omitempty"` // nom du périphérique ("" = celui de Windows)
	Rate   int    `json:"rate,omitempty"`   // 44100, 48000, 96000
	Buffer int    `json:"buffer,omitempty"` // taille du tampon en échantillons (images)
	Pair   int    `json:"pair,omitempty"`   // ASIO : paire de sorties (0 = sorties 1-2)
}

func (a *AudioCfg) sanitize() {
	switch a.API {
	case apiAuto, apiWASAPI, apiWASAPX, apiASIO, apiMME, apiCoreAudio, apiAAudio:
	default:
		a.API = apiAuto
	}
	if a.Rate != 44100 && a.Rate != 48000 && a.Rate != 88200 && a.Rate != 96000 {
		a.Rate = 48000
	}
	a.Buffer = clamp(a.Buffer, 0, 8192)
	if a.Pair < 0 || a.Pair > 31 {
		a.Pair = 0
	}
	if len([]rune(a.Device)) > 120 {
		a.Device = ""
	}
}

// pilote choisi automatiquement selon le système (modifié au démarrage sur Mac)
var defaultAPI = apiWASAPI

// nom de la carte son « par défaut » dans les menus
var defaultDeviceName = "celle de Windows"

func apiLabel(a string) string {
	switch a {
	case apiCoreAudio:
		return "CoreAudio"
	case apiAAudio:
		return "Android (AAudio)"
	case apiASIO:
		return "ASIO"
	case apiWASAPX:
		return "WASAPI (exclusif)"
	case apiMME:
		return "MME (ancien)"
	default:
		return "WASAPI"
	}
}

// tampons proposés dans les réglages
var bufferChoices = []int{32, 64, 128, 192, 256, 384, 512, 1024}
var rateChoices = []int{44100, 48000, 88200, 96000}

func sortStrings(s []string) {
	for i := 1; i < len(s); i++ {
		for j := i; j > 0 && s[j] < s[j-1]; j-- {
			s[j], s[j-1] = s[j-1], s[j]
		}
	}
}
