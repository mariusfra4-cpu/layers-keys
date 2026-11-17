# Spacepad by JB Praise

Lecteur de pads et de crowds pour la scène. Écrit en C++ avec JUCE. Il existe en trois formats :
application autonome, plugin VST3, et plugin Audio Unit pour Logic Pro et GarageBand.

## Ce que fait le logiciel
- **Choisir un dossier…** : Spacepad analyse le dossier tout seul.
  - Il prend le **nom du dossier** comme nom de librairie.
  - Il décide si c'est une **Pad Library** ou une **Crowd Library**.
  - Les sous-dossiers deviennent des catégories (Worship FX, Natural…).
  - Il trouve la **tonalité** de chaque son : d'abord par le nom du fichier (C, C#, Db, Em, E minor, Do, Ré, Mi mineur…), sinon en écoutant le son.
  - Une tonalité absente est jouée en **transposant** le son le plus proche.
  - Un dossier qui contient plusieurs dossiers de pads donne plusieurs librairies.
- **Enregistrer** : la liste est écrite dans un dossier commun, que l'application, le VST3 et l'AU lisent tous. Une fois enregistrée, la librairie est donc reconnue dans Logic Pro, Ableton, etc.
  - Mac : `/Library/Application Support/JB Praise/Spacepad/libraries.json`
  - Windows : `C:\ProgramData\JB Praise\Spacepad\libraries.json`
  - Vos sons restent où ils sont et ne sont jamais modifiés.
- **Jeu** : on clique une tonalité sur l'anneau, ou on appuie sur une touche MIDI (Do, Ré… dans n'importe quelle octave).
  - Changer de tonalité fait un fondu enchaîné.
  - **LOOP** relit le son en boucle sans clic.
  - Majeur et Mineur se choisissent avec deux boutons.
  - Un changement de programme MIDI passe à une autre librairie.
- **Effets** : Shimmer, Warm, Air, Reverb, Motion, Width, Sub oct., Low cut, Attack, Fondu, Freeze, Swell. Les volumes du pad et des crowds se règlent séparément.
- **Projet** : la librairie, la tonalité et les réglages sont enregistrés dans le projet Logic ou Ableton.

## Construire
```
cmake -S spacepad -B build -DCMAKE_BUILD_TYPE=Release   # JUCE est téléchargé automatiquement
cmake --build build --target Spacepad_Standalone Spacepad_VST3 Spacepad_AU SpacepadTests
build/SpacepadTests_artefacts/Release/SpacepadTests captures/   # tests et captures de l'interface
```
Les installateurs sont faits par GitHub Actions (`.github/workflows/spacepad.yml`) :
- Windows : `installer/windows/spacepad.nsi`, publié dans la branche `ci-spacepad-win` ;
- Mac : `installer/mac/build_pkg.sh`, publié dans la branche `ci-spacepad-mac`.

## Licences
- JUCE 8 : licence gratuite (« Starter ») tant que le chiffre d'affaires reste sous 50 000 $ par an. Au-delà, il faut une licence payante, ou bien publier le code sous AGPLv3.
- SDK VST3 : licence MIT.
- Polices Michroma et Inter : licence SIL OFL (fichiers dans `resources/fonts`).
