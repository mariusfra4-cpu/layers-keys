package main

import (
	"errors"
	"io/fs"
	"strings"
)

// fileErrText : texte d'erreur d'ouverture de fichier, compréhensible par un musicien.
// Sur Mac, la protection de la vie privée (Téléchargements, Documents, Bureau…) refuse l'accès à Logic, GarageBand
// ou au logiciel lui-même avec « operation not permitted » : on explique quoi faire au lieu du message système.
func fileErrText(err error) string {
	if err == nil {
		return ""
	}
	s := err.Error()
	if errors.Is(err, fs.ErrPermission) || strings.Contains(s, "operation not permitted") || strings.Contains(s, "permission denied") {
		return T("accès refusé par le système. Mets tes sons dans le dossier Musique, ou autorise le logiciel (ou Logic / GarageBand) dans Réglages Système > Confidentialité et sécurité.")
	}
	return s
}
