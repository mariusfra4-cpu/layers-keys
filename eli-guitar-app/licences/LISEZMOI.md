# Licences Eli Guitar Chords (contrôle à distance)

## Mise en place (une seule fois, 2 minutes)

1. Sur GitHub, crée un **nouveau dépôt PUBLIC** nommé exactement **`eli-licences`**
   (compte `mariusfra4-cpu`). Coche « Add a README » pour qu'il ne soit pas vide.
2. Dans ce dépôt : **Add file > Create new file**, nom **`licences.txt`**, colle le contenu de
   `eli-guitar-app/licences/licences.txt`, puis **Commit changes** (branche `main`).
3. Vérifie que cette adresse affiche bien le fichier :
   https://raw.githubusercontent.com/mariusfra4-cpu/eli-licences/main/licences.txt

Les clés du générateur marchent même si ce fichier n'existe pas encore ; mais il faut ce fichier pour
pouvoir **bloquer** quelqu'un (et pour l'activation par la liste).

Le dépôt `layers-keys` peut passer en privé : l'appli ne lit que `eli-licences`.
`eli-licences` doit rester **public** (il ne contient que des codes de téléphones, aucun nom).

## Activer un client (avec le générateur « Eli Clés »)

1. Installe **Eli-Cles-Generateur.apk** sur TON téléphone (jamais chez un client).
   Au premier lancement, entre ton mot de passe (donné à part) et laisse « Rester déverrouillé » coché.
2. Le client installe Eli Guitar Chords : l'écran affiche **le code de son téléphone**
   (`ELI-XXXX-XXXX-XXXX-XXXX`). Il te l'envoie avec « Envoyer à Eli Praise » (WhatsApp…).
3. Dans WhatsApp, appuie longuement sur son message > **Partager** > **Eli Clés** : le code est rempli
   et la clé est fabriquée. (Ou : copie le message, puis « COLLER » dans Eli Clés.)
4. **ENVOYER AU CLIENT** : il reçoit sa clé, touche « Coller ma clé » puis « Activer ». C'est fait,
   **même sans Internet**, pour toujours (sauf si tu le bloques).

Une clé ne marche que sur le téléphone dont elle porte le code : la partager ne sert à rien.

## Bloquer un client

Dans Eli Clés : touche le client dans l'historique > **BLOQUER CE CLIENT** > « Copier et ouvrir GitHub ».
Colle la ligne `bloque ELI-…` dans `licences.txt` et enregistre (**Commit changes**).
Son appli se verrouille dès qu'elle se connecte à Internet. Pour le débloquer : supprime la ligne.

## Activer sans le générateur (autre possibilité)

Tu peux aussi simplement ajouter le code du client sur une ligne de `licences.txt`. Dans ce cas l'appli
doit se connecter au moins une fois tous les 7 jours.

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

- L'appli lit la liste à l'ouverture si la dernière lecture date de plus de 3 heures.
- Un téléphone bloqué se verrouille donc au plus tard quelques heures après ta modification,
  à condition qu'il soit connecté à Internet.
- Avec une **clé** : l'appli marche sans Internet, sans limite.
- Sans clé (activé par la liste ou ancien code) : **7 jours** sans Internet, puis une connexion est demandée.
- Une horloge du téléphone remise en arrière est repérée.

## Anciens clients (codes de la version web)

Pendant la transition (`anciens-codes AAAA-MM-JJ`), un ancien client touche
« J'ai déjà un code d'accès » et entre son code : l'appli s'ouvre. Après cette date, il doit
t'envoyer le code de son téléphone. Le code de test `ELI-TEST-2026` est un ancien code : pense à
le bloquer (`bloque-ancien ELI-TEST-2026`) si tu ne veux pas qu'il serve à d'autres.
