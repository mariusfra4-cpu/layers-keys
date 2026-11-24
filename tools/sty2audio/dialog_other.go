//go:build !windows

package main

func pickStyles() []string { return nil }

func openFolder(string) {}
