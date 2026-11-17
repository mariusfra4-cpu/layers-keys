package main

import (
	"os"
	"strings"
	"testing"
)

func TestFileErrTextPermission(t *testing.T) {
	err := &os.PathError{Op: "open", Path: "/Users/x/Downloads/FM EP.sf2", Err: os.ErrPermission}
	if s := fileErrText(err); !strings.Contains(s, "Musique") {
		t.Fatalf("message non expliqué : %q", s)
	}
	if s := fileErrText(os.ErrNotExist); strings.Contains(s, "Musique") {
		t.Fatalf("message de permission pour une autre erreur : %q", s)
	}
}
