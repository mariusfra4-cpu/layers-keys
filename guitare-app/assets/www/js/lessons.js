// Cours de théorie. Balises interactives :
//  <button class="pl" data-n="C4 E4 G4" data-m="chord|arp">  → joue des notes
//  <div class="xc" data-c="Dm7 G7 Cmaj7">                   → diagrammes d'accords (touchables)
//  <div class="xs" data-r="C" data-s="major" data-l="deg">   → gamme sur le manche
//  <div class="xd" data-k="C" data-m="major">                → accords de la tonalité
//  <a class="xp" data-p="251maj">                            → ouvre une progression
'use strict';
const LESSONS = [
  // =====================================================================
  { cat: 'Les bases', id: 'notes', title: 'Les 12 notes et le demi-ton', body: `
<p>La musique occidentale utilise <b>12 notes</b> qui se répètent à chaque octave :</p>
<p class="big">C · C♯/D♭ · D · D♯/E♭ · E · F · F♯/G♭ · G · G♯/A♭ · A · A♯/B♭ · B</p>
<p>En français : <b>Do Ré Mi Fa Sol La Si</b> (C D E F G A B). Tu peux changer la notation dans les Réglages.</p>
<ul>
<li>La distance entre deux notes voisines est un <b>demi-ton</b> = <b>une frette</b> sur la guitare.</li>
<li>Deux demi-tons = un <b>ton</b> = deux frettes.</li>
<li>Il n’y a <b>pas de note</b> entre <b>E–F</b> (Mi–Fa) et <b>B–C</b> (Si–Do) : ce sont des demi-tons naturels.</li>
<li><b>♯ (dièse)</b> monte d’un demi-ton, <b>♭ (bémol)</b> descend d’un demi-ton. C♯ et D♭ sont la même touche : on dit qu’elles sont <i>enharmoniques</i>.</li>
</ul>
<p><button class="pl" data-n="C4 C#4 D4 D#4 E4 F4 F#4 G4 G#4 A4 A#4 B4 C5" data-m="arp">▶ Écouter la gamme chromatique</button></p>
<h4>L’octave</h4>
<p>Après 12 demi-tons on retrouve la même note, deux fois plus aiguë (fréquence ×2). Sur une corde, la <b>12e frette</b> donne l’octave de la corde à vide.</p>
<p><button class="pl" data-n="A2 A3 A4" data-m="arp">▶ La sur trois octaves</button></p>` },

  { cat: 'Les bases', id: 'manche', title: 'Apprendre les notes du manche', body: `
<p>Accordage standard, de la corde grave (6) à l’aiguë (1) : <b>E A D G B E</b> (« <i>Et Avec De Grands Bras Embrasse</i> »).</p>
<div class="xs" data-r="C" data-s="major" data-l="note"></div>
<p>Ci-dessus : toutes les notes naturelles (sans dièses). Méthode rapide :</p>
<ol>
<li>Apprends d’abord les notes des <b>cordes 6 et 5</b> (elles servent de basse à presque tous les accords barrés).</li>
<li>Repères : frettes <b>3, 5, 7, 9, 12</b>. Corde 6 : G à la 3, A à la 5, B à la 7, C♯ à la 9, E à la 12.</li>
<li><b>Formes d’octave</b> : depuis une note sur la corde 6, l’octave est 2 cordes plus haut et 2 frettes plus loin (corde 4). Depuis la corde 5 → corde 3, +2 frettes. Depuis les cordes 4 ou 3 → +3 frettes (à cause de la corde B).</li>
<li>Chaque corde a un « décalage » de 5 frettes avec la suivante (la 5e frette = corde à vide suivante), sauf G→B : 4 frettes.</li>
</ol>
<p>Entraîne-toi dans <b>Exercices → Notes du manche</b> : quelques minutes par jour suffisent.</p>` },

  { cat: 'Les bases', id: 'rythme', title: 'Rythme, mesure et tempo', body: `
<p>Le <b>tempo</b> se mesure en <b>BPM</b> (battements par minute). Une <b>mesure 4/4</b> contient 4 temps ; on compte « 1 2 3 4 ».</p>
<ul>
<li><b>Noire</b> = 1 temps · <b>croches</b> = 2 par temps (« 1 et 2 et ») · <b>double-croches</b> = 4 par temps (« 1 e et a »).</li>
<li><b>Swing</b> (jazz, blues) : les croches sont inégales, « longue-courte », comme un triolet.</li>
<li><b>Syncope</b> : accent sur un contretemps (« et »), très utilisé en funk, R&B, gospel, bossa.</li>
</ul>
<h4>Rythmiques de base à la main droite</h4>
<ul>
<li>Pop/folk : <b>B · B H · H B H</b> (B = bas, H = haut) sur « 1 · 2 et · et 4 et ».</li>
<li>Funk : double-croches continues, la main ne s’arrête jamais ; on « étouffe » les cordes (ghost notes) entre les accents.</li>
<li>Ballade gospel/R&B : arpèges lents en croches, basse sur le 1.</li>
</ul>
<p>L’onglet <b>Outils</b> contient un métronome. Travaille toujours lentement puis accélère de 5 BPM.</p>` },

  // =====================================================================
  { cat: 'Intervalles', id: 'intervalles', title: 'Les intervalles', body: `
<p>Un <b>intervalle</b> est la distance entre deux notes, comptée en demi-tons. C’est la brique de base de toute l’harmonie : accords, gammes et modes ne sont que des <b>listes d’intervalles</b> depuis une fondamentale.</p>
<div class="ivtable"></div>
<h4>Consonance et dissonance</h4>
<p><b>Justes</b> (4, 5, 8) : stables, « creux ». <b>Tierces et sixtes</b> : consonances douces qui donnent la couleur majeure/mineure. <b>Secondes, septièmes, triton</b> : dissonances qui créent de la <b>tension</b> — et la musique avance en alternant tension et résolution.</p>
<h4>Le renversement</h4>
<p>Un intervalle + son renversement = 12 demi-tons : tierce majeure (4) ↔ sixte mineure (8) ; quarte (5) ↔ quinte (7) ; le triton (6) est son propre renversement.</p>` },

  { cat: 'Intervalles', id: 'iv-manche', title: 'Les intervalles sur le manche', body: `
<p>Sur deux cordes voisines (sauf G–B), les formes sont toujours les mêmes. Depuis une note sur la corde 6 ou 5 :</p>
<ul>
<li><b>Même corde</b> : 1 frette = ♭2, 2 = 2, 3 = ♭3, 4 = 3, 5 = 4…</li>
<li><b>Corde suivante, même frette</b> : quarte juste (4).</li>
<li><b>Corde suivante, +2 frettes</b> : quinte juste (5) → c’est le <i>power chord</i>.</li>
<li><b>Corde suivante, −1 frette</b> : tierce majeure (3). <b>−2 frettes</b> : tierce mineure (♭3).</li>
<li><b>Corde suivante, +1 frette</b> : triton (♭5).</li>
<li><b>Deux cordes plus haut, +2 frettes</b> : octave.</li>
<li><b>Deux cordes plus haut, −1 frette</b> : septième majeure (7) ; <b>même frette</b> : septième mineure (♭7).</li>
</ul>
<p>⚠️ Quand tu traverses la corde <b>B</b> (de G vers B), ajoute <b>+1 frette</b> à toutes ces formes.</p>
<p><button class="pl" data-n="E2 B2" data-m="chord">▶ Quinte (power chord)</button> <button class="pl" data-n="A2 C#3" data-m="arp">▶ Tierce majeure</button> <button class="pl" data-n="A2 G3" data-m="arp">▶ Septième mineure</button></p>` },

  // =====================================================================
  { cat: 'Gammes', id: 'majeure', title: 'La gamme majeure', body: `
<p>Formule : <b>T T ½ T T T ½</b> (T = ton, ½ = demi-ton). Intervalles : <b>1 2 3 4 5 6 7</b>.</p>
<p>Do majeur : C D E F G A B — uniquement les notes naturelles.</p>
<div class="xs" data-r="C" data-s="major" data-l="deg"></div>
<p><button class="pl" data-n="C4 D4 E4 F4 G4 A4 B4 C5" data-m="arp">▶ Do majeur</button> <button class="pl" data-n="G3 A3 B3 C4 D4 E4 F#4 G4" data-m="arp">▶ Sol majeur</button></p>
<p>La gamme majeure est la référence : on décrit tout le reste par rapport à elle (« ♭3 » = tierce abaissée par rapport à la tierce de la gamme majeure).</p>
<h4>Les armures</h4>
<p>Chaque tonalité majeure a un nombre fixe d’altérations : G (1♯ : F♯), D (2♯), A (3♯), E (4♯), B (5♯) · F (1♭ : B♭), B♭ (2♭), E♭ (3♭), A♭ (4♭), D♭ (5♭). C’est le <a class="lk" data-l="quintes">cycle des quintes</a>.</p>` },

  { cat: 'Gammes', id: 'mineures', title: 'Les trois gammes mineures', body: `
<h4>Mineure naturelle (éolien)</h4>
<p><b>1 2 ♭3 4 5 ♭6 ♭7</b> — même notes que la gamme majeure située une tierce mineure au-dessus (La mineur = Do majeur : c’est la <b>relative</b>).</p>
<div class="xs" data-r="A" data-s="minor" data-l="deg"></div>
<p><button class="pl" data-n="A3 B3 C4 D4 E4 F4 G4 A4" data-m="arp">▶ La mineur naturel</button></p>
<h4>Mineure harmonique</h4>
<p><b>1 2 ♭3 4 5 ♭6 7</b> — la 7e est remontée pour obtenir un vrai accord de dominante (E7 en La mineur). Couleur « orientale » grâce à l’intervalle de seconde augmentée entre ♭6 et 7.</p>
<p><button class="pl" data-n="A3 B3 C4 D4 E4 F4 G#4 A4" data-m="arp">▶ La mineur harmonique</button></p>
<h4>Mineure mélodique (« jazz »)</h4>
<p><b>1 2 ♭3 4 5 6 7</b> — mineur avec 6 et 7 majeures. En jazz on la joue identique en montant et en descendant. Ses modes donnent les gammes <b>altérée</b> et <b>lydien ♭7</b> (voir <a class="lk" data-l="melmodes">modes de la mineure mélodique</a>).</p>
<p><button class="pl" data-n="A3 B3 C4 D4 E4 F#4 G#4 A4" data-m="arp">▶ La mineur mélodique</button></p>` },

  { cat: 'Gammes', id: 'penta', title: 'Pentatoniques et gamme blues', body: `
<p>5 notes, aucun demi-ton : impossible de faire une « fausse note ». C’est la gamme n°1 du rock, du blues, du gospel et du R&B.</p>
<p><b>Pentatonique mineure</b> : 1 ♭3 4 5 ♭7. <b>Majeure</b> : 1 2 3 5 6 (mêmes notes que la mineure relative).</p>
<div class="xs" data-r="A" data-s="pentam" data-l="deg"></div>
<p><button class="pl" data-n="A3 C4 D4 E4 G4 A4" data-m="arp">▶ La penta mineure</button> <button class="pl" data-n="C4 D4 E4 G4 A4 C5" data-m="arp">▶ Do penta majeure</button></p>
<h4>Les 5 positions</h4>
<p>La gamme se répète sur tout le manche en 5 « boîtes » qui s’emboîtent. La position 1 de La mineur commence à la frette 5 (forme la plus connue). Apprends-les une par une et relie-les en glissant.</p>
<h4>La gamme blues</h4>
<p>Pentatonique mineure + la <b>blue note ♭5</b> : 1 ♭3 4 <b>♭5</b> 5 ♭7. Version majeure (country, gospel) : 1 2 <b>♭3</b> 3 5 6.</p>
<div class="xs" data-r="A" data-s="blues" data-l="deg"></div>
<p><button class="pl" data-n="A3 C4 D4 D#4 E4 G4 A4" data-m="arp">▶ Gamme blues de La</button></p>
<p><b>Astuce gospel/R&B</b> : mélange penta majeure et blues majeure (la ♭3 qui glisse vers la 3) sur les accords majeurs.</p>` },

  { cat: 'Gammes', id: 'symetriques', title: 'Gammes symétriques : diminuée et par tons', body: `
<h4>Diminuée demi-ton / ton</h4>
<p>8 notes : ½ T ½ T ½ T ½ T. Elle se joue sur les accords <b>7♭9, 13♭9, 7♯9</b>. Elle se répète toutes les 3 frettes : C, E♭, F♯, A ont la même gamme.</p>
<div class="xs" data-r="G" data-s="hw" data-l="deg"></div>
<p><button class="pl" data-n="G3 G#3 A#3 B3 C#4 D4 E4 F4 G4" data-m="arp">▶ Sol diminuée ½-T</button></p>
<h4>Diminuée ton / demi-ton</h4>
<p>Le contraire : T ½ T ½… elle va sur l’accord <b>dim7</b>.</p>
<h4>Par tons</h4>
<p>6 notes séparées d’un ton : 1 2 3 ♯4 ♯5 ♭7. Sur les accords <b>7♯5 / aug</b>. Son « rêve », impressionniste (Debussy), très jazz.</p>
<p><button class="pl" data-n="C4 D4 E4 F#4 G#4 A#4 C5" data-m="arp">▶ Gamme par tons</button></p>` },

  // =====================================================================
  { cat: 'Accords', id: 'triades', title: 'Les triades', body: `
<p>Un accord = au moins trois notes jouées ensemble. La <b>triade</b> superpose deux tierces sur une fondamentale.</p>
<table class="t"><tr><th>Type</th><th>Formule</th><th>Exemple (C)</th><th>Couleur</th></tr>
<tr><td>Majeur</td><td>1 3 5</td><td>C E G</td><td>joyeux, stable</td></tr>
<tr><td>Mineur</td><td>1 ♭3 5</td><td>C E♭ G</td><td>triste, doux</td></tr>
<tr><td>Diminué</td><td>1 ♭3 ♭5</td><td>C E♭ G♭</td><td>tendu, instable</td></tr>
<tr><td>Augmenté</td><td>1 3 ♯5</td><td>C E G♯</td><td>étrange, suspendu</td></tr></table>
<div class="xc" data-c="C Cm Cdim Caug"></div>
<p>Les accords <b>sus</b> remplacent la tierce : <b>sus2</b> (1 2 5) et <b>sus4</b> (1 4 5). Ni majeurs ni mineurs, ils « attendent » une résolution.</p>
<div class="xc" data-c="Dsus2 D Dsus4 D"></div>
<h4>Renversements</h4>
<p>On peut mettre une autre note que la fondamentale à la basse : <b>C/E</b> (1er renversement, tierce à la basse), <b>C/G</b> (2e renversement, quinte à la basse). Très utile pour faire des <b>basses qui marchent</b> (C – C/B – Am – Am/G – F…).</p>
<div class="xc" data-c="C C/E C/G"></div>` },

  { cat: 'Accords', id: 'septiemes', title: 'Les accords de septième', body: `
<p>On ajoute une troisième tierce : la <b>7e</b>. Il y a 5 accords de 7e essentiels :</p>
<table class="t"><tr><th>Nom</th><th>Formule</th><th>En C</th><th>Rôle</th></tr>
<tr><td><b>maj7</b> (Δ)</td><td>1 3 5 7</td><td>C E G B</td><td>tonique jazz, I et IV</td></tr>
<tr><td><b>7</b> (dominante)</td><td>1 3 5 ♭7</td><td>C E G B♭</td><td>V, tension qui veut résoudre ; tout le blues</td></tr>
<tr><td><b>m7</b></td><td>1 ♭3 5 ♭7</td><td>C E♭ G B♭</td><td>ii, iii, vi ; funk, R&B</td></tr>
<tr><td><b>m7♭5</b> (ø)</td><td>1 ♭3 ♭5 ♭7</td><td>C E♭ G♭ B♭</td><td>ii en mineur, vii en majeur</td></tr>
<tr><td><b>dim7</b> (°7)</td><td>1 ♭3 ♭5 ♭♭7</td><td>C E♭ G♭ A</td><td>accord de passage (gospel !)</td></tr></table>
<div class="xc" data-c="Cmaj7 C7 Cm7 Cm7b5 Cdim7"></div>
<p>Moins courants : <b>m(maj7)</b> (1 ♭3 5 7, son « film noir »), <b>6</b> et <b>m6</b> (sixte à la place de la septième, swing et manouche), <b>7sus4</b>.</p>
<h4>Le triton de la dominante</h4>
<p>Dans G7, la 3 (B) et la ♭7 (F) forment un <b>triton</b>. B veut monter vers C, F veut descendre vers E : c’est ce qui « tire » G7 vers C. Retiens-le, c’est la clé du jazz et du gospel.</p>
<p><button class="pl" data-n="G2 B3 D4 F4" data-m="chord">▶ G7</button> <button class="pl" data-n="C3 C4 E4 G4" data-m="chord">▶ → C</button></p>` },

  { cat: 'Accords', id: 'extensions', title: 'Extensions : 9, 11, 13', body: `
<p>En continuant d’empiler des tierces au-delà de la 7e on obtient les <b>extensions</b> : <b>9</b> (= 2), <b>11</b> (= 4), <b>13</b> (= 6), une octave plus haut.</p>
<table class="t"><tr><th>Accord</th><th>Formule</th><th>Style</th></tr>
<tr><td>maj9</td><td>1 3 5 7 9</td><td>R&B, neo-soul, ballades</td></tr>
<tr><td>9</td><td>1 3 5 ♭7 9</td><td>funk, blues, gospel</td></tr>
<tr><td>m9</td><td>1 ♭3 5 ♭7 9</td><td>neo-soul, R&B (le son « smooth »)</td></tr>
<tr><td>m11</td><td>1 ♭3 (5) ♭7 9 11</td><td>neo-soul, jazz modal</td></tr>
<tr><td>13</td><td>1 3 (5) ♭7 (9) 13</td><td>jazz, gospel, big band</td></tr>
<tr><td>6/9</td><td>1 3 5 6 9</td><td>accord final jazz/gospel/bossa</td></tr>
<tr><td>add9</td><td>1 3 5 9 (sans 7e)</td><td>pop, worship</td></tr></table>
<div class="xc" data-c="Cmaj9 C9 Cm9 Cm11 C13 C6/9"></div>
<h4>Que retirer ?</h4>
<p>La guitare n’a que 6 cordes (et 4 doigts) : on <b>supprime la quinte</b> en premier (elle n’apporte pas de couleur), puis la fondamentale si un bassiste la joue. Les notes <b>indispensables</b> sont la <b>3e et la 7e</b> (elles disent si l’accord est majeur, mineur, dominant), puis l’extension qui donne la couleur.</p>
<p>⚠️ Sur un accord majeur on n’utilise pas la 11 juste (elle frotte contre la 3) : on la hausse en <b>♯11</b> (son lydien).</p>` },

  { cat: 'Accords', id: 'alterations', title: 'Accords altérés (♭9, ♯9, ♯5, ♯11, alt)', body: `
<p>Sur un accord de <b>dominante</b>, on peut altérer la 5 et la 9 pour plus de tension avant la résolution :</p>
<ul>
<li><b>7♭9</b> : très sombre, idéal pour résoudre vers un accord <b>mineur</b> (V7♭9 → i).</li>
<li><b>7♯9</b> : l’accord « Hendrix » (tierce majeure ET mineure), funk et rock, gospel.</li>
<li><b>7♯5</b> (7+) : la quinte monte vers la tierce de l’accord suivant.</li>
<li><b>7♯11</b> / <b>13♯11</b> : couleur lydien ♭7, brillante, sur les II7 et ♭VII7.</li>
<li><b>7alt</b> : ♭9, ♯9, ♭5 et/ou ♯5 à la fois. Gamme : <b>altérée</b>.</li>
<li><b>13♭9</b> : la 13 en haut et la ♭9 au milieu, accord gospel par excellence.</li>
</ul>
<div class="xc" data-c="G7b9 G7#9 G7#5 G13 G13b9 G7alt"></div>
<p><button class="pl" data-n="G2 F3 B3 D#4 A#4" data-m="chord">▶ G7♯5♯9</button> <button class="pl" data-n="C3 G3 B3 D4 E4" data-m="chord">▶ → Cmaj9</button></p>` },

  { cat: 'Accords', id: 'voicings', title: 'Voicings : shell, drop 2, drop 3', body: `
<p>Le <b>voicing</b>, c’est la façon de disposer les notes d’un accord. Le même Cmaj7 peut sonner épais, aérien, sombre…</p>
<h4>Shell voicings (1 – 3 – 7)</h4>
<p>Trois notes seulement : fondamentale à la basse + 3e + 7e. Le son des guitaristes de big band, parfait pour comprendre le jazz. Fondamentale sur la corde 6 : 1-7-3 ; sur la corde 5 : 1-3-7.</p>
<div class="xc" data-c="Dm7 G7 Cmaj7"></div>
<h4>Drop 2</h4>
<p>On prend un accord serré (4 notes dans l’octave) et on <b>descend la 2e note la plus aiguë d’une octave</b>. Résultat : un accord jouable sur 4 cordes voisines (1-2-3-4 ou 2-3-4-5), très utilisé en jazz.</p>
<h4>Drop 3</h4>
<p>On descend la 3e note la plus aiguë : la basse sur la corde 6 ou 5, une corde sautée. Son ample, idéal en accompagnement solo.</p>
<h4>Voicings neo-soul</h4>
<p>Superposer une triade sur une basse : <b>Em/C = Cmaj7</b>, <b>Em7/C = Cmaj9</b>, <b>F/G = G9sus4</b>, <b>Bb/C = C9sus4</b>. C’est la façon la plus simple de jouer des accords riches sur les 3 cordes aiguës.</p>
<div class="xc" data-c="Cmaj9 G9sus4 Am11"></div>` },

  { cat: 'Accords', id: 'caged', title: 'Le système CAGED', body: `
<p>Les 5 accords ouverts <b>C, A, G, E, D</b> sont 5 « formes » que l’on peut déplacer sur tout le manche avec un barré. Elles s’emboîtent dans cet ordre (C → A → G → E → D → C…) en montant.</p>
<div class="xc" data-c="C A G E D"></div>
<p>Exemple pour <b>Do majeur</b> : forme C (ouverte), forme A (barré frette 3), forme G (frette 5), forme E (barré frette 8), forme D (frette 10).</p>
<p>Pourquoi c’est utile : chaque forme contient une position de la gamme majeure et de la pentatonique autour de l’accord. Tu peux donc <b>improviser en voyant l’accord</b> sous tes doigts, et trouver un même accord à 5 endroits.</p>` },

  { cat: 'Accords', id: 'slash', title: 'Accords slash (basse différente)', body: `
<p><b>X/Y</b> = accord X avec la note Y à la basse. Deux cas :</p>
<ul>
<li><b>Y fait partie de l’accord</b> → c’est un renversement : C/E, G/B, D/F♯ (basses qui marchent en pop, folk, worship).</li>
<li><b>Y ne fait pas partie de l’accord</b> → on crée un nouvel accord : F/G = G9sus4, D/C (couleur lydienne de Do : 9, ♯11, 13), Ab/Bb = B♭9sus4, E/F (très tendu).</li>
</ul>
<div class="xc" data-c="G/B D/F# F/G C/E"></div>
<p>En <b>gospel</b>, le IV/V (F/G en Do) est partout ; le I/3 et le I/5 servent à faire monter la basse (voir la <a class="xp" data-p="gwalk">montée gospel</a>).</p>` },

  // =====================================================================
  { cat: 'Harmonie', id: 'diatonique', title: 'Les accords d’une tonalité', body: `
<p>En empilant des tierces sur chaque note de la gamme majeure, on obtient les <b>7 accords diatoniques</b>. Leur type est toujours le même, dans toutes les tonalités :</p>
<p class="big">Imaj7 · ii m7 · iii m7 · IVmaj7 · V7 · vi m7 · viiø7</p>
<div class="xd" data-k="C" data-m="major"></div>
<p>Change la tonalité pour voir les accords dans toutes les tonalités. Les chiffres romains (majuscule = majeur, minuscule = mineur) permettent de parler d’une progression <b>sans tonalité</b> : « un ii–V–I » se joue partout.</p>
<h4>Les 3 fonctions</h4>
<ul>
<li><b>Tonique</b> (repos) : I, iii, vi.</li>
<li><b>Sous-dominante</b> (départ) : IV, ii.</li>
<li><b>Dominante</b> (tension) : V, vii°.</li>
</ul>
<p>La musique tonale circule : Tonique → Sous-dominante → Dominante → Tonique. Tu peux remplacer un accord par un autre de la même fonction (vi pour I, ii pour IV…).</p>
<h4>En mineur</h4>
<p class="big">i m7 · iiø7 · ♭IIImaj7 · iv m7 · v m7 (ou V7) · ♭VImaj7 · ♭VII7</p>
<div class="xd" data-k="A" data-m="minor"></div>` },

  { cat: 'Harmonie', id: 'cadences', title: 'Cadences', body: `
<p>Une <b>cadence</b> est une formule de fin de phrase.</p>
<ul>
<li><b>Parfaite</b> V → I : la résolution la plus forte. <span class="xc inline" data-c="G7 C"></span></li>
<li><b>Plagale</b> IV → I : l’« Amen » de l’église, très gospel. Version mineure : IV → iv → I.</li>
<li><b>Demi-cadence</b> : on s’arrête sur V (suspens, question).</li>
<li><b>Rompue</b> V → vi : on attend I et on va sur vi (surprise douce).</li>
<li><b>ii–V–I</b> : la cadence parfaite précédée de sa sous-dominante, base du jazz.</li>
</ul>
<p>Écoute-les dans l’onglet <b>Progressions</b> : <a class="xp" data-p="gamen">Amen</a>, <a class="xp" data-p="251maj">ii–V–I</a>.</p>` },

  { cat: 'Harmonie', id: 'quintes', title: 'Le cycle des quintes', body: `
<p>En montant de quinte en quinte : <b>C G D A E B F♯ C♯…</b>, chaque tonalité ajoute un ♯. En descendant (quartes) : <b>C F B♭ E♭ A♭ D♭ G♭</b>, chaque tonalité ajoute un ♭.</p>
<div class="circle"></div>
<p>Pourquoi c’est essentiel :</p>
<ul>
<li>Les tonalités voisines partagent 6 notes sur 7 : modulations faciles.</li>
<li>Les accords qui se suivent en <b>descendant par quintes</b> (sens inverse des aiguilles) forment l’enchaînement le plus naturel : iii – vi – ii – V – I. Tout le jazz et le gospel en sont remplis (<a class="xp" data-p="g736">7-3-6-2-5-1</a>).</li>
<li>La relative mineure est à l’intérieur du cercle (Am pour C, Em pour G…).</li>
</ul>` },

  { cat: 'Harmonie', id: 'secondaires', title: 'Dominantes secondaires', body: `
<p>Chaque accord de la tonalité (sauf vii°) peut être précédé de <b>sa propre dominante</b>, notée <b>V7/x</b> (« cinq de x »).</p>
<table class="t"><tr><th>En Do</th><th>Dominante secondaire</th><th>Vers</th></tr>
<tr><td>V7/ii</td><td>A7</td><td>Dm</td></tr>
<tr><td>V7/iii</td><td>B7</td><td>Em</td></tr>
<tr><td>V7/IV</td><td>C7</td><td>F</td></tr>
<tr><td>V7/V</td><td>D7</td><td>G</td></tr>
<tr><td>V7/vi</td><td>E7</td><td>Am</td></tr></table>
<p>Résultat : des notes étrangères (C♯, D♯, F♯, G♯, B♭) qui apportent du mouvement. Elles sont partout en jazz, en gospel et en R&B (<a class="xp" data-p="jt2u">Imaj7 – VII7 – iii7</a>).</p>
<div class="xc" data-c="C A7 Dm7 G7"></div>
<p><b>Astuce</b> : quand l’appli détecte un accord de 7 qui n’est pas le V, regarde quel accord vient juste après : il est souvent une quinte plus bas.</p>` },

  { cat: 'Harmonie', id: 'subtritone', title: 'Substitution tritonique', body: `
<p>On peut remplacer un accord de dominante par la dominante située à un <b>triton</b> (6 demi-tons) : <b>G7 ↔ D♭7</b>.</p>
<p>Pourquoi ça marche : G7 = G <b>B</b> D <b>F</b> ; D♭7 = D♭ <b>F</b> A♭ <b>C♭(=B)</b>. Les deux accords partagent le même triton (B–F) qui crée la tension !</p>
<p>Effet : la basse descend <b>chromatiquement</b> : Dm7 – D♭7 – Cmaj7. Son très jazz et très gospel.</p>
<div class="xc" data-c="Dm7 Db7 Cmaj7"></div>
<p><a class="xp" data-p="tritone">Écouter la progression</a></p>` },

  { cat: 'Harmonie', id: 'emprunt', title: 'Emprunt modal et accords « couleur »', body: `
<p>On emprunte des accords à la tonalité <b>mineure homonyme</b> (même tonique) pour colorer une tonalité majeure :</p>
<ul>
<li><b>iv</b> (Fm en Do) : nostalgie, cadence plagale mineure.</li>
<li><b>♭VII</b> (B♭) : rock, soul, backdoor (B♭7 → C).</li>
<li><b>♭VI</b> (A♭maj7) : épique, cinématique (♭VI – ♭VII – I).</li>
<li><b>♭III</b> (E♭) : rock, funk.</li>
<li><b>ii ø7</b> (Dm7♭5) : couleur sombre avant V.</li>
</ul>
<div class="xc" data-c="C Abmaj7 Bb C"></div>
<p>Quand l’appli affiche des chiffres comme <b>♭VI</b> ou <b>♭VII</b>, c’est presque toujours un emprunt.</p>` },

  // =====================================================================
  { cat: 'Modes', id: 'modes', title: 'Les 7 modes de la gamme majeure', body: `
<p>Un <b>mode</b>, c’est la gamme majeure jouée en partant d’une autre note. Les notes sont les mêmes, mais le <b>centre</b> change, donc la couleur change.</p>
<p>En Do majeur : partir de D donne <b>D dorien</b>, de E → <b>E phrygien</b>, de F → <b>F lydien</b>, de G → <b>G mixolydien</b>, de A → <b>A éolien</b>, de B → <b>B locrien</b>.</p>
<div class="modetable"></div>
<h4>Comparer les modes sur la même tonique</h4>
<p>La meilleure façon d’entendre un mode est de le jouer sur la <b>même fondamentale</b> (tout en C) et de repérer sa <b>note caractéristique</b> :</p>
<p>Du plus lumineux au plus sombre : <b>Lydien (♯4)</b> → Ionien → <b>Mixolydien (♭7)</b> → <b>Dorien (♭3, 6)</b> → Éolien (♭6) → <b>Phrygien (♭2)</b> → <b>Locrien (♭5)</b>. Chaque mode n’a qu’une note de différence avec son voisin !</p>
<div class="modeplay"></div>` },

  { cat: 'Modes', id: 'modes-usage', title: 'Quel mode sur quel accord ?', body: `
<table class="t"><tr><th>Accord</th><th>Mode(s)</th><th>Note à faire sonner</th></tr>
<tr><td>maj7 (I)</td><td>Ionien</td><td>7, 9</td></tr>
<tr><td>maj7 (IV), maj7♯11</td><td>Lydien</td><td>♯11</td></tr>
<tr><td>m7 (ii), m9, m11</td><td>Dorien</td><td>6 majeure</td></tr>
<tr><td>m7 (vi)</td><td>Éolien</td><td>♭6</td></tr>
<tr><td>m7 (iii)</td><td>Phrygien</td><td>♭2</td></tr>
<tr><td>7, 9, 13</td><td>Mixolydien</td><td>3 et ♭7</td></tr>
<tr><td>7♯11</td><td>Lydien ♭7</td><td>♯11</td></tr>
<tr><td>7♭9 (vers mineur)</td><td>Phrygien dominant / diminuée</td><td>♭9</td></tr>
<tr><td>7alt</td><td>Altérée</td><td>♭9 ♯9 ♯5</td></tr>
<tr><td>m7♭5</td><td>Locrien (ou locrien ♮2)</td><td>♭5</td></tr>
<tr><td>dim7</td><td>Diminuée ton/½</td><td>—</td></tr></table>
<p><b>Méthode simple</b> : dans une progression tonale (ii–V–I), une seule gamme majeure suffit pour tout. Les modes deviennent importants quand un accord <b>dure longtemps</b> (jazz modal, funk, vamps) ou quand un accord est <b>hors tonalité</b>.</p>
<p>Exemple : un vamp <b>Dm7 – G7</b> qui tourne = D dorien (funk, Santana). <a class="xp" data-p="dorvamp">Écouter</a></p>` },

  { cat: 'Modes', id: 'melmodes', title: 'Modes de la mineure mélodique', body: `
<p>La mineure mélodique (1 2 ♭3 4 5 6 7) produit aussi 7 modes ; 4 d’entre eux sont fondamentaux en jazz :</p>
<table class="t"><tr><th>Degré</th><th>Mode</th><th>Formule</th><th>Accord</th></tr>
<tr><td>I</td><td>Mineur mélodique</td><td>1 2 ♭3 4 5 6 7</td><td>m(maj7), m6</td></tr>
<tr><td>IV</td><td><b>Lydien ♭7</b> (« Bartók »)</td><td>1 2 3 ♯4 5 6 ♭7</td><td>7♯11, 13♯11</td></tr>
<tr><td>VI</td><td>Locrien ♮2</td><td>1 2 ♭3 4 ♭5 ♭6 ♭7</td><td>m7♭5</td></tr>
<tr><td>VII</td><td><b>Altéré</b> (super-locrien)</td><td>1 ♭9 ♯9 3 ♭5 ♯5 ♭7</td><td>7alt</td></tr></table>
<p><b>Astuce</b> : pour jouer G altéré, joue la mineure mélodique d’<b>A♭</b> (un demi-ton au-dessus). Pour G lydien ♭7, joue la mineure mélodique de <b>D</b> (une quinte au-dessus).</p>
<div class="xs" data-r="G" data-s="altered" data-l="deg"></div>` },

  // =====================================================================
  { cat: 'Styles', id: 'jazz', title: 'Jazz : l’essentiel', body: `
<ul>
<li><b>Tout est en 7e minimum</b> : maj7, m7, 7, m7♭5 ; les extensions (9, 13) et altérations sur les dominantes.</li>
<li><b>Le ii–V–I</b> est partout. Apprends-le dans toutes les tonalités, en majeur et en mineur.</li>
<li><b>Guide tones</b> : les 3e et 7e. D’un accord à l’autre dans un ii–V–I, elles bougent d’un demi-ton ou restent : Dm7 (F, C) → G7 (F, B) → Cmaj7 (E, B).</li>
<li><b>Substitutions</b> : tritonique, dominantes secondaires, accords diminués de passage.</li>
<li>Rythme : <b>swing</b>, accords sur 2 et 4 (« Freddie Green » : un accord par temps en shell, très court).</li>
</ul>
<div class="xc" data-c="Dm9 G13 Cmaj9 A7b9"></div>
<p>Progressions : <a class="xp" data-p="251maj">ii–V–I</a> · <a class="xp" data-p="251min">ii–V–i</a> · <a class="xp" data-p="turn1625">turnaround</a> · <a class="xp" data-p="rhythm">rhythm changes</a> · <a class="xp" data-p="jazzblues">blues jazz</a>.</p>` },

  { cat: 'Styles', id: 'gospel', title: 'Gospel : passages, tensions, réharmonisation', body: `
<p>Le gospel, c’est l’art de <b>remplir l’espace entre deux accords</b> pour créer de l’émotion.</p>
<h4>1. Les accords diminués de passage</h4>
<p>Entre deux accords dont les basses sont à un ton, on glisse un <b>dim7</b> sur la note chromatique : C – <b>C♯°7</b> – Dm7, F – <b>F♯°7</b> – C/G. (Le dim7 = un 7♭9 sans fondamentale de l’accord qui suit.)</p>
<div class="xc" data-c="C C#dim7 Dm7"></div>
<h4>2. Le IV/V (9sus4)</h4>
<p>F/G en Do : la dominante douce, avant ou à la place du G7. Souvent suivi de G7♭9 ou G13♭9 pour la tension finale.</p>
<h4>3. Les 7♯9 et 7♯5 sur les accords « de passage »</h4>
<p>Transformer les accords mineurs en dominantes pour enchaîner sur le cycle des quintes : Em7 → <b>E7♯9</b> → Am9 ; Am → <b>A7♯5</b> → Dm9.</p>
<h4>4. La cadence plagale et le iv mineur</h4>
<p>F – Fm6 – C : « Amen ». Les « church endings » utilisent aussi ♭VI – ♭VII – I.</p>
<h4>5. Le chromatisme</h4>
<p>Glisser d’un demi-ton au-dessus ou en dessous vers l’accord cible (C♯m7 → Cm7 ; B13 → C9…), basse qui marche chromatiquement, substitution tritonique.</p>
<p>Progressions : <a class="xp" data-p="gwalk">montée gospel</a> · <a class="xp" data-p="g251">2-5-1 gospel</a> · <a class="xp" data-p="g736">7-3-6-2-5-1</a> · <a class="xp" data-p="g45">IV/V</a> · <a class="xp" data-p="gamen">Amen</a> · <a class="xp" data-p="g1436">1-4 gospel</a>.</p>` },

  { cat: 'Styles', id: 'rnb', title: 'R&B et neo-soul', body: `
<ul>
<li><b>Accords à 4-5 sons partout</b> : m9, maj9, m11, 13, 9sus4. On ne joue presque jamais de triade nue.</li>
<li><b>Peu d’accords, beaucoup de couleur</b> : souvent 2 à 4 accords qui tournent, mais avec des voicings soignés et des voix qui bougent à l’intérieur.</li>
<li><b>Mouvements par degrés</b> : ii – iii – IV (Dm9 – Em7 – Fmaj9), et accords diatoniques successifs.</li>
<li><b>Chromatic slides</b> : on attaque un accord depuis une frette au-dessus ou en dessous (glissé).</li>
<li><b>Hammer-on / pull-off</b> dans l’accord (la 9 qui va à la 3, la 11 qui va à la ♭3) et petites phrases en penta entre les accords (style Hendrix « Little Wing », Curtis Mayfield).</li>
<li><b>Triades et accords sur basse</b> : Em/C (= Cmaj7), Em7/C (= Cmaj9), Am/F (= Fmaj7), F/G (= G9sus4).</li>
</ul>
<div class="xc" data-c="Dm9 Em7 Fmaj9 Fmaj7#11 Am11 G13"></div>
<p>Progressions : <a class="xp" data-p="jt2u">Imaj7–VII7–iii7</a> · <a class="xp" data-p="ns23">ii–iii–IV</a> · <a class="xp" data-p="rb6251">6-2-5-1 R&B</a> · <a class="xp" data-p="nsminor">neo-soul mineur</a> · <a class="xp" data-p="lydianrb">lydien</a>.</p>` },

  { cat: 'Styles', id: 'blues', title: 'Blues', body: `
<p>Le blues repose sur la <b>grille de 12 mesures</b> et l’accord de <b>7 de dominante</b> sur tous les degrés (I7, IV7, V7) — ce qui est « faux » en théorie classique et fait tout son charme.</p>
<p>Grille : I7 | IV7 | I7 | I7 | IV7 | IV7 | I7 | I7 | V7 | IV7 | I7 | V7</p>
<ul>
<li>Improvisation : <b>pentatonique mineure + ♭5</b>, en visant la tierce majeure de I7 (le frottement ♭3 → 3 est la base du blues).</li>
<li>Rythme <b>shuffle</b> (swing), riff de boogie (5-6 sur la quinte).</li>
<li>Versions enrichies : 9 et 13 au lieu de 7, <a class="xp" data-p="jazzblues">blues jazz</a>, <a class="xp" data-p="minorblues">blues mineur</a>.</li>
</ul>
<div class="xc" data-c="A7 D9 E9 E7#9"></div>` },

  { cat: 'Styles', id: 'funk', title: 'Funk', body: `
<ul>
<li>Accords courts sur les <b>cordes aiguës</b> : 9, 7♯9, m7, m9, 13 (souvent 3 ou 4 cordes).</li>
<li>Main droite en <b>double-croches continues</b>, étouffements de la main gauche, accents sur les contretemps.</li>
<li>Un ou deux accords en vamp (dorien : i7 – IV9 ; mixolydien : I9).</li>
<li>Petits glissés d’un demi-ton pour attaquer l’accord.</li>
</ul>
<div class="xc" data-c="E9 E7#9 Am7 D9"></div>
<p><a class="xp" data-p="dorvamp">Vamp dorien</a> · <a class="xp" data-p="funk9">I9 – IV9</a></p>` },

  // =====================================================================
  { cat: 'Oreille', id: 'oreille', title: 'Trouver les accords d’une chanson', body: `
<p>L’onglet <b>Détecter</b> le fait automatiquement, mais voici la méthode pour le faire à l’oreille (et vérifier l’appli !) :</p>
<ol>
<li><b>Trouve la tonique</b> : la note sur laquelle la chanson « se repose ». Chante-la, puis trouve-la sur la corde 6 ou 5.</li>
<li><b>Majeur ou mineur ?</b> Joue l’accord majeur puis mineur sur cette tonique : lequel colle ?</li>
<li><b>Écris les accords de la tonalité</b> (onglet Théorie → Les accords d’une tonalité). 90 % des chansons pop n’utilisent que I, IV, V, vi (et ii, iii).</li>
<li><b>Suis la basse</b> : la note grave donne presque toujours la fondamentale de l’accord.</li>
<li><b>Ajoute la couleur</b> : écoute si c’est 7, maj7, m7, 9… (entraîne-toi dans Exercices → Qualité d’accord).</li>
<li>Repère les <b>formules</b> : ii–V–I, cadences, dominantes secondaires, passages diminués.</li>
</ol>
<h4>Conseils pour la détection automatique</h4>
<ul>
<li>Micro : place le téléphone près de la source (haut-parleur ou guitare), évite le bruit.</li>
<li>Le <b>niveau d’accords</b> « Simple » donne des résultats plus stables pour la pop ; « Jazz / Gospel / R&B » reconnaît les 9, 11, 13 et altérés.</li>
<li>Pour une chanson entière, utilise <b>Fichier / Vidéo</b> : l’analyse est plus précise (accordage, tempo, tonalité et lissage).</li>
<li>Les accords proposés en « alternatives » sont souvent des versions plus riches ou plus simples du même accord (C ↔ Cmaj7 ↔ Am7/C).</li>
</ul>` },

  // =====================================================================
  { cat: 'Techniques de guitaristes', id: 'tq-triades', title: 'Les triades sur tout le manche', body: `
<p>Les grands guitaristes de gospel, de R&B et de funk jouent surtout des <b>triades sur 3 cordes voisines</b>, pas des accords barrés de 6 cordes. C’est plus léger, plus précis, et ça laisse la place à la basse et au clavier.</p>
<ul>
<li>Chaque triade existe en <b>3 renversements</b> (fondamentale, tierce ou quinte à la basse) sur <b>4 groupes de cordes</b> : 3-2-1, 4-3-2, 5-4-3, 6-5-4. Soit 12 positions par accord.</li>
<li>Pour changer d’accord, choisis le renversement <b>le plus proche</b> : les doigts bougent d’une ou deux cases seulement (« voice leading »).</li>
<li>Commence par les cordes <b>3-2-1</b> : c’est là que vivent le gospel et le R&B.</li>
</ul>
<div class="xv" data-c="C" data-f="triads"></div>
<p><b>Exercice</b> : joue C – F – G – C uniquement sur les cordes 3-2-1, en restant entre les cases 5 et 10.</p>` },

  { cat: 'Techniques de guitaristes', id: 'tq-couleur', title: 'Triades « couleur » : jouer des accords riches facilement', body: `
<p>Astuce des pianistes reprise par les guitaristes neo-soul : un accord enrichi contient <b>des triades cachées</b>. On joue la triade sur les cordes aiguës pendant que la basse (ou le bassiste) joue la fondamentale.</p>
<table class="t"><tr><th>Tu joues…</th><th>sur la basse…</th><th>ça donne</th></tr>
<tr><td>Em</td><td>C</td><td>Cmaj7</td></tr>
<tr><td>G</td><td>C</td><td>Cmaj9 (sans tierce)</td></tr>
<tr><td>F</td><td>D</td><td>Dm7</td></tr>
<tr><td>Am</td><td>D</td><td>D9sus4 (« D11 »)</td></tr>
<tr><td>F</td><td>G</td><td>G9sus4 (le IV/V gospel)</td></tr>
<tr><td>A</td><td>G</td><td>G13♯11 (lydien)</td></tr></table>
<div class="xv" data-c="Dm9" data-f="color"></div>
<p>Dans l’onglet <b>Accords</b>, choisis n’importe quel accord puis « Triades couleur » : l’appli te les trouve toutes.</p>` },

  { cat: 'Techniques de guitaristes', id: 'tq-jazz', title: 'Comping jazz : shells, guide tones, drop 2', body: `
<h4>1. Shells</h4>
<p>Fondamentale + 3 + 7 : trois doigts, le son de l’orchestre de swing. Sur un ii–V–I, les formes alternent entre la corde 6 et la corde 5, la main bouge à peine.</p>
<div class="xv" data-c="G7" data-f="shell"></div>
<h4>2. Guide tones</h4>
<p>Joue seulement la 3 et la 7 de chaque accord, sur deux cordes voisines : Dm7 (F-C) → G7 (F-B) → Cmaj7 (E-B). Une note reste, l’autre descend d’un demi-ton. Ces deux voix « racontent » toute l’harmonie.</p>
<p><button class="pl" data-n="F3 C4" data-m="chord">▶ Dm7</button> <button class="pl" data-n="F3 B3" data-m="chord">▶ G7</button> <button class="pl" data-n="E3 B3" data-m="chord">▶ Cmaj7</button></p>
<h4>3. Drop 2 sur les 4 cordes aiguës</h4>
<p>Le voicing jazz par excellence. Apprends les 4 renversements de Cmaj7, C7, Cm7, Cm7♭5 sur les cordes 4-3-2-1 : tu peux ensuite harmoniser n’importe quelle mélodie.</p>
<div class="xv" data-c="Cmaj7" data-f="drop2"></div>
<h4>4. Rythmes de comping</h4>
<ul><li><b>Freddie Green</b> : un accord court sur chaque temps, accent léger sur 2 et 4.</li>
<li><b>Charleston</b> : sur 1, puis sur le « et » de 2.</li>
<li><b>Anticipation</b> : l’accord suivant arrive une croche en avance (sur le « et » de 4).</li></ul>` },

  { cat: 'Techniques de guitaristes', id: 'tq-rootless', title: 'Voicings sans fondamentale et structures supérieures', body: `
<p>En groupe, le bassiste joue déjà la fondamentale. Les guitaristes jazz et gospel la <b>suppriment</b> pour faire de la place aux tensions (9, 11, 13).</p>
<h4>Formes A et B (« Bill Evans »)</h4>
<p>Forme A : 3 – 5 – 7 – 9 ; forme B : 7 – 9 – 3 – 5. Sur un ii–V–I, on alterne A – B – A : les doigts bougent à peine.</p>
<div class="xv" data-c="Dm9" data-f="rootless"></div>
<h4>Structures supérieures (gospel moderne)</h4>
<p>Sur un accord de dominante : le <b>triton</b> (3 et ♭7) en bas, et une <b>triade majeure</b> au-dessus :</p>
<ul><li>triade sur le <b>II</b> (D sur C7) → C13♯11, brillant ;</li>
<li>triade sur le <b>VI</b> (A sur C7) → C13♭9, LE son gospel ;</li>
<li>triade sur le <b>♭VI</b> (A♭ sur C7) → C7♯5♯9, très tendu ;</li>
<li>triade sur le <b>♭III</b> (E♭ sur C7) → C7♯9.</li></ul>
<div class="xv" data-c="C7" data-f="ust"></div>` },

  { cat: 'Techniques de guitaristes', id: 'tq-passages', title: 'Jouer les accords de passage à la guitare', body: `
<p>Les accords de passage sont faciles à la guitare car <b>les formes se déplacent telles quelles</b> sur le manche.</p>
<ol>
<li><b>Glissé d’une case</b> : joue l’accord d’arrivée une case au-dessus ou au-dessous, puis glisse. Ex. : D♭9 → C9, B13 → C13.</li>
<li><b>Diminué de passage</b> : la forme dim7 se répète toutes les 3 cases (C°7 = E♭°7 = F♯°7 = A°7). Une seule forme sert partout ! Place-la un demi-ton sous l’accord d’arrivée.</li>
<li><b>Dominante avant chaque accord</b> : le V7 d’un accord est sur la même case, une corde plus grave (forme fondamentale corde 5 → corde 6).</li>
<li><b>Substitution tritonique</b> : le ♭II7 partage la tierce et la septième du V7. Garde ces deux notes et change seulement la basse.</li>
<li><b>Basse qui marche</b> : garde la triade sur les cordes aiguës et fais bouger la basse avec le pouce (C – C/B – Am – Am/G…).</li>
</ol>
<div class="xc" data-c="C C#dim7 Dm7 G7 Db7 C"></div>
<p>L’onglet <b>Progressions → Accords de passage</b> génère des milliers d’enchaînements pour arriver sur n’importe quel accord.</p>` },

  { cat: 'Techniques de guitaristes', id: 'tq-gospel', title: 'Guitare gospel et praise', body: `
<ul>
<li><b>Triades sur les cordes aiguës</b>, ou accords de 3-4 notes ; on laisse le grave au bassiste et au clavier.</li>
<li><b>Hammer-on de la sus4 vers la 3</b> (ou de la 2 vers la 3) : on pose l’accord sus et on « frappe » la tierce. Son typique des louanges.</li>
<li><b>Sixtes en doubles cordes</b> (cordes 3 et 1, ou 4 et 2) qui glissent sur la gamme : les « church licks ».</li>
<li><b>Glissés chromatiques</b> d’une case vers chaque accord, surtout dans les praise breaks.</li>
<li><b>Accords de passage</b> : diminués entre les degrés, 13♭9 et 7♯9 sur les dominantes, IV/V avant le I.</li>
<li><b>Praise break / shout</b> : double-croches très serrées, étouffements de la main gauche, accents sur les contretemps, et on monte d’un demi-ton pour relancer.</li>
<li><b>Attaque</b> : sur les chants lents, arpèges aux doigts avec un léger trémolo ; sur les louanges rapides, médiator et coups secs.</li>
</ul>
<div class="xv" data-c="F" data-f="triads"></div>
<p>Progressions à travailler : <a class="xp" data-p="gwalk">montée gospel</a> · <a class="xp" data-p="pbreak1">praise break mineur</a> · <a class="xp" data-p="pw1">I – V/7 – vi – IV</a> · <a class="xp" data-p="tgospel">turnaround gospel</a>.</p>` },

  { cat: 'Techniques de guitaristes', id: 'tq-rnb', title: 'Guitare R&B et neo-soul', body: `
<ul>
<li><b>Doubles cordes en tierces et en sixtes</b> autour de l’accord (style Curtis Mayfield).</li>
<li><b>Embellissements</b> : hammer-on de la 9 vers la 3, de la 4 vers la 3 ; pull-off de la 13 vers la quinte.</li>
<li><b>Accords en quartes</b> et triades couleur sur les cordes aiguës, souvent glissées d’une case.</li>
<li><b>Peu d’accords</b>, mais une main droite très douce (pouce ou doigts) et beaucoup d’espace.</li>
<li>Petites <b>phrases pentatoniques</b> entre deux accords, dans la même position que la forme d’accord.</li>
</ul>
<div class="xv" data-c="Am11" data-f="quartal"></div>
<p>Progressions : <a class="xp" data-p="jt2u">Imaj7 – VII7 – iii7</a> · <a class="xp" data-p="ns23">ii – iii – IV</a> · <a class="xp" data-p="rb6251">6-2-5-1</a>.</p>` },

  { cat: 'Techniques de guitaristes', id: 'tq-chordmelody', title: 'Chord-melody et basse + accords', body: `
<p>Le <b>chord-melody</b> consiste à jouer la mélodie et les accords en même temps : la mélodie est toujours la <b>note la plus aiguë</b> de l’accord.</p>
<ol>
<li>Repère la note de mélodie sur la corde 1 ou 2.</li>
<li>Choisis un voicing <b>drop 2 ou drop 3</b> dont la note aiguë est cette note (l’onglet Accords montre tous les renversements).</li>
<li>Entre deux accords, joue les notes de passage seules ou harmonisées en dim7.</li>
</ol>
<div class="xv" data-c="Cmaj7" data-f="drop3"></div>
<h4>Basse + accords (« walking »)</h4>
<p>Le pouce joue une ligne de basse sur les cordes 6 et 5 (une note par temps), les doigts piquent un shell ou une triade sur les contretemps. C’est la façon de jouer seul sans bassiste.</p>` },
];
const LESSON_CATS = [...new Set(LESSONS.map((l) => l.cat))];
