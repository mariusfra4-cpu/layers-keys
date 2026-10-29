//go:build !windows && !darwin

package main

import "fmt"

func main() { fmt.Println("Layers Keys ne fonctionne que sous Windows.") }
