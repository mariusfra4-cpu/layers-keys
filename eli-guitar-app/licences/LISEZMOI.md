# Licences Eli Guitar Chords (contrôle à distance)

## Mise en place (une seule fois, 2 minutes)

1. Sur GitHub, crée un **nouveau dépôt PUBLIC** nommé exactement **`eli-licences`**
   (compte `mariusfra4-cpu`). Coche « Add a README » pour qu'il ne soit pas vide.
2. Dans ce dépôt : **Add file > Create new file**, nom **`licences.txt`**, colle le contenu de
   `eli-guitar-app/licences/licences.txt`, puis **Commit changes** (branche `main`).
3. Vérifie que cette adresse affiche bien le fichier :
   https://raw.githubusercontent.com/mariusfra4-cpu/eli-licences/main/licences.txt

Tant que ce fichier n'existe pas, **aucun téléphone ne peut s'activer** (l'appli dit
« Impossible de vérifier la licence »).

Le dépôt `layers-keys` peut passer en privé : l'appli ne lit que `eli-licences`.
`eli-licences` doit rester **public** (il ne contient que des codes de téléphones, aucun nom).

## Activer un client

1. Le client installe l'APK et l'ouvre : l'écran affiche **le code de son téléphone**
   (`ELI-XXXX-XXXX-XXXX-XXXX`). Il te l'envoie avec le bouton « Envoyer à Eli Praise ».
2. Sur GitHub (téléphone ou ordinateur) : ouvre `licences.txt` dans `eli-licences`, touche le
   crayon ✏️, ajoute une ligne avec ce code, puis **Commit changes**.
3. Le client touche **Vérifier ma licence** (compte environ 5 minutes après ta modification).

## Ce que tu peux écrire dans `licences.txt`

| Ligne | Effet |
|---|---|
| `ELI-AB2C-DE3F-GH4J-KL5M` | téléphone activé, sans limite |
| `ELI-AB2C-DE3F-GH4J-KL5M 2027-06-30` | activé jusqu'au 30 juin 2027 inclus |
| `bloque ELI-AB2C-DE3F-GH4J-KL5M` | téléphone bloqué (ou supprime simplement sa ligne) |
| `anciens-codes 2027-01-31` | anciens codes `ELI-XXXX-XXXX` acceptés jusqu'à cette date |
| `bloque-ancien ELI-XXXX-XXXX` | un ancien code bloqué |
| `tout-bloquer` | l'appli est suspendue pour tout le monde |
| `message Texte…` | message affiché sur l'écran de licence |
| `# …` | commentaire (ex. `# client 1`) |

## Quand le changement est pris en compte

- L'appli vérifie à chaque ouverture si la dernière vérification date de plus de 3 heures.
- Un téléphone bloqué se verrouille donc au plus tard quelques heures après ta modification
  (tout de suite s'il rouvre l'appli après ce délai).
- **Sans Internet**, l'appli reste ouverte **7 jours** après la dernière vérification réussie,
  puis demande une connexion.
- Une horloge du téléphone remise en arrière est repérée.

## Anciens clients (codes de la version web)

Pendant la transition (`anciens-codes AAAA-MM-JJ`), un ancien client touche
« J'ai déjà un code d'accès » et entre son code : l'appli s'ouvre. Après cette date, il doit
t'envoyer le code de son téléphone. Le code de test `ELI-TEST-2026` est un ancien code : pense à
le bloquer (`bloque-ancien ELI-TEST-2026`) si tu ne veux pas qu'il serve à d'autres.
