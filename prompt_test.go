package main

import (
	"os"
	"testing"
)

type promptHost struct {
	Host
	asked string
}

func (p *promptHost) PromptText(title, initial string) (string, bool) {
	p.asked = title
	return "Louange du dimanche", true
}

// dans le plugin, le nom d'un preset passe par la case de texte du système
func TestPluginRenameUsesSystemField(t *testing.T) {
	u, _, _ := newTestUI(t, "")
	ph := &promptHost{Host: u.host}
	u.host = ph
	u.Plugin = true
	u.openRename(2)
	if ph.asked == "" || u.cfg.Presets[2].Name != "Louange du dimanche" {
		t.Fatalf("nom non enregistré : %q (question %q)", u.cfg.Presets[2].Name, ph.asked)
	}
	if u.ov != nil {
		t.Fatal("aucune fenêtre dessinée ne devrait rester ouverte")
	}
	// en standalone, la saisie reste dans le logiciel
	u.Plugin = false
	ph.asked = ""
	u.openRename(1)
	if ph.asked != "" || u.ov == nil || !u.ov.input {
		t.Fatal("standalone : la saisie habituelle devrait s'ouvrir")
	}
	u.closeOverlay()
	if dir := os.Getenv("LOOKSHOTS"); dir != "" {
		u.Plugin = true
		shotPNG(t, dir, "plugin_entete_ouvrir_enregistrer", u)
	}
}
