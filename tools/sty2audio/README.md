# sty2audio : styles Yamaha → MP3 et WAV

`sty2audio` convertit les fichiers de style Yamaha (`.sty`, `.prs`, `.bcs`,
`.sst`, `.fps`…) en fichiers audio **MP3** et **WAV**, section par section.

Il fonctionne seul, sans autre logiciel à installer. Il lit le style, le joue
avec son propre synthétiseur SoundFont et écrit les fichiers audio.

## Utilisation (Windows)

1. Copiez `bin/sty2audio.exe` dans un dossier (ou `bin/sty2audio32.exe` sur un
   Windows 32 bits).
2. **Glissez vos fichiers `.sty`** (ou un dossier entier) **sur `sty2audio.exe`**.
3. La première fois, s'il ne trouve aucune SoundFont, le programme propose de
   télécharger **GeneralUser GS** (gratuite, 32 Mo). Répondez `O` puis Entrée.
   Elle est rangée à côté du programme et resservira ensuite.
4. Les fichiers audio sont créés dans un dossier **« nom du style (audio) »** à
   côté de chaque style :

```
MonStyle (audio)/
  MonStyle - 00 Complet.mp3      ← Intro → Main A → Fill → Main B … → Ending
  MonStyle - 01 Intro A.mp3
  MonStyle - 02 Main A.mp3
  MonStyle - 03 Fill In AA.mp3
  …  (et les mêmes en .wav)
```

Vous pouvez aussi double-cliquer sur `sty2audio.exe` : il convertit alors tous
les styles qui se trouvent dans son propre dossier.

## Options (ligne de commande)

```
sty2audio [options] fichier.sty [autres fichiers ou dossiers…]
```

| Option | Rôle | Par défaut |
|---|---|---|
| `-sf2 "C:\Sons\mon.sf2"` | SoundFont à utiliser | recherche automatique |
| `-format mp3` / `wav` / `mp3,wav` | formats produits | `mp3,wav` |
| `-sortie "D:\Audio"` | dossier de sortie | à côté du style |
| `-repetitions 4` | nombre de passages de chaque Main | `2` |
| `-repetitions-fill 1` | passages de chaque Fill In / Break | `1` |
| `-tempo 96` | tempo en BPM | tempo du style |
| `-tonalite Re` | tonalité (Do, Re#, Mib, Sol, C, F#, Bb…) | celle du style |
| `-transposer -2` | transposition en demi-tons | `0` |
| `-sections "Main A,Intro A"` | ne convertir que certaines sections | toutes |
| `-complet=false` | pas de fichier d'enchaînement complet | `true` |
| `-bits 24` | WAV en 24 bits | `16` |
| `-mp3-debit 192` | débit MP3 en kbit/s | `320` |
| `-frequence 48000` | fréquence d'échantillonnage | `44100` |
| `-queue 6` | durée max. de la résonance finale (s) | `4` |
| `-reverb 0.5` | réverbération (0 = aucune, 2 = forte) | `1` |
| `-normaliser=false` | garder le volume brut | ajusté à -1 dBFS |
| `-infos` | afficher les sections sans convertir | |

Exemple : `sty2audio -tonalite Sol -tempo 100 -format mp3 "C:\Styles\8BeatPop.sty"`

## Précision du rendu

- **Lecture fidèle du style** : sections délimitées par les marqueurs Yamaha,
  initialisation SFF1/SInt appliquée, tempo et changements de tempo, bloc
  **CASM** (routage des canaux source vers les pistes Rythme, Basse, Accords,
  Pad, Phrases ; limites de notes), SysEx XG/GS/GM (mode batterie des parties,
  banque, programme, volume, panoramique, envois d'effets).
- **Synthétiseur SoundFont SF2 complet** : interpolation cubique, enveloppes de
  volume et de modulation, LFO de vibrato et de modulation, filtre passe-bas
  résonant, **tous les modulateurs SF2** (y compris ceux propres à chaque son),
  classes exclusives (charleston ouvert/fermé), pédale, pitch bend et sa
  sensibilité (RPN), réverbération et chorus.
- Le rendu a été comparé à FluidSynth (synthétiseur de référence) avec les
  mêmes SoundFonts : niveau des notes identique à 0,1 dB près (vélocité,
  volume, expression), et spectre d'un morceau complet à ±2 dB par bande.
- Le volume est ajusté une seule fois pour tout le style : les sections gardent
  leurs différences de volume d'origine.
- MP3 : encodé avec **LAME** si `ffmpeg.exe` est présent (à côté du programme ou
  dans le PATH), sinon avec l'encodeur intégré. Dans les deux cas : 320 kbit/s,
  stéréo, titre inscrit dans le fichier.

## Limites

- Le son dépend de la SoundFont : une SoundFont GM/GS ne contient pas les sons
  Yamaha d'origine (Mega Voices, Super Articulation, voix « Sweet! » ou « Cool! »).
  Pour chaque partie, le programme joue l'équivalent General MIDI le plus proche.
- Le style est joué dans son accord source (le plus souvent Do majeur 7). La
  tonalité et le tempo se changent avec `-tonalite` et `-tempo`. Le passage à un
  autre type d'accord (mineur, septième…) n'est pas pris en charge.
- Les SoundFonts compressées `.sf3` ne sont pas prises en charge : utilisez la
  version `.sf2`.

## Compilation

```
./build.sh       # bin/sty2audio.exe, bin/sty2audio32.exe, + Linux et macOS
go test ./...    # tests automatiques
```

L'encodeur MP3 intégré (`internal/shine`) est une copie de
[shine-mp3](https://github.com/braheezy/shine-mp3) (licence LGPL v2), modifiée
pour permettre 320 kbit/s.
