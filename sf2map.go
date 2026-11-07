package main

// Accès aux échantillons d'un SoundFont par « projection mémoire » : le fichier est présenté au programme comme
// un grand tableau, et le système (Windows, Mac) ne lit sur le disque que ce qui sert, au fur et à mesure.
// Charger un son de 600 Mo ne copie donc plus 600 Mo dans la mémoire du programme : plus de grosse réservation
// de mémoire, plus de travail du ramasse-miettes, plus de craquements pendant le chargement.
// Si la projection est impossible (fichier sur un disque exotique, Windows 32 bits à court d'adresses…),
// on revient à la lecture classique.

import (
	"time"
	"unsafe"
)

// view : le fichier entier projeté en lecture seule, ou nil.
func (s *SF2) view() []byte {
	s.mapOnce.Do(func() {
		if s.sf3 {
			return
		}
		fi, err := s.f.Stat()
		if err != nil || fi.Size() < 1<<20 { // petits fichiers : la lecture classique est aussi rapide
			return
		}
		if b, err := mapFileOS(s.f, fi.Size()); err == nil && int64(len(b)) == fi.Size() {
			s.mm = b
		}
	})
	return s.mm
}

// viewData : n échantillons 16 bits à partir de l'octet off du fichier, sans copie.
func (s *SF2) viewData(off, n int64) []int16 {
	mm := s.view()
	if mm == nil || n <= 0 || off < 0 || off%2 != 0 || off+n*2 > int64(len(mm)) {
		return nil
	}
	return unsafe.Slice((*int16)(unsafe.Pointer(&mm[off])), int(n))
}

// touch lit une fois chaque page des échantillons demandés, en arrière-plan de l'audio :
// ainsi, quand le son est annoncé « chargé », il est déjà en mémoire et le premier jeu ne va pas chercher sur le disque.
func (s *SF2) touch(ids []int) {
	var sink int16
	chunk := 0
	for _, id := range ids {
		d, ok := s.cacheGet(id)
		if !ok {
			continue
		}
		for i := 0; i < len(d); i += 2048 { // 4096 octets
			sink += d[i]
			chunk++
			if chunk >= 1024 { // toutes les 4 Mo : courte pause pour laisser la main au son
				chunk = 0
				time.Sleep(200 * time.Microsecond)
			}
		}
	}
	touchSink = sink
}

var touchSink int16

func (s *SF2) cacheGet(id int) ([]int16, bool) {
	s.mu.Lock()
	d, ok := s.cache[id]
	s.mu.Unlock()
	return d, ok
}
