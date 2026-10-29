//go:build darwin && (vst3 || au)

package main

import "errors"

// Dans un plugin, le DAW gère la sortie audio : le logiciel n'ouvre aucune carte son.

func audioAPIs() []string              { return []string{apiCoreAudio} }
func audioDevices(api string) []string { return nil }

func openAudio(e *Engine, cfg AudioCfg) (audioBackend, string, error) {
	return nil, "", errors.New("la sortie audio est gérée par le logiciel hôte")
}
