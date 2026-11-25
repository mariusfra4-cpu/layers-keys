// Théorie musicale de base : notes, intervalles, accords, gammes, modes, tonalités.
'use strict';
const M = (() => {
  const SHARP = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const FLAT = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B'];
  const LATIN = { C: 'Do', D: 'Ré', E: 'Mi', F: 'Fa', G: 'Sol', A: 'La', B: 'Si' };
  // Tonalités préférant les bémols (majeures) : F Bb Eb Ab Db Gb
  const FLAT_KEYS = new Set([5, 10, 3, 8, 1, 6]);
  let notation = 'en'; // 'en' (C D E) ou 'fr' (Do Ré Mi)
  let preferFlats = null; // null = noms usuels (D♭ E♭ F♯ A♭ B♭)
  const MIXED = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];

  function setNotation(n) { notation = n; }
  function getNotation() { return notation; }
  function setPreferFlats(b) { preferFlats = b; }

  const mod = (n, m = 12) => ((n % m) + m) % m;

  function pcName(pc, flats) {
    const f = flats === undefined ? preferFlats : flats;
    const n = (f === null ? MIXED : f ? FLAT : SHARP)[mod(pc)];
    if (notation === 'fr') {
      const base = LATIN[n[0]];
      const acc = n.length > 1 ? (n[1] === '#' ? '♯' : '♭') : '';
      return base + acc;
    }
    return n.replace('#', '♯').replace(/^([A-G])b/, '$1♭');
  }
  function noteName(midi, flats) { return pcName(midi, flats) + (Math.floor(midi / 12) - 1); }
  function pcFromName(s) {
    s = s.trim();
    const m = /^([A-Ga-g])([#♯b♭]?)/.exec(s);
    if (!m) return null;
    let pc = SHARP.indexOf(m[1].toUpperCase());
    if (m[2] === '#' || m[2] === '♯') pc++;
    if (m[2] === 'b' || m[2] === '♭') pc--;
    return mod(pc);
  }
  function midiFromName(s) { // "C4", "Eb3"
    const m = /^([A-G][#b]?)(-?\d)$/.exec(s);
    if (!m) return null;
    return pcFromName(m[1]) + (parseInt(m[2], 10) + 1) * 12;
  }
  const midiToFreq = (m) => 440 * Math.pow(2, (m - 69) / 12);
  const freqToMidi = (f) => 69 + 12 * Math.log2(f / 440);

  // ---------- Intervalles ----------
  const INTERVALS = [
    { st: 0, short: '1', name: 'Unisson', q: 'juste', feel: 'La même note.' , song: '—' },
    { st: 1, short: '♭2', name: 'Seconde mineure', q: 'dissonant', feel: 'Tension forte, menaçante.', song: 'Les Dents de la mer' },
    { st: 2, short: '2', name: 'Seconde majeure', q: 'doux', feel: 'Un pas, ouvert.', song: 'Joyeux anniversaire (Hap-py)' },
    { st: 3, short: '♭3', name: 'Tierce mineure', q: 'consonant', feel: 'Mélancolique, cœur de l’accord mineur.', song: 'Smoke on the Water (2 premières notes)' },
    { st: 4, short: '3', name: 'Tierce majeure', q: 'consonant', feel: 'Lumineux, cœur de l’accord majeur.', song: 'Oh When the Saints' },
    { st: 5, short: '4', name: 'Quarte juste', q: 'juste', feel: 'Solide, en suspens.', song: 'Here Comes the Bride / La Marseillaise' },
    { st: 6, short: '♭5', name: 'Triton (quarte aug. / quinte dim.)', q: 'dissonant', feel: 'Instable, veut se résoudre.', song: 'The Simpsons / Maria (West Side Story)' },
    { st: 7, short: '5', name: 'Quinte juste', q: 'juste', feel: 'Puissant, neutre (power chord).', song: 'Star Wars (thème)' },
    { st: 8, short: '♭6', name: 'Sixte mineure', q: 'consonant', feel: 'Doux-amer.', song: 'The Entertainer (montée)' },
    { st: 9, short: '6', name: 'Sixte majeure', q: 'consonant', feel: 'Chaleureux, nostalgique.', song: 'My Way / NBC' },
    { st: 10, short: '♭7', name: 'Septième mineure', q: 'dissonant doux', feel: 'Bluesy, couleur de dominante.', song: 'Star Trek (thème)' },
    { st: 11, short: '7', name: 'Septième majeure', q: 'dissonant doux', feel: 'Rêveur, jazzy.', song: 'Take On Me (refrain)' },
    { st: 12, short: '8', name: 'Octave', q: 'juste', feel: 'La même note, plus haut.', song: 'Somewhere Over the Rainbow' },
  ];
  // Nom d'un degré d'accord (pour les étiquettes de diagrammes)
  const DEG = ['1', '♭9', '9', '♭3', '3', '11', '♭5', '5', '♯5', '6', '♭7', '7'];
  function degLabel(semi, chordIntervals) {
    const s = mod(semi);
    // Contexte : ♯9 si l'accord a aussi une tierce majeure, ♯11 si la quinte est juste, 13 pour la sixte sur un accord de 7e
    const has = (x) => chordIntervals && chordIntervals.some((i) => mod(i) === x);
    if (s === 3 && has(4)) return '♯9';
    if (s === 6 && has(7)) return '♯11';
    if (s === 8 && has(7)) return '♭13';
    if (s === 9) {
      if (has(3) && has(6) && !has(7) && !has(10) && !has(11)) return '♭♭7';
      return has(10) || has(11) ? '13' : '6';
    }
    if (s === 5) return has(4) || has(3) ? '11' : '4';
    if (s === 2) return has(4) || has(3) ? '9' : '2';
    if (s === 1) return '♭9';
    return DEG[s];
  }

  // ---------- Accords ----------
  // sym : suffixe ; iv : intervalles ; ess : notes indispensables sur la guitare ; lvl : 1 = basique, 2 = 7e, 3 = jazz/gospel/R&B
  const CHORDS = [
    { id: 'maj', sym: '', name: 'Majeur', iv: [0, 4, 7], lvl: 1, style: 'Tous styles' },
    { id: 'min', sym: 'm', name: 'Mineur', iv: [0, 3, 7], lvl: 1, style: 'Tous styles' },
    { id: '5', sym: '5', name: 'Power chord (quinte)', iv: [0, 7], lvl: 1, style: 'Rock, metal' },
    { id: 'dim', sym: 'dim', name: 'Diminué', iv: [0, 3, 6], lvl: 2, style: 'Passages, classique' },
    { id: 'aug', sym: 'aug', name: 'Augmenté', iv: [0, 4, 8], lvl: 2, style: 'Transitions, jazz' },
    { id: 'sus2', sym: 'sus2', name: 'Suspendu 2', iv: [0, 2, 7], lvl: 1, style: 'Pop, worship' },
    { id: 'sus4', sym: 'sus4', name: 'Suspendu 4', iv: [0, 5, 7], lvl: 1, style: 'Pop, rock, gospel' },
    { id: 'add9', sym: 'add9', name: 'Majeur add9', iv: [0, 4, 7, 2], lvl: 2, style: 'Pop, worship' },
    { id: 'madd9', sym: 'm(add9)', name: 'Mineur add9', iv: [0, 3, 7, 2], lvl: 2, style: 'Pop, ballades' },
    { id: '6', sym: '6', name: 'Sixte', iv: [0, 4, 7, 9], ess: [0, 4, 9], lvl: 2, style: 'Jazz, swing, blues' },
    { id: 'm6', sym: 'm6', name: 'Mineur sixte', iv: [0, 3, 7, 9], ess: [0, 3, 9], lvl: 2, style: 'Jazz, manouche' },
    { id: '69', sym: '6/9', name: 'Six-neuf', iv: [0, 4, 7, 9, 2], ess: [0, 4, 9, 2], lvl: 3, style: 'Jazz, bossa, gospel' },
    { id: '7', sym: '7', name: 'Septième (dominante)', iv: [0, 4, 7, 10], ess: [0, 4, 10], lvl: 2, style: 'Blues, tous styles' },
    { id: 'maj7', sym: 'maj7', name: 'Septième majeure', iv: [0, 4, 7, 11], ess: [0, 4, 11], lvl: 2, style: 'Jazz, R&B, bossa' },
    { id: 'm7', sym: 'm7', name: 'Mineur septième', iv: [0, 3, 7, 10], ess: [0, 3, 10], lvl: 2, style: 'Jazz, R&B, funk' },
    { id: 'mmaj7', sym: 'm(maj7)', name: 'Mineur 7e majeure', iv: [0, 3, 7, 11], ess: [0, 3, 11], lvl: 3, style: 'Jazz, film' },
    { id: 'm7b5', sym: 'm7♭5', name: 'Demi-diminué', iv: [0, 3, 6, 10], lvl: 2, style: 'Jazz (ii en mineur)' },
    { id: 'dim7', sym: 'dim7', name: 'Diminué 7', iv: [0, 3, 6, 9], lvl: 2, style: 'Gospel, jazz, passages' },
    { id: '7sus4', sym: '7sus4', name: 'Septième sus4', iv: [0, 5, 7, 10], ess: [0, 5, 10], lvl: 2, style: 'Funk, gospel' },
    { id: '9', sym: '9', name: 'Neuvième', iv: [0, 4, 7, 10, 2], ess: [0, 4, 10, 2], lvl: 3, style: 'Funk, blues, gospel' },
    { id: 'maj9', sym: 'maj9', name: 'Neuvième majeure', iv: [0, 4, 7, 11, 2], ess: [0, 4, 11, 2], lvl: 3, style: 'R&B, neo-soul, jazz' },
    { id: 'm9', sym: 'm9', name: 'Mineur neuvième', iv: [0, 3, 7, 10, 2], ess: [0, 3, 10, 2], lvl: 3, style: 'R&B, neo-soul' },
    { id: '9sus4', sym: '9sus4', name: 'Neuvième sus4 (« 11 »)', iv: [0, 5, 7, 10, 2], ess: [0, 5, 10, 2], lvl: 3, style: 'Gospel, R&B (le fameux IV/V)' },
    { id: 'm11', sym: 'm11', name: 'Mineur onzième', iv: [0, 3, 7, 10, 2, 5], ess: [0, 3, 10, 5], lvl: 3, style: 'Neo-soul, R&B, jazz modal' },
    { id: '13', sym: '13', name: 'Treizième', iv: [0, 4, 7, 10, 2, 9], ess: [0, 4, 10, 9], lvl: 3, style: 'Jazz, gospel, funk' },
    { id: 'maj13', sym: 'maj13', name: 'Treizième majeure', iv: [0, 4, 7, 11, 2, 9], ess: [0, 4, 11, 9], lvl: 3, style: 'Neo-soul, jazz' },
    { id: 'm13', sym: 'm13', name: 'Mineur treizième', iv: [0, 3, 7, 10, 2, 9], ess: [0, 3, 10, 9], lvl: 3, style: 'Neo-soul, jazz' },
    { id: '7b9', sym: '7♭9', name: 'Septième ♭9', iv: [0, 4, 7, 10, 1], ess: [0, 4, 10, 1], lvl: 3, style: 'Jazz (V vers mineur), gospel' },
    { id: '7s9', sym: '7♯9', name: 'Septième ♯9 (« Hendrix »)', iv: [0, 4, 7, 10, 3], ess: [0, 4, 10, 3], lvl: 3, style: 'Funk, rock, gospel' },
    { id: '7s5', sym: '7♯5', name: 'Septième ♯5', iv: [0, 4, 8, 10], lvl: 3, style: 'Jazz, gospel' },
    { id: '7b5', sym: '7♭5', name: 'Septième ♭5', iv: [0, 4, 6, 10], lvl: 3, style: 'Jazz' },
    { id: '7s11', sym: '7♯11', name: 'Septième ♯11 (lydien ♭7)', iv: [0, 4, 7, 10, 6], ess: [0, 4, 10, 6], lvl: 3, style: 'Jazz, fusion' },
    { id: 'maj7s11', sym: 'maj7♯11', name: 'Maj7 ♯11 (lydien)', iv: [0, 4, 7, 11, 6], ess: [0, 4, 11, 6], lvl: 3, style: 'Jazz, neo-soul, film' },
    { id: '13s11', sym: '13♯11', name: 'Treizième ♯11', iv: [0, 4, 7, 10, 2, 6, 9], ess: [0, 4, 10, 6, 9], lvl: 3, style: 'Jazz' },
    { id: '7alt', sym: '7alt', name: 'Altéré (7♯5♯9)', iv: [0, 4, 8, 10, 3], ess: [0, 4, 8, 10, 3], lvl: 3, style: 'Jazz, gospel' },
    { id: '13b9', sym: '13♭9', name: 'Treizième ♭9', iv: [0, 4, 7, 10, 1, 9], ess: [0, 4, 10, 1, 9], lvl: 3, style: 'Jazz, gospel' },
  ];
  const CHORD_BY_ID = Object.fromEntries(CHORDS.map((c) => [c.id, c]));
  for (const c of CHORDS) {
    if (!c.ess) c.ess = c.iv.slice();
    c.pcs = [...new Set(c.iv.map((i) => mod(i)))];
  }
  function chordSymbol(root, id, bass) {
    const c = CHORD_BY_ID[id];
    let s = pcName(root) + (notation === 'fr' && c.sym === 'm' ? 'm' : c.sym);
    if (bass !== undefined && bass !== null && mod(bass) !== mod(root)) s += '/' + pcName(bass);
    return s;
  }
  // Orthographe des notes d'un accord : dièses pour D E F♯ G A B (sauf mineurs sur D et G), bémols pour D♭ E♭ F A♭ B♭
  function chordNotes(root, id) {
    let fl;
    if (preferFlats !== null) fl = preferFlats;
    else {
      const r = mod(root), c = CHORD_BY_ID[id];
      const minorish = c.iv.includes(3) && !c.iv.includes(4);
      fl = [1, 3, 5, 8, 10].includes(r) || r === 0 || (minorish && [2, 7].includes(r));
    }
    return CHORD_BY_ID[id].iv.map((i) => pcName(root + i, fl));
  }
  // Analyse d'un symbole tapé par l'utilisateur : "Dm7", "Bbmaj9", "F#m7b5", "C/E"
  const SYM_ALIASES = {
    '': 'maj', 'M': 'maj', 'maj': 'maj', 'm': 'min', 'min': 'min', '-': 'min', '5': '5', 'dim': 'dim', '°': 'dim', 'aug': 'aug', '+': 'aug',
    'sus2': 'sus2', 'sus4': 'sus4', 'sus': 'sus4', 'add9': 'add9', 'madd9': 'madd9', 'm(add9)': 'madd9', '6': '6', 'm6': 'm6', '6/9': '69', '69': '69',
    '7': '7', 'maj7': 'maj7', 'M7': 'maj7', 'Δ': 'maj7', 'Δ7': 'maj7', 'm7': 'm7', 'min7': 'm7', '-7': 'm7', 'm(maj7)': 'mmaj7', 'mmaj7': 'mmaj7', 'mM7': 'mmaj7',
    'm7b5': 'm7b5', 'ø': 'm7b5', 'ø7': 'm7b5', 'dim7': 'dim7', '°7': 'dim7', '7sus4': '7sus4', '7sus': '7sus4', '9': '9', 'maj9': 'maj9', 'M9': 'maj9',
    'm9': 'm9', '9sus4': '9sus4', '11': '9sus4', 'm11': 'm11', '13': '13', 'maj13': 'maj13', 'm13': 'm13', '7b9': '7b9', '7#9': '7s9', '7#5': '7s5', '7+': '7s5', 'aug7': '7s5',
    '7b5': '7b5', '7#11': '7s11', 'maj7#11': 'maj7s11', '13#11': '13s11', '7alt': '7alt', 'alt': '7alt', '13b9': '13b9',
  };
  function parseChord(str) {
    str = str.trim().replace(/♯/g, '#').replace(/♭/g, 'b');
    const m = /^([A-Ga-g][#b]?)([^/]*)(?:\/([A-Ga-g][#b]?))?$/.exec(str);
    if (!m) return null;
    const root = pcFromName(m[1]);
    const id = SYM_ALIASES[m[2]];
    if (id === undefined) return null;
    return { root, id, bass: m[3] ? pcFromName(m[3]) : undefined };
  }

  // ---------- Gammes et modes ----------
  const SCALES = [
    { id: 'major', name: 'Majeure (ionien)', iv: [0, 2, 4, 5, 7, 9, 11] },
    { id: 'minor', name: 'Mineure naturelle (éolien)', iv: [0, 2, 3, 5, 7, 8, 10] },
    { id: 'harm', name: 'Mineure harmonique', iv: [0, 2, 3, 5, 7, 8, 11] },
    { id: 'mel', name: 'Mineure mélodique (jazz)', iv: [0, 2, 3, 5, 7, 9, 11] },
    { id: 'penta', name: 'Pentatonique majeure', iv: [0, 2, 4, 7, 9] },
    { id: 'pentam', name: 'Pentatonique mineure', iv: [0, 3, 5, 7, 10] },
    { id: 'blues', name: 'Blues (mineure)', iv: [0, 3, 5, 6, 7, 10] },
    { id: 'bluesM', name: 'Blues majeure', iv: [0, 2, 3, 4, 7, 9] },
    { id: 'dorian', name: 'Dorien', iv: [0, 2, 3, 5, 7, 9, 10] },
    { id: 'phryg', name: 'Phrygien', iv: [0, 1, 3, 5, 7, 8, 10] },
    { id: 'lydian', name: 'Lydien', iv: [0, 2, 4, 6, 7, 9, 11] },
    { id: 'mixo', name: 'Mixolydien', iv: [0, 2, 4, 5, 7, 9, 10] },
    { id: 'locrian', name: 'Locrien', iv: [0, 1, 3, 5, 6, 8, 10] },
    { id: 'phrygdom', name: 'Phrygien dominant (5e mode harm.)', iv: [0, 1, 4, 5, 7, 8, 10] },
    { id: 'lydb7', name: 'Lydien ♭7 (4e mode mél.)', iv: [0, 2, 4, 6, 7, 9, 10] },
    { id: 'altered', name: 'Altérée / super-locrien (7e mode mél.)', iv: [0, 1, 3, 4, 6, 8, 10] },
    { id: 'hw', name: 'Diminuée demi-ton/ton (dominante)', iv: [0, 1, 3, 4, 6, 7, 9, 10] },
    { id: 'wh', name: 'Diminuée ton/demi-ton', iv: [0, 2, 3, 5, 6, 8, 9, 11] },
    { id: 'whole', name: 'Par tons', iv: [0, 2, 4, 6, 8, 10] },
    { id: 'bebopdom', name: 'Bebop dominante', iv: [0, 2, 4, 5, 7, 9, 10, 11] },
  ];
  const SCALE_BY_ID = Object.fromEntries(SCALES.map((s) => [s.id, s]));

  const MODES = [
    { name: 'Ionien', deg: 'I', iv: [0, 2, 4, 5, 7, 9, 11], chord: 'maj7', color: 'La gamme majeure : stable, joyeuse.', char: '7 majeure, 4 juste', use: 'Pop, accords Imaj7.' },
    { name: 'Dorien', deg: 'II', iv: [0, 2, 3, 5, 7, 9, 10], chord: 'm7', color: 'Mineur mais lumineux grâce à la 6te majeure.', char: '6 majeure', use: 'Funk, jazz modal (So What), Santana, neo-soul sur m7/m9.' },
    { name: 'Phrygien', deg: 'III', iv: [0, 1, 3, 5, 7, 8, 10], chord: 'm7', color: 'Sombre, espagnol.', char: '♭2', use: 'Flamenco, metal.' },
    { name: 'Lydien', deg: 'IV', iv: [0, 2, 4, 6, 7, 9, 11], chord: 'maj7s11', color: 'Flottant, rêveur, cinématique.', char: '♯4', use: 'Musique de film, neo-soul, jazz sur IVmaj7.' },
    { name: 'Mixolydien', deg: 'V', iv: [0, 2, 4, 5, 7, 9, 10], chord: '7', color: 'Majeur bluesy.', char: '♭7', use: 'Blues, rock, funk, gospel sur les accords 7/9/13.' },
    { name: 'Éolien', deg: 'VI', iv: [0, 2, 3, 5, 7, 8, 10], chord: 'm7', color: 'Mineur naturel, triste.', char: '♭6', use: 'Pop, rock, ballades.' },
    { name: 'Locrien', deg: 'VII', iv: [0, 1, 3, 5, 6, 8, 10], chord: 'm7b5', color: 'Instable, très sombre.', char: '♭2 et ♭5', use: 'Sur le m7♭5 (ii d’un ii-V-i mineur).' },
  ];

  // ---------- Tonalités et harmonie diatonique ----------
  const ROMAN = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII'];
  // Accords à 4 sons de la gamme majeure / mineure naturelle
  const DIATONIC = {
    major: { triads: ['maj', 'min', 'min', 'maj', 'maj', 'min', 'dim'], sevenths: ['maj7', 'm7', 'm7', 'maj7', '7', 'm7', 'm7b5'] },
    minor: { triads: ['min', 'dim', 'maj', 'min', 'min', 'maj', 'maj'], sevenths: ['m7', 'm7b5', 'maj7', 'm7', 'm7', 'maj7', '7'] },
  };
  function diatonicChords(tonic, mode = 'major', sevenths = true) {
    const sc = mode === 'major' ? SCALE_BY_ID.major.iv : SCALE_BY_ID.minor.iv;
    const types = DIATONIC[mode][sevenths ? 'sevenths' : 'triads'];
    return sc.map((iv, i) => ({ root: mod(tonic + iv), id: types[i], roman: romanFor(i, types[i]) }));
  }
  function romanFor(deg, id) {
    let r = ROMAN[deg];
    const c = CHORD_BY_ID[id];
    const minorish = c.iv.includes(3) && !c.iv.includes(4);
    if (minorish) r = r.toLowerCase();
    let suf = '';
    if (id === 'dim') suf = '°';
    else if (id === 'dim7') suf = '°7';
    else if (id === 'm7b5') suf = 'ø7';
    else if (id === 'aug') suf = '+';
    else if (id !== 'maj' && id !== 'min') suf = c.sym.replace(/^m(?!aj)/, '');
    return r + suf;
  }
  // Degré romain d'un accord dans une tonalité (gère les degrés altérés : ♭VII, ♭III...)
  const DEG_OF_SEMI = { 0: 'I', 1: '♭II', 2: 'II', 3: '♭III', 4: 'III', 5: 'IV', 6: '♯IV', 7: 'V', 8: '♭VI', 9: 'VI', 10: '♭VII', 11: 'VII' };
  const DEG_OF_SEMI_MIN = { 0: 'I', 1: '♭II', 2: 'II', 3: 'III', 4: '♮III', 5: 'IV', 6: '♯IV', 7: 'V', 8: 'VI', 9: '♮VI', 10: 'VII', 11: '♮VII' };
  function romanOf(root, id, key) {
    if (!key) return '';
    const semi = mod(root - key.tonic);
    let r = (key.mode === 'minor' ? DEG_OF_SEMI_MIN : DEG_OF_SEMI)[semi];
    const c = CHORD_BY_ID[id];
    const minorish = c.iv.includes(3) && !c.iv.includes(4);
    if (minorish || id === 'dim' || id === 'm7b5' || id === 'dim7') r = r.replace(/[IV]+/, (x) => x.toLowerCase());
    let suf = '';
    if (id === 'dim') suf = '°';
    else if (id === 'dim7') suf = '°7';
    else if (id === 'm7b5') suf = 'ø7';
    else if (id === 'aug') suf = '+';
    else if (id !== 'maj' && id !== 'min') suf = c.sym.replace(/^m(?!aj)/, '');
    return r + suf;
  }
  function keyName(key) { return pcName(key.tonic, FLAT_KEYS.has(key.mode === 'major' ? key.tonic : mod(key.tonic + 3))) + (key.mode === 'major' ? ' majeur' : ' mineur'); }
  function keyPrefersFlats(key) { return FLAT_KEYS.has(key.mode === 'major' ? key.tonic : mod(key.tonic + 3)); }

  // Identification d'un ensemble de notes (diagramme tapé par l'utilisateur)
  function identify(pcs, bassPc) {
    const set = new Set(pcs.map((p) => mod(p)));
    const res = [];
    for (let r = 0; r < 12; r++) {
      for (const c of CHORDS) {
        const cs = new Set(c.pcs.map((i) => mod(i + r)));
        let missing = 0, extra = 0;
        for (const p of cs) if (!set.has(p)) missing += (mod(p - r) === 7 ? 0.4 : 1);
        for (const p of set) if (!cs.has(p)) extra++;
        if (extra > 0) continue;
        const essMiss = c.ess.filter((i) => !set.has(mod(r + i))).length;
        if (essMiss > 0) continue;
        let score = 10 - missing * 2 - c.pcs.length * 0.1;
        if (bassPc !== undefined && mod(bassPc) === r) score += 3;
        res.push({ root: r, id: c.id, score, bass: bassPc !== undefined && mod(bassPc) !== r ? bassPc : undefined });
      }
    }
    res.sort((a, b) => b.score - a.score);
    return res.slice(0, 5);
  }

  return {
    SHARP, FLAT, INTERVALS, CHORDS, CHORD_BY_ID, SCALES, SCALE_BY_ID, MODES, ROMAN, mod,
    pcName, noteName, pcFromName, midiFromName, midiToFreq, freqToMidi, degLabel,
    chordSymbol, chordNotes, parseChord, diatonicChords, romanOf, romanFor, keyName, keyPrefersFlats,
    identify, setNotation, getNotation, setPreferFlats,
  };
})();
if (typeof module !== 'undefined') module.exports = M;
