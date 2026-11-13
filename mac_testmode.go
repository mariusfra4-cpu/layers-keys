//go:build darwin && lktest

package main

import "time"

// PLUGIN DE TEST SEULEMENT (jamais distribué) : déjà autorisé, pour que le test automatique sur les Mac de GitHub
// puisse mesurer le son qui sort vraiment de l'Audio Unit.
var lkMacTestMode = "LK_MAC_TEST_MODE_ACTIVE"

func init() {
	licenseFactory = func() *License {
		return &License{Now: time.Now, MachineID: func() string { return lkMacTestMode }, override: &LicStatus{State: LicLicensed}}
	}
}
