package main

// Langues de l'interface : français (par défaut), anglais, espagnol.
// Les textes du logiciel sont écrits en français ; T() les traduit selon la langue choisie.

import (
	"fmt"
	"sync/atomic"
)

const (
	langFR = iota
	langEN
	langES
)

var curLang int32

func SetLang(l int) { atomic.StoreInt32(&curLang, int32(l)) }
func CurLang() int  { return int(atomic.LoadInt32(&curLang)) }
func langCode(l int) string {
	return [...]string{"fr", "en", "es"}[clamp(l, 0, 2)]
}
func langFromCode(s string) int {
	switch s {
	case "en":
		return langEN
	case "es":
		return langES
	}
	return langFR
}

// T traduit un texte français ; un texte inconnu (nom de son, de fichier…) reste tel quel.
func T(fr string) string {
	l := CurLang()
	if l == langFR {
		return fr
	}
	if t, ok := trTable[fr]; ok {
		if s := t[l-1]; s != "" {
			return s
		}
	}
	return fr
}

// Tf = fmt.Sprintf sur le texte traduit
func Tf(fr string, a ...any) string { return fmt.Sprintf(T(fr), a...) }

var trTable = map[string][2]string{
	// en-tête
	"Nouveau":        {"New", "Nuevo"},
	"Ouvrir":         {"Open", "Abrir"},
	"Enregistrer":    {"Save", "Guardar"},
	"Sous…":          {"Save as…", "Como…"},
	"Paramètres":     {"Settings", "Ajustes"},
	"Projet : %s":    {"Project: %s", "Proyecto: %s"},
	"Sans titre":     {"Untitled", "Sin título"},
	"Mon projet":     {"My project", "Mi proyecto"},
	"Aucun accord":   {"No chord", "Sin acorde"},
	"JOUE UN ACCORD": {"PLAY A CHORD", "TOCA UN ACORDE"},
	"%d / %d voix":   {"%d / %d voices", "%d / %d voces"},
	// pistes
	"Piste vide":                        {"Empty track", "Pista vacía"},
	"Double-clique pour charger":        {"Double-click to load", "Doble clic para cargar"},
	"Vide · double-clique pour charger": {"Empty · double-click to load", "Vacía · doble clic para cargar"},
	"CHARGEMENT…":                       {"LOADING…", "CARGANDO…"},
	"Piste %d":                          {"Track %d", "Pista %d"},
	"OCTAVE":                            {"OCTAVE", "OCTAVA"},
	"TRANSPO":                           {"TRANSPOSE", "TRANSP."},
	"MUET":                              {"MUTE", "SILENC."},
	"RÉGLAGES · SON %d":                 {"SETTINGS · SOUND %d", "AJUSTES · SONIDO %d"},
	"AUCUN SON":                         {"NO SOUND", "SIN SONIDO"},
	"AUCUN SON · CLIQUE SUR « CHARGER UN SON »": {"NO SOUND · CLICK « LOAD A SOUND »", "SIN SONIDO · PULSA « CARGAR UN SONIDO »"},
	"CHARGER UN SON": {"LOAD A SOUND", "CARGAR SONIDO"},
	"RENOMMER":       {"RENAME", "RENOMBRAR"},
	"PÉDALE":         {"PEDAL", "PEDAL"},
	"FILTRE LP 12":   {"LP 12 FILTER", "FILTRO LP 12"},
	"Volume général": {"Master volume", "Volumen general"},
	"Réverbération":  {"Reverb", "Reverberación"},
	"BRILLANCE":      {"BRIGHTNESS", "BRILLO"},
	"RÉSONANCE":      {"RESONANCE", "RESONANCIA"},
	"ATTAQUE":        {"ATTACK", "ATAQUE"},
	"RELÂCHEMENT":    {"RELEASE", "RELAJACIÓN"},
	"DYNAMIQUE":      {"DYNAMICS", "DINÁMICA"},
	"VITESSE":        {"RATE", "VELOCIDAD"},
	"Clavier du PC : lettres = notes  ·  Espace = sustain  ·  Échap = tout arrêter  ·  Molette = ajuster, double-clic = valeur par défaut": {
		"PC keyboard: letters = notes  ·  Space = sustain  ·  Esc = stop all  ·  Wheel = adjust, double-click = default value",
		"Teclado del PC: letras = notas  ·  Espacio = sustain  ·  Esc = parar todo  ·  Rueda = ajustar, doble clic = valor por defecto"},
	// sons / fichiers
	"Ouvrir un fichier : SF2, WAV ou MP3…":  {"Open a file: SF2, WAV or MP3…", "Abrir un archivo: SF2, WAV o MP3…"},
	"parcourir l'ordinateur":                {"browse the computer", "explorar el ordenador"},
	"Aucun son (vider cette piste)":         {"No sound (empty this track)", "Sin sonido (vaciar esta pista)"},
	"Fichiers récents":                      {"Recent files", "Archivos recientes"},
	"Sons de base (synthétisés)":            {"Basic sounds (synthesized)", "Sonidos básicos (sintetizados)"},
	"Choisir le son de la piste %d":         {"Choose the sound of track %d", "Elegir el sonido de la pista %d"},
	"Clique sur un son · Échap pour fermer": {"Click a sound · Esc to close", "Pulsa un sonido · Esc para cerrar"},
	"Tape pour filtrer · ↑ ↓ pour choisir · Entrée pour valider · Échap pour fermer": {"Type to filter · ↑ ↓ to choose · Enter to confirm · Esc to close", "Escribe para filtrar · ↑ ↓ para elegir · Intro para validar · Esc para cerrar"},
	"%d sons":                                {"%d sounds", "%d sonidos"},
	"Filtre : tape le nom d'un son…":         {"Filter: type a sound name…", "Filtro: escribe el nombre de un sonido…"},
	"Aucun son ne correspond.":               {"No matching sound.", "Ningún sonido coincide."},
	"Lecteur audio…":                         {"Audio player…", "Reproductor de audio…"},
	"le lecteur lit les fichiers WAV et MP3": {"the player reads WAV and MP3 files", "el reproductor lee archivos WAV y MP3"},
	"Format non pris en charge : choisis un fichier SF2, WAV ou MP3.":             {"Unsupported format: choose an SF2, WAV or MP3 file.", "Formato no compatible: elige un archivo SF2, WAV o MP3."},
	"Aucun son chargé : double-clique une piste (ou « SON ») pour en ajouter un.": {"No sound loaded: double-click a track to add one.", "Ningún sonido cargado: haz doble clic en una pista para añadir uno."},
	"ERREUR · NON CHARGÉ":                      {"ERROR · NOT LOADED", "ERROR · NO CARGADO"},
	"Nom du preset %d":                         {"Name of preset %d", "Nombre del preset %d"},
	"Nom de la piste %d  (vide = nom du son)":  {"Name of track %d  (empty = sound name)", "Nombre de la pista %d  (vacío = nombre del sonido)"},
	"Entrée pour valider · Échap pour annuler": {"Enter to confirm · Esc to cancel", "Intro para validar · Esc para cancelar"},
	"Preset %d « %s » enregistré":              {"Preset %d « %s » saved", "Preset %d « %s » guardado"},
	// projets
	"Impossible d'enregistrer le projet : %s":           {"Unable to save the project: %s", "No se puede guardar el proyecto: %s"},
	"Projet « %s » enregistré":                          {"Project « %s » saved", "Proyecto « %s » guardado"},
	"Projet « %s » ouvert":                              {"Project « %s » opened", "Proyecto « %s » abierto"},
	"Nouveau projet":                                    {"New project", "Nuevo proyecto"},
	"puis continuer":                                    {"then continue", "y continuar"},
	"Ne pas enregistrer":                                {"Don't save", "No guardar"},
	"perdre les modifications":                          {"discard changes", "perder los cambios"},
	"Annuler":                                           {"Cancel", "Cancelar"},
	"Enregistrer les modifications de « %s » ?":         {"Save changes to « %s »?", "¿Guardar los cambios de « %s »?"},
	"Clique sur un choix · Échap pour annuler":          {"Click an option · Esc to cancel", "Pulsa una opción · Esc para cancelar"},
	"Parcourir l'ordinateur…":                           {"Browse the computer…", "Explorar el ordenador…"},
	"fichier .lkeys":                                    {".mpiano file", "archivo .mpiano"},
	"Projets récents":                                   {"Recent projects", "Proyectos recientes"},
	"Ouvrir un projet":                                  {"Open a project", "Abrir un proyecto"},
	"Clique sur un projet · Échap pour fermer":          {"Click a project · Esc to close", "Pulsa un proyecto · Esc para cerrar"},
	"ce fichier n'est pas un projet Layers Keys valide": {"this file is not a valid Layers Keys project", "este archivo no es un proyecto válido de Layers Keys"},
	"ce fichier n'est pas un projet Layers Keys":        {"this file is not a Layers Keys project", "este archivo no es un proyecto de Layers Keys"},
	"ce projet a été créé avec une version plus récente de Layers Keys": {"this project was created with a newer version of Layers Keys", "este proyecto se creó con una versión más reciente de Layers Keys"},
	// paramètres
	"Paramètres de Layers Keys":              {"Layers Keys settings", "Ajustes de Layers Keys"},
	"Sortie audio":                           {"Audio output", "Salida de audio"},
	"Langue":                                 {"Language", "Idioma"},
	"Accords":                                {"Chords", "Acordes"},
	"MIDI":                                   {"MIDI", "MIDI"},
	"Pilote, carte son, fréquence, tampon":   {"Driver, sound card, sample rate, buffer", "Controlador, tarjeta de sonido, frecuencia, búfer"},
	"Notation des accords : lettres (C D E)": {"Chord notation: letters (C D E)", "Notación de acordes: letras (C D E)"},
	"Notation des accords : Do Ré Mi":        {"Chord notation: Do Re Mi", "Notación de acordes: Do Re Mi"},
	"touche pour changer":                    {"click to change", "pulsa para cambiar"},
	"Actualiser MIDI":                        {"Refresh MIDI", "Actualizar MIDI"},
	"PIANO TACTILE":                          {"TOUCH PIANO", "PIANO TÁCTIL"},
	"◀  MIXAGE":                              {"◀  MIXER", "◀  MEZCLA"},
	"PÉDALE (sustain)":                       {"PEDAL (sustain)", "PEDAL (sustain)"},
	"TAILLE":                                 {"SIZE", "TAMAÑO"},
	"Connecter un clavier MIDI Bluetooth…":   {"Connect a Bluetooth MIDI keyboard…", "Conectar un teclado MIDI Bluetooth…"},
	"USB : branche-le, il est reconnu tout seul": {"USB: plug it in, it is detected automatically", "USB: conéctalo, se reconoce solo"},
	"Chercher à nouveau les claviers MIDI":       {"Search for MIDI keyboards again", "Buscar de nuevo teclados MIDI"},
	"Clique sur un réglage · Échap pour fermer":  {"Click a setting · Esc to close", "Pulsa un ajuste · Esc para cerrar"},
	"Audio":                              {"Audio", "Audio"},
	"Appliquer et redémarrer l'audio":    {"Apply and restart audio", "Aplicar y reiniciar el audio"},
	"Pilote : %s":                        {"Driver: %s", "Controlador: %s"},
	"Carte son : %s":                     {"Sound card: %s", "Tarjeta de sonido: %s"},
	"Fréquence : %d Hz":                  {"Sample rate: %d Hz", "Frecuencia: %d Hz"},
	"Tampon : %d échantillons (%.1f ms)": {"Buffer: %d samples (%.1f ms)", "Búfer: %d muestras (%.1f ms)"},
	"Tampon : automatique":               {"Buffer: automatic", "Búfer: automático"},
	"Sorties ASIO : %d-%d":               {"ASIO outputs: %d-%d", "Salidas ASIO: %d-%d"},
	"Panneau de réglages du pilote":      {"Driver control panel", "Panel de control del controlador"},
	"celle de Windows":                   {"Windows default", "la de Windows"},
	"Choisir le pilote audio":            {"Choose the audio driver", "Elegir el controlador de audio"},
	"Choisir la carte son":               {"Choose the sound card", "Elegir la tarjeta de sonido"},
	"Choisir la fréquence":               {"Choose the sample rate", "Elegir la frecuencia"},
	"Choisir la taille du tampon":        {"Choose the buffer size", "Elegir el tamaño del búfer"},
	"Choisir les sorties":                {"Choose the outputs", "Elegir las salidas"},
	"Plus petit = moins de latence, mais plus de risque de craquements.": {"Smaller = lower latency, but more risk of crackling.", "Más pequeño = menos latencia, pero más riesgo de chasquidos."},
	"Audio actif : %s":      {"Audio running: %s", "Audio activo: %s"},
	"Son indisponible : %s": {"Audio unavailable: %s", "Audio no disponible: %s"},
	"Le dernier pilote audio a planté : retour au mode standard (WASAPI).": {"The last audio driver crashed: back to standard mode (WASAPI).", "El último controlador de audio falló: vuelta al modo estándar (WASAPI)."},
	"recommandé pour une carte son externe":                                {"recommended for an external sound card", "recomendado para una tarjeta de sonido externa"},
	"standard, partagé avec Windows":                                       {"standard, shared with Windows", "estándar, compartido con Windows"},
	"latence minimale, bloque les autres logiciels":                        {"minimum latency, blocks other software", "latencia mínima, bloquea otros programas"},
	"ancien système, compatibilité":                                        {"legacy system, compatibility", "sistema antiguo, compatibilidad"},
	"Aucun pilote trouvé":                                                  {"No driver found", "Ningún controlador encontrado"},
	"MIDI : aucun clavier":                                                 {"MIDI: no keyboard", "MIDI: ningún teclado"},
	// erreurs de fichiers
	"ce fichier MP3 est illisible ou corrompu":                       {"this MP3 file is unreadable or corrupted", "este archivo MP3 es ilegible o está dañado"},
	"fichier audio vide ou invalide":                                 {"empty or invalid audio file", "archivo de audio vacío o no válido"},
	"ce fichier n'est pas un WAV valide":                             {"this file is not a valid WAV", "este archivo no es un WAV válido"},
	"ce WAV est compressé : seuls les WAV PCM sont pris en charge":   {"this WAV is compressed: only PCM WAV files are supported", "este WAV está comprimido: solo se admiten WAV PCM"},
	"format non pris en charge (utilise un fichier SF2, WAV ou MP3)": {"unsupported format (use an SF2, WAV or MP3 file)", "formato no compatible (usa un archivo SF2, WAV o MP3)"},
}
