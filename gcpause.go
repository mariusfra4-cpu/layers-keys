package main

// Ancienne tentative (suspendre le ramasse-miettes pendant le chargement d'un son) retirée :
// avec de gros fichiers, la limite de mémoire était dépassée et le logiciel ralentissait fortement.
func gcPauseBegin() {}
func gcPauseEnd()   {}
