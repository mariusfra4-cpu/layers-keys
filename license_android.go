//go:build android

package main

func machineIDRaw() string  { return androidMachineID() }
func licenseDirs() []string { return androidLicenseDirs() }
