// Générateur d'accords de passage : toutes les façons classiques (jazz, gospel, blues, R&B, praise) d'arriver sur un accord.
// Chaque règle produit une ou plusieurs suites d'accords placées juste avant l'accord d'arrivée.
'use strict';
const Passing = (() => {
  const mod = M.mod;
  const isMinor = (id) => { const c = M.CHORD_BY_ID[id]; return c.iv.includes(3) && !c.iv.includes(4); };
  const isDom = (id) => { const c = M.CHORD_BY_ID[id]; return c.iv.includes(4) && c.iv.includes(10); };
  const ch = (root, id, bass) => ({ root: mod(root), id, bass: bass === undefined ? undefined : mod(bass) });

  // Variantes de dominante, de la plus simple à la plus colorée
  const DOMS_MAJ = ['7', '9', '13', '7s5', '7s9', '7b9', '13b9', '7alt', '9sus4'];
  const DOMS_MIN = ['7b9', '7', '7alt', '7s9', '13b9', '7s5'];

  // Règles : (t = fondamentale d'arrivée, q = type d'arrivée) → [{ chords, name, why, styles, level }]
  const RULES = [
    { id: 'dom', name: 'Dominante secondaire (V7 de l’accord)', styles: ['Jazz', 'Gospel', 'Blues', 'R&B', 'Praise'], level: 1,
      gen: (t, q) => (isMinor(q) ? DOMS_MIN : DOMS_MAJ).map((d) => ({ chords: [ch(t + 7, d)], variant: d })),
      why: 'L’accord situé une quinte au-dessus, joué en 7 de dominante : sa tierce monte d’un demi-ton vers la fondamentale d’arrivée, sa septième descend vers la tierce. La résolution la plus forte de toute la musique tonale.' },
    { id: 'iiv', name: 'ii – V vers l’accord', styles: ['Jazz', 'Gospel', 'R&B'], level: 1,
      gen: (t, q) => {
        const ii = isMinor(q) ? ['m7b5'] : ['m7', 'm9', 'm11'];
        const v = isMinor(q) ? ['7b9', '7alt', '13b9'] : ['7', '13', '7b9', '9sus4'];
        const out = [];
        for (const a of ii) for (const b of v) out.push({ chords: [ch(t + 2, a), ch(t + 7, b)] });
        return out;
      },
      why: 'On fait précéder la dominante de son propre « ii » : sous-dominante → dominante → arrivée. En jazz, tout accord peut être précédé de son ii–V.' },
    { id: 'tritone', name: 'Substitution tritonique (♭II7)', styles: ['Jazz', 'Gospel', 'R&B'], level: 2,
      gen: (t) => ['7', '9', '13', '7s11'].map((d) => ({ chords: [ch(t + 1, d)] })),
      why: 'La dominante située un demi-ton au-dessus de l’arrivée remplace le V7 : elle contient le même triton. La basse descend chromatiquement vers l’accord.' },
    { id: 'iisubv', name: 'ii – ♭II7 (ii–V avec substitution)', styles: ['Jazz', 'Gospel'], level: 2,
      gen: (t, q) => [
        { chords: [ch(t + 2, isMinor(q) ? 'm7b5' : 'm7'), ch(t + 1, '7')] },
        { chords: [ch(t + 8, 'm7'), ch(t + 1, '7')] },
        { chords: [ch(t + 2, 'm9'), ch(t + 1, '13')] },
      ],
      why: 'ii–V dont le V est remplacé par sa substitution tritonique : basse D – D♭ – C. Variante : le ii du ♭II7 (A♭m7 – D♭7 – C).' },
    { id: 'dimbelow', name: 'Diminué un demi-ton en dessous (♯°7)', styles: ['Gospel', 'Jazz', 'Blues', 'Praise'], level: 1,
      gen: (t) => [{ chords: [ch(t - 1, 'dim7')] }],
      why: 'Le dim7 placé un demi-ton sous l’arrivée est un V7♭9 sans fondamentale : il monte chromatiquement vers l’accord. Le passage de base du gospel (C – C♯°7 – Dm7).' },
    { id: 'dimsame', name: 'Diminué sur la même basse (accord « d’église »)', styles: ['Gospel', 'Blues'], level: 2,
      gen: (t, q) => [{ chords: [ch(t, 'dim7')], note: 'On joue d’abord le °7 puis on « ouvre » vers ' + (isMinor(q) ? 'le mineur' : 'le majeur') }],
      why: 'Le °7 construit sur la fondamentale d’arrivée, qui se résout sans changer de basse : effet de broderie très utilisé en gospel et en blues (C°7 → C6).' },
    { id: 'chromUp', name: 'Glissé chromatique par le dessus', styles: ['R&B', 'Gospel', 'Jazz', 'Funk'], level: 1,
      gen: (t, q) => [{ chords: [ch(t + 1, q)] }, { chords: [ch(t + 1, isMinor(q) ? 'm9' : isDom(q) ? '9' : 'maj9')] }],
      why: 'Le même accord un demi-ton au-dessus, puis glissé vers l’arrivée. Sur la guitare : même forme, une case plus haut. Signature neo-soul et funk.' },
    { id: 'chromDown', name: 'Glissé chromatique par le dessous', styles: ['R&B', 'Gospel', 'Funk', 'Blues'], level: 1,
      gen: (t, q) => [{ chords: [ch(t - 1, q)] }, { chords: [ch(t - 1, isMinor(q) ? 'm7' : '7')] }],
      why: 'Le même accord une case plus bas, glissé vers le haut : attaque typique du funk et du blues (B9 → C9).' },
    { id: 'backdoor', name: 'Backdoor (iv7 – ♭VII7)', styles: ['Jazz', 'Gospel', 'R&B'], level: 2, onlyMajor: true,
      gen: (t) => [{ chords: [ch(t + 10, '7')] }, { chords: [ch(t + 5, 'm7'), ch(t + 10, '7')] }, { chords: [ch(t + 5, 'm9'), ch(t + 10, '13')] }],
      why: 'On arrive par le ♭VII7, emprunté au mode mineur : résolution douce, « par la porte de derrière ». Très soul et gospel.' },
    { id: 'plagal', name: 'Plagale IV → I et iv mineur (« Amen »)', styles: ['Gospel', 'Praise', 'Blues'], level: 1, onlyMajor: true,
      gen: (t) => [{ chords: [ch(t + 5, 'maj7')] }, { chords: [ch(t + 5, 'm6')] }, { chords: [ch(t + 5, 'maj7'), ch(t + 5, 'm6')] }, { chords: [ch(t + 5, 'maj', t)] }],
      why: 'Le IV qui retombe sur I, la cadence d’église. Le iv mineur (emprunt) rend l’arrivée très émouvante ; IV/I garde la basse sur la tonique.' },
    { id: 'sus', name: 'Dominante suspendue → résolue', styles: ['Gospel', 'Praise', 'R&B'], level: 1,
      gen: (t, q) => [{ chords: [ch(t + 7, '9sus4'), ch(t + 7, isMinor(q) ? '7b9' : '13')] }, { chords: [ch(t + 7, '7sus4'), ch(t + 7, '7')] }, { chords: [ch(t + 5, 'maj', t + 7), ch(t + 7, '7b9')] }],
      why: 'Le V suspendu (IV/V = V9sus4) puis sa résolution : la quarte descend vers la tierce juste avant l’arrivée. Indispensable en gospel et en praise.' },
    { id: 'chain', name: 'Chaîne de dominantes (III7 – VI7 – II7 – V7)', styles: ['Jazz', 'Blues', 'Gospel'], level: 2,
      gen: (t) => [
        { chords: [ch(t + 2, '7'), ch(t + 7, '7')] },
        { chords: [ch(t + 9, '7'), ch(t + 2, '7'), ch(t + 7, '7')] },
        { chords: [ch(t + 4, '7'), ch(t + 9, '7'), ch(t + 2, '7'), ch(t + 7, '7')] },
        { chords: [ch(t + 4, '7s9'), ch(t + 9, '7s5'), ch(t + 2, '9'), ch(t + 7, '13')] },
      ],
      why: 'Chaque accord est la dominante du suivant, en descendant par quintes : le « ragtime » et le turnaround blues-jazz.' },
    { id: '736', name: '7 – 3 – 6 – 2 – 5 (tour gospel)', styles: ['Gospel', 'Jazz'], level: 3,
      gen: (t, q) => [
        { chords: [ch(t + 11, 'm7b5'), ch(t + 4, '7s9'), ch(t + 9, 'm9'), ch(t + 2, 'm9'), ch(t + 7, '13')] },
        { chords: [ch(t + 11, 'm7b5'), ch(t + 4, '7b9'), ch(t + 9, 'm7'), ch(t + 9, '7s5'), ch(t + 2, 'm9'), ch(t + 7, '13b9')] },
        { chords: [ch(t + 4, 'm7'), ch(t + 9, '7s9'), ch(t + 2, 'm9'), ch(t + 7, isMinor(q) ? '7alt' : '9sus4')] },
      ],
      why: 'Le cycle des quintes complet depuis le VII : chaque accord mineur peut devenir dominant (III7♯9, VI7♯5) pour pousser plus fort vers le suivant.' },
    { id: 'walk', name: 'Montée de basse gospel (I – I/3 – IV – ♯IV°7 – I/5)', styles: ['Gospel', 'Praise'], level: 2, onlyMajor: true,
      gen: (t) => [
        { chords: [ch(t + 5, 'maj'), ch(t + 6, 'dim7'), ch(t, 'maj', t + 7), ch(t + 7, '7')] },
        { chords: [ch(t, '7', t + 4), ch(t + 5, 'maj7'), ch(t + 6, 'dim7'), ch(t, 'maj', t + 7), ch(t + 7, '9sus4'), ch(t + 7, '7b9')] },
        { chords: [ch(t + 1, 'dim7'), ch(t + 2, 'm7'), ch(t + 3, 'dim7'), ch(t, 'maj', t + 4)] },
      ],
      why: 'La basse monte par demi-tons ; chaque note intermédiaire reçoit un °7 ou un renversement. La façon la plus « église » de revenir à la tonique.' },
    { id: 'slashV', name: 'Basse qui descend (V/7 → I/3, renversements)', styles: ['Praise', 'Gospel', 'Pop'], level: 1,
      gen: (t, q) => [
        { chords: [ch(t + 7, '7', t + 5)], note: 'puis l’accord d’arrivée en 1er renversement (tierce à la basse)', bassTarget: t + (isMinor(q) ? 3 : 4) },
        { chords: [ch(t + 2, 'm7'), ch(t + 7, 'maj', t + 11)] },
        { chords: [ch(t + 9, 'min'), ch(t + 7, 'maj', t + 11)] },
      ],
      why: 'On garde une ligne de basse mélodique : V7/4 → I/3 (G7/F → C/E), ou vi – V/7 → I. Le son typique du praise & worship et des ballades gospel.' },
    { id: 'church', name: 'Fin d’église ♭VI – ♭VII – I', styles: ['Gospel', 'Praise', 'Rock'], level: 2, onlyMajor: true,
      gen: (t) => [{ chords: [ch(t + 8, 'maj'), ch(t + 10, 'maj')] }, { chords: [ch(t + 8, 'maj7'), ch(t + 10, '9sus4')] }, { chords: [ch(t + 8, '7'), ch(t + 10, '13')] }],
      why: 'Deux accords majeurs empruntés au mineur qui montent par tons vers la tonique : la « cadence épique » des fins de chants.' },
    { id: 'bvi7', name: '♭VI7 → V7 (dominante augmentée)', styles: ['Jazz', 'Gospel', 'Blues'], level: 2,
      gen: (t, q) => [{ chords: [ch(t + 8, '7'), ch(t + 7, isMinor(q) ? '7b9' : '7')] }, { chords: [ch(t + 8, '9'), ch(t + 7, '7s9')] }],
      why: 'Le ♭VI7 descend d’un demi-ton sur le V7 : approche chromatique de la dominante, très utilisée en blues mineur et en gospel.' },
    { id: 'minorplagal', name: '♭VImaj7 – ♭VII13 → i (mineur soul)', styles: ['R&B', 'Gospel'], level: 2, onlyMinor: true,
      gen: (t) => [{ chords: [ch(t + 8, 'maj7'), ch(t + 10, '13')] }, { chords: [ch(t + 5, 'm9'), ch(t + 8, 'maj7'), ch(t + 7, '7s9')] }],
      why: 'En mineur : ♭VI et ♭VII montent vers la tonique, ou iv9 – ♭VImaj7 – V7♯9 pour la couleur gospel mineure.' },
    { id: 'coltrane', name: 'Substitution « Coltrane » (tierces majeures)', styles: ['Jazz'], level: 3, onlyMajor: true,
      gen: (t) => [{ chords: [ch(t + 3, '7'), ch(t + 8, 'maj7'), ch(t + 11, '7'), ch(t + 4, 'maj7'), ch(t + 7, '7')] }],
      why: 'On traverse deux tonalités éloignées d’une tierce majeure avant d’arriver : la technique de Giant Steps appliquée à un V–I.' },
    { id: 'quartal', name: 'Approche neo-soul (accords diatoniques parallèles)', styles: ['R&B', 'Praise'], level: 2,
      gen: (t, q) => isMinor(q)
        ? [{ chords: [ch(t + 2, 'm7b5'), ch(t + 3, 'maj7')] }, { chords: [ch(t - 2, 'maj9'), ch(t - 1, 'maj9')] }]
        : [{ chords: [ch(t + 2, 'm9'), ch(t + 4, 'm7'), ch(t + 5, 'maj9')] }, { chords: [ch(t + 4, 'm7'), ch(t + 2, 'm9')] }, { chords: [ch(t - 2, '13'), ch(t - 1, '13')] }],
      why: 'Des accords enrichis qui bougent par degrés conjoints, souvent avec la même forme de main gauche glissée : la marque du neo-soul et du praise moderne.' },
    { id: 'bluesturn', name: 'Turnaround blues (I7 – VI7 – II7 – V7)', styles: ['Blues', 'Jazz'], level: 1, onlyMajor: true,
      gen: (t) => [{ chords: [ch(t, '7'), ch(t + 5, '7'), ch(t, '7'), ch(t + 7, '7')] }, { chords: [ch(t, '7'), ch(t + 9, '7'), ch(t + 2, '7'), ch(t + 7, '7')] }, { chords: [ch(t, '7'), ch(t + 3, '7'), ch(t + 8, '7'), ch(t + 1, '7')] }],
      why: 'Les deux dernières mesures d’un blues qui relancent la grille. La version avec tritons (I – ♭III7 – ♭VI7 – ♭II7) fait descendre la basse par demi-tons.' },
  ];

  const STYLES = ['Tous', 'Jazz', 'Gospel', 'Blues', 'R&B', 'Praise', 'Funk'];
  const TARGETS = ['maj7', 'maj', 'maj9', '69', 'min', 'm7', 'm9', 'm6', '7', '9', '13'];

  // Toutes les approches pour une arrivée donnée
  function forTarget(t, q, style = 'Tous') {
    const out = [];
    for (const r of RULES) {
      if (style !== 'Tous' && !r.styles.includes(style)) continue;
      if (r.onlyMajor && isMinor(q)) continue;
      if (r.onlyMinor && !isMinor(q)) continue;
      for (const v of r.gen(t, q)) out.push({ rule: r, chords: v.chords, note: v.note, bassTarget: v.bassTarget });
    }
    return out;
  }
  // Nombre total d'enchaînements dans les 12 tonalités
  function total() {
    let n = 0;
    for (let t = 0; t < 12; t++) for (const q of TARGETS) n += forTarget(t, q).length;
    return n;
  }
  // Degré d'un accord de passage par rapport à l'arrivée (« V7/x », « ♭II7 », …)
  const REL = ['I', '♭II', 'II', '♭III', 'III', 'IV', '♯IV', 'V', '♭VI', 'VI', '♭VII', 'VII'];
  function relRoman(c, t) {
    const semi = mod(c.root - t);
    let r = REL[semi];
    const cd = M.CHORD_BY_ID[c.id];
    if ((cd.iv.includes(3) && !cd.iv.includes(4)) || c.id === 'dim7') r = r.replace(/[IV]+/, (x) => x.toLowerCase());
    const suf = c.id === 'dim7' ? '°7' : c.id === 'm7b5' ? 'ø7' : c.id === 'maj' || c.id === 'min' ? '' : cd.sym.replace(/^m(?!aj)/, '');
    return r + suf;
  }
  return { RULES, STYLES, TARGETS, forTarget, total, relRoman, isMinor };
})();
