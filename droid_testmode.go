//go:build android && droidtest

package main

import "time"

// APK DE TEST SEULEMENT (jamais distribué) : le logiciel est déjà autorisé, pour que les contrôles automatiques
// dans l'émulateur puissent jouer sans passer par l'écran de licence.
var lkTestMode = "LK_TEST_MODE_ACTIVE"

func init() {
	licenseFactory = func() *License {
		return &License{Now: time.Now, MachineID: func() string { return lkTestMode }, override: &LicStatus{State: LicLicensed}}
	}
}
