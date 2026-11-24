// Langue de l'interface. Fichier de traductions : français → anglais, espagnol, portugais.
#include "Lang.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <unordered_map>

namespace lang {
namespace {

struct Row {
    const char* fr;
    const char* other[3];  // anglais, espagnol, portugais
};

const Row kRows[] = {
    {"Annuler", {"Cancel", "Cancelar", "Cancelar"}},
    {"Remplacer", {"Replace", "Reemplazar", "Substituir"}},
    {"Renommer", {"Rename", "Renombrar", "Renomear"}},
    {"Supprimer", {"Delete", "Eliminar", "Excluir"}},
    {"Nouveau nom :", {"New name:", "Nuevo nombre:", "Novo nome:"}},
    {"Attrapez la forme d'onde et déposez-la sur une piste de votre logiciel de musique", {"Grab the waveform and drop it on a track in your music software", "Arrastra la forma de onda y suéltala en una pista de tu software de música", "Arraste a forma de onda e solte-a numa faixa do seu software de música"}},
    {"Choisissez une loop, ou déposez un fichier WAV / MP3 ici", {"Choose a loop, or drop a WAV / MP3 file here", "Elige un loop o suelta aquí un archivo WAV / MP3", "Escolha um loop ou solte aqui um arquivo WAV / MP3"}},
    {"GLISSER VERS VOTRE LOGICIEL  >>", {"DRAG TO YOUR DAW  >>", "ARRASTRAR A TU DAW  >>", "ARRASTAR PARA SUA DAW  >>"}},
    {"Rechercher une loop…", {"Search a loop…", "Buscar un loop…", "Buscar um loop…"}},
    {"Choisir le dossier de vos loops : elles sont toutes analysées et classées par style", {"Choose your loops folder: they are all analysed and sorted by style", "Elige la carpeta de tus loops: se analizan y se ordenan por estilo", "Escolha a pasta dos seus loops: todos são analisados e organizados por estilo"}},
    {"Ouvrir un fichier WAV ou MP3", {"Open a WAV or MP3 file", "Abrir un archivo WAV o MP3", "Abrir um arquivo WAV ou MP3"}},
    {"Créer un nouveau style", {"Create a new style", "Crear un nuevo estilo", "Criar um novo estilo"}},
    {"Copier des loops (WAV, MP3) dans un style", {"Copy loops (WAV, MP3) into a style", "Copiar loops (WAV, MP3) en un estilo", "Copiar loops (WAV, MP3) para um estilo"}},
    {"★ Favoris", {"★ Favourites", "★ Favoritos", "★ Favoritos"}},
    {"Favoris", {"Favourites", "Favoritos", "Favoritos"}},
    {"Nouveau style", {"New style", "Nuevo estilo", "Novo estilo"}},
    {"Nouveau style…", {"New style…", "Nuevo estilo…", "Novo estilo…"}},
    {"Nom du style :", {"Style name:", "Nombre del estilo:", "Nome do estilo:"}},
    {"Loops à copier dans « ", {"Loops to copy into « ", "Loops para copiar en « ", "Loops para copiar em « "}},
    {"Importer dans le style…", {"Import into style…", "Importar al estilo…", "Importar para o estilo…"}},
    {"Dossier de vos loops", {"Your loops folder", "Carpeta de tus loops", "Pasta dos seus loops"}},
    {"Ouvrir une loop", {"Open a loop", "Abrir un loop", "Abrir um loop"}},
    {"Toutes les loops", {"All loops", "Todos los loops", "Todos os loops"}},
    {"Non classés", {"Unsorted", "Sin clasificar", "Sem estilo"}},
    {"Renommer ce style…", {"Rename this style…", "Renombrar este estilo…", "Renomear este estilo…"}},
    {"Importer des loops dans ce style…", {"Import loops into this style…", "Importar loops a este estilo…", "Importar loops para este estilo…"}},
    {"Ouvrir le dossier", {"Open folder", "Abrir la carpeta", "Abrir a pasta"}},
    {"Supprimer ce style (ses loops vont dans « Non classés »)", {"Delete this style (its loops go to « Unsorted »)", "Eliminar este estilo (sus loops pasan a « Sin clasificar »)", "Excluir este estilo (os loops vão para « Sem estilo »)"}},
    {"Renommer le style", {"Rename style", "Renombrar el estilo", "Renomear o estilo"}},
    {"illisible (survoler)", {"unreadable (hover)", "ilegible (pasa el ratón)", "ilegível (passe o mouse)"}},
    {"Illisible : ", {"Unreadable: ", "Ilegible: ", "Ilegível: "}},
    {"☆  Retirer des favoris", {"☆  Remove from favourites", "☆  Quitar de favoritos", "☆  Remover dos favoritos"}},
    {"★  Ajouter aux favoris", {"★  Add to favourites", "★  Añadir a favoritos", "★  Adicionar aos favoritos"}},
    {"Ranger dans le style", {"Move to style", "Mover al estilo", "Mover para o estilo"}},
    {"Montrer le fichier", {"Show file", "Mostrar el archivo", "Mostrar o arquivo"}},
    {"Analyse de la loop… (tempo, percussions, séparation)", {"Analysing the loop… (tempo, drums, separation)", "Analizando el loop… (tempo, percusiones, separación)", "Analisando o loop… (andamento, percussões, separação)"}},
    {"Bienvenue dans African Loops Pro", {"Welcome to African Loops Pro", "Bienvenido a African Loops Pro", "Bem-vindo ao African Loops Pro"}},
    {"Nouvelles loops : ", {"New loops: ", "Nuevos loops: ", "Novos loops: "}},
    {"Analyse du dossier… ", {"Analysing folder… ", "Analizando la carpeta… ", "Analisando a pasta… "}},
    {"Fait une seule fois : ensuite chaque loop se charge aussitôt", {"Done only once: then every loop loads instantly", "Se hace una sola vez: luego cada loop carga al instante", "Feito uma única vez: depois cada loop carrega na hora"}},
    {"Cliquez sur DOSSIER pour choisir le dossier de vos loops.\nElles seront analysées (tempo, percussions) et classées par style.", {"Click FOLDER to choose your loops folder.\nThey will be analysed (tempo, drums) and sorted by style.", "Haz clic en CARPETA para elegir la carpeta de tus loops.\nSe analizarán (tempo, percusiones) y se ordenarán por estilo.", "Clique em PASTA para escolher a pasta dos seus loops.\nEles serão analisados (andamento, percussões) e organizados por estilo."}},
    {"Aucune loop", {"No loops", "Ningún loop", "Nenhum loop"}},
    {"DOSSIER", {"FOLDER", "CARPETA", "PASTA"}},
    {"+ FICHIER", {"+ FILE", "+ ARCHIVO", "+ ARQUIVO"}},
    {"+ STYLE", {"+ STYLE", "+ ESTILO", "+ ESTILO"}},
    {"IMPORTER", {"IMPORT", "IMPORTAR", "IMPORTAR"}},
    {"Tempo : glissez vers le haut ou le bas, double-cliquez pour le taper. PRESET allumé : la loop joue à son propre tempo.", {"Tempo: drag up or down, double-click to type it. PRESET on: the loop plays at its own tempo.", "Tempo: arrastra hacia arriba o abajo, doble clic para escribirlo. PRESET encendido: el loop suena a su propio tempo.", "Andamento: arraste para cima ou para baixo, clique duas vezes para digitar. PRESET aceso: o loop toca no seu próprio andamento."}},
    {"Global BPM", {"Global BPM", "BPM global", "BPM global"}},
    {"Tempo (40 à 240) :", {"Tempo (40 to 240):", "Tempo (40 a 240):", "Andamento (40 a 240):"}},
    {"Attrapez le numéro de la piste pour la déposer seule dans votre logiciel", {"Grab the track number to drop this track alone into your software", "Arrastra el número de la pista para soltarla sola en tu software", "Arraste o número da faixa para soltá-la sozinha no seu software"}},
    {"Allumé : la loop joue à son tempo d'origine", {"On: the loop plays at its original tempo", "Encendido: el loop suena a su tempo original", "Aceso: o loop toca no seu andamento original"}},
    {"Allumé : suit le tempo et la lecture de votre logiciel de musique", {"On: follows the tempo and transport of your music software", "Encendido: sigue el tempo y la reproducción de tu software de música", "Aceso: segue o andamento e a reprodução do seu software de música"}},
    {"Ableton Link : Ableton et les autres logiciels Link prennent le tempo de chaque loop, avec les mêmes mesures et le départ ensemble", {"Ableton Link: Ableton and other Link apps take each loop's tempo, with the same bars and starting together", "Ableton Link: Ableton y las demás apps Link toman el tempo de cada loop, con los mismos compases y arrancando juntos", "Ableton Link: o Ableton e os outros apps Link pegam o andamento de cada loop, com os mesmos compassos e começando juntos"}},
    {"Égaliseur, compresseur et limiteur du mix (ouvre la page)", {"EQ, compressor and limiter on the mix (opens the page)", "Ecualizador, compresor y limitador de la mezcla (abre la página)", "Equalizador, compressor e limitador da mixagem (abre a página)"}},
    {"Réverbération et envoi de chaque piste (ouvre la page)", {"Reverb and send for each track (opens the page)", "Reverb y envío de cada pista (abre la página)", "Reverb e envio de cada faixa (abre a página)"}},
    {"Chorus, flanger, phaser, trémolo et écho (ouvre la page)", {"Chorus, flanger, phaser, tremolo and echo (opens the page)", "Chorus, flanger, phaser, trémolo y eco (abre la página)", "Chorus, flanger, phaser, trêmolo e eco (abre a página)"}},
    {"Fondus et fondu enchaîné (ouvre la page)", {"Fades and crossfade (opens the page)", "Fundidos y fundido cruzado (abre la página)", "Fades e crossfade (abre a página)"}},
    {"Enregistrer tout : la loop, le tempo, le mixer et les effets", {"Save everything: the loop, tempo, mixer and effects", "Guardar todo: el loop, el tempo, el mezclador y los efectos", "Salvar tudo: o loop, o andamento, o mixer e os efeitos"}},
    {"Rappeler un setup enregistré", {"Recall a saved setup", "Cargar un setup guardado", "Carregar um setup salvo"}},
    {"Filtre passe-bas : enlève les aigus", {"Low-pass filter: removes highs", "Filtro paso bajo: quita los agudos", "Filtro passa-baixa: tira os agudos"}},
    {"Filtre passe-haut : enlève les graves", {"High-pass filter: removes lows", "Filtro paso alto: quita los graves", "Filtro passa-alta: tira os graves"}},
    {"Chaleur : saturation douce", {"Warmth: soft saturation", "Calidez: saturación suave", "Calor: saturação suave"}},
    {"Gauche : vitesse (Double, Half). Droite : mesure — 3/4 rejoue chaque mesure en valse, 6/8 lui donne un balancement ternaire", {"Left: speed (Double, Half). Right: time signature — 3/4 replays each bar as a waltz, 6/8 gives it a triplet swing", "Izquierda: velocidad (Double, Half). Derecha: compás — 3/4 toca cada compás como un vals, 6/8 le da un balanceo ternario", "Esquerda: velocidade (Double, Half). Direita: compasso — 3/4 toca cada compasso como valsa, 6/8 dá um balanço ternário"}},
    {"Ouvrir les paramètres (langue)", {"Open settings (language)", "Abrir ajustes (idioma)", "Abrir configurações (idioma)"}},
    {"LANGUE", {"LANGUAGE", "IDIOMA", "IDIOMA"}},
    {"ENREGISTRER SETUP", {"SAVE SETUP", "GUARDAR SETUP", "SALVAR SETUP"}},
    {"MES SETUPS  ▾", {"MY SETUPS  ▾", "MIS SETUPS  ▾", "MEUS SETUPS  ▾"}},
    {"Mon setup", {"My setup", "Mi setup", "Meu setup"}},
    {"Enregistrer le setup", {"Save setup", "Guardar el setup", "Salvar o setup"}},
    {"Nom du setup (la loop, le tempo, le mixer et les effets sont gardés) :", {"Setup name (the loop, tempo, mixer and effects are kept):", "Nombre del setup (se guardan el loop, el tempo, el mezclador y los efectos):", "Nome do setup (o loop, o andamento, o mixer e os efeitos são salvos):"}},
    {"Setup existant", {"Existing setup", "Setup existente", "Setup existente"}},
    {"Remplacer le setup « ", {"Replace setup « ", "¿Reemplazar el setup « ", "Substituir o setup « "}},
    {" » ?", {" »?", " »?", " »?"}},
    {"CHARGER UN SETUP", {"LOAD A SETUP", "CARGAR UN SETUP", "CARREGAR UM SETUP"}},
    {"Aucun setup enregistré pour l'instant", {"No setup saved yet", "Aún no hay setups guardados", "Nenhum setup salvo ainda"}},
    {"Ouvrir le dossier des setups", {"Open the setups folder", "Abrir la carpeta de setups", "Abrir a pasta dos setups"}},
    {"Supprimer le setup", {"Delete setup", "Eliminar el setup", "Excluir o setup"}},
    {"Supprimer « ", {"Delete « ", "¿Eliminar « ", "Excluir « "}},
    {"Renommer le setup", {"Rename setup", "Renombrar el setup", "Renomear o setup"}},
    {" Hz  -  8 pistes", {" Hz  -  8 tracks", " Hz  -  8 pistas", " Hz  -  8 faixas"}},
    {"  -  Ableton Link : ", {"  -  Ableton Link: ", "  -  Ableton Link: ", "  -  Ableton Link: "}},
    {"en attente d'un autre logiciel", {"waiting for another app", "esperando otra app", "aguardando outro app"}},
    {" logiciels", {" apps", " apps", " apps"}},
    {" logiciel", {" app", " app", " app"}},
    {"  -  calé sur le logiciel hôte", {"  -  synced to the host", "  -  sincronizado con el host", "  -  sincronizado com o host"}},
    {"PRESETS / BIBLIOTHÈQUE", {"PRESETS / LIBRARY", "PRESETS / BIBLIOTECA", "PRESETS / BIBLIOTECA"}},
    {"RETOUR AU MIXER", {"BACK TO MIXER", "VOLVER AL MEZCLADOR", "VOLTAR AO MIXER"}},
    {"✕  RETOUR AU MIXER", {"✕  BACK TO MIXER", "✕  VOLVER AL MEZCLADOR", "✕  VOLTAR AO MIXER"}},
    {"SEND FX  ·  RÉVERB", {"SEND FX  ·  REVERB", "SEND FX  ·  REVERB", "SEND FX  ·  REVERB"}},
    {"MOD FX  ·  ÉCHO", {"MOD FX  ·  ECHO", "MOD FX  ·  ECO", "MOD FX  ·  ECO"}},
    {"EQ PAR PISTE", {"TRACK EQ", "EQ POR PISTA", "EQ POR FAIXA"}},
    {"COMPRESSEUR + LIMITEUR : ACTIFS", {"COMPRESSOR + LIMITER: ON", "COMPRESOR + LIMITADOR: ACTIVOS", "COMPRESSOR + LIMITADOR: ATIVOS"}},
    {"Graves", {"Bass", "Graves", "Graves"}},
    {"Bas-médium", {"Low-mid", "Medios-graves", "Médio-grave"}},
    {"Haut-médium", {"High-mid", "Medios-agudos", "Médio-agudo"}},
    {"Aigus", {"Treble", "Agudos", "Agudos"}},
    {"Médium", {"Mid", "Medios", "Médios"}},
    {"Fréq. graves", {"Bass freq.", "Frec. graves", "Freq. graves"}},
    {"Seuil", {"Threshold", "Umbral", "Limiar"}},
    {"Ratio", {"Ratio", "Ratio", "Taxa"}},
    {"Attaque", {"Attack", "Ataque", "Ataque"}},
    {"Relâchement", {"Release", "Liberación", "Liberação"}},
    {"Plafond", {"Ceiling", "Techo", "Teto"}},
    {"Gain", {"Gain", "Ganancia", "Ganho"}},
    {"ACTIVE", {"ON", "ACTIVA", "ATIVA"}},
    {"ACTIF", {"ON", "ACTIVO", "ATIVO"}},
    {"ACTIFS", {"ON", "ACTIVOS", "ATIVOS"}},
    {"SALLE", {"HALL", "SALA", "SALA"}},
    {"ÉGLISE", {"CHURCH", "IGLESIA", "IGREJA"}},
    {"PLAQUE", {"PLATE", "PLACA", "PLACA"}},
    {"Niveau", {"Level", "Nivel", "Nível"}},
    {"Largeur", {"Width", "Anchura", "Largura"}},
    {"Taille", {"Size", "Tamaño", "Tamanho"}},
    {"Amorti", {"Damping", "Amortiguación", "Amortecimento"}},
    {"Pré-délai", {"Pre-delay", "Pre-retardo", "Pré-atraso"}},
    {"Coupe-bas", {"Low cut", "Corte graves", "Corte graves"}},
    {"Coupe-haut", {"High cut", "Corte agudos", "Corte agudos"}},
    {"TRÉMOLO", {"TREMOLO", "TRÉMOLO", "TRÊMOLO"}},
    {"Vitesse", {"Rate", "Velocidad", "Velocidade"}},
    {"Profondeur", {"Depth", "Profundidad", "Profundidade"}},
    {"Retour", {"Feedback", "Realimentación", "Realimentação"}},
    {"Mix", {"Mix", "Mezcla", "Mix"}},
    {"1/8 POINTÉE", {"1/8 DOTTED", "1/8 CON PUNTILLO", "1/8 PONTUADA"}},
    {"Entrée", {"Fade in", "Entrada", "Entrada"}},
    {"Sortie", {"Fade out", "Salida", "Saída"}},
    {"Durée", {"Length", "Duración", "Duração"}},
    {"ÉGALISEUR 4 BANDES  ·  MASTER", {"4-BAND EQ  ·  MASTER", "ECUALIZADOR 4 BANDAS  ·  MASTER", "EQUALIZADOR 4 BANDAS  ·  MASTER"}},
    {"MODULATION", {"MODULATION", "MODULACIÓN", "MODULAÇÃO"}},
    {"COMPRESSEUR", {"COMPRESSOR", "COMPRESOR", "COMPRESSOR"}},
    {"LIMITEUR", {"LIMITER", "LIMITADOR", "LIMITADOR"}},
    {"RÉDUCTION", {"REDUCTION", "REDUCCIÓN", "REDUÇÃO"}},
    {"RÉVERBÉRATION", {"REVERB", "REVERBERACIÓN", "REVERBERAÇÃO"}},
    {"RÉGLAGES DE LA RÉVERBÉRATION", {"REVERB SETTINGS", "AJUSTES DE LA REVERB", "AJUSTES DA REVERB"}},
    {"ENVOI DE CHAQUE PISTE VERS LA RÉVERB", {"SEND FROM EACH TRACK TO THE REVERB", "ENVÍO DE CADA PISTA A LA REVERB", "ENVIO DE CADA FAIXA PARA A REVERB"}},
    {"ÉCHO CALÉ SUR LE TEMPO", {"TEMPO-SYNCED ECHO", "ECO SINCRONIZADO AL TEMPO", "ECO SINCRONIZADO AO ANDAMENTO"}},
    {"PISTE", {"TRACK", "PISTA", "FAIXA"}},
    {"GRAVES", {"BASS", "GRAVES", "GRAVES"}},
    {"MÉDIUM", {"MID", "MEDIOS", "MÉDIOS"}},
    {"AIGUS", {"TREBLE", "AGUDOS", "AGUDOS"}},
    {"FONDUS AU DÉPART ET À L'ARRÊT", {"FADE IN AND FADE OUT", "FUNDIDOS AL INICIAR Y AL PARAR", "FADES NO INÍCIO E NA PARADA"}},
    {"FONDU ENCHAÎNÉ ENTRE DEUX LOOPS", {"CROSSFADE BETWEEN TWO LOOPS", "FUNDIDO CRUZADO ENTRE DOS LOOPS", "CROSSFADE ENTRE DOIS LOOPS"}},
    {"Quand vous choisissez une autre loop pendant la lecture, l'ancienne s'efface pendant que la nouvelle monte, à partir de la mesure suivante.", {"When you pick another loop during playback, the old one fades out while the new one fades in, from the next bar.", "Cuando eliges otro loop durante la reproducción, el anterior se desvanece mientras el nuevo sube, desde el compás siguiente.", "Quando você escolhe outro loop durante a reprodução, o antigo some enquanto o novo entra, a partir do compasso seguinte."}},
    {"PRÉ-DÉLAI ", {"PRE-DELAY ", "PRE-RETARDO ", "PRÉ-ATRASO "}},
    {"DURÉE ≈ ", {"LENGTH ≈ ", "DURACIÓN ≈ ", "DURAÇÃO ≈ "}},
    {"ENTRÉE", {"IN", "ENTRADA", "ENTRADA"}},
    {"SORTIE", {"OUT", "SALIDA", "SAÍDA"}},
    {" mesure", {" bar", " compás", " compasso"}},
    {" mesures", {" bars", " compases", " compassos"}},
    {"ce MP3 ne contient pas de son lisible (fichier abîmé ou mal nommé)", {"this MP3 has no readable sound (damaged or misnamed file)", "este MP3 no contiene sonido legible (archivo dañado o mal nombrado)", "este MP3 não contém som legível (arquivo danificado ou mal nomeado)"}},
    {"le décodeur MP3 n'arrive pas à lire ce fichier", {"the MP3 decoder cannot read this file", "el decodificador MP3 no puede leer este archivo", "o decodificador MP3 não consegue ler este arquivo"}},
    {"MP3 vide ou abîmé", {"empty or damaged MP3", "MP3 vacío o dañado", "MP3 vazio ou danificado"}},
    {"lecture du MP3 impossible", {"cannot read the MP3", "no se puede leer el MP3", "não é possível ler o MP3"}},
    {"MP3 vide", {"empty MP3", "MP3 vacío", "MP3 vazio"}},
    {"fichier introuvable", {"file not found", "archivo no encontrado", "arquivo não encontrado"}},
    {"fichier trop gros pour une loop", {"file too large for a loop", "archivo demasiado grande para un loop", "arquivo grande demais para um loop"}},
    {"impossible d'ouvrir le fichier", {"cannot open the file", "no se puede abrir el archivo", "não é possível abrir o arquivo"}},
    {"format non reconnu (WAV ou MP3)", {"unrecognised format (WAV or MP3)", "formato no reconocido (WAV o MP3)", "formato não reconhecido (WAV ou MP3)"}},
    {"lecture impossible", {"cannot read", "lectura imposible", "leitura impossível"}},
    {"fichier illisible", {"unreadable file", "archivo ilegible", "arquivo ilegível"}},
    {"ce fichier n'est pas un WAV valide", {"this is not a valid WAV file", "este archivo no es un WAV válido", "este arquivo não é um WAV válido"}},
    {"WAV invalide (fmt)", {"invalid WAV (fmt)", "WAV no válido (fmt)", "WAV inválido (fmt)"}},
    {"WAV invalide (données manquantes)", {"invalid WAV (missing data)", "WAV no válido (faltan datos)", "WAV inválido (dados ausentes)"}},
    {"WAV invalide (format)", {"invalid WAV (format)", "WAV no válido (formato)", "WAV inválido (formato)"}},
    {"WAV compressé abîmé", {"damaged compressed WAV", "WAV comprimido dañado", "WAV comprimido danificado"}},
    {"dossier introuvable", {"folder not found", "carpeta no encontrada", "pasta não encontrada"}},
    {"impossible d'écrire l'index dans le dossier", {"cannot write the index in the folder", "no se puede escribir el índice en la carpeta", "não é possível gravar o índice na pasta"}},
};

using Table = std::unordered_map<juce::String, const Row*>;

const Table& table() {
    static const Table t = [] {
        Table m;
        for (const auto& r : kRows) m[juce::String::fromUTF8(r.fr)] = &r;
        return m;
    }();
    return t;
}

juce::File settingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("African Loops")
        .getChildFile("langue.txt");
}

Id fromCode(const juce::String& code) {
    const auto c = code.trim().toLowerCase().substring(0, 2);
    if (c == "fr") return Fr;
    if (c == "es") return Es;
    if (c == "pt") return Pt;
    if (c == "en") return En;
    return Count;
}

const char* code(Id l) {
    switch (l) {
        case En: return "en";
        case Es: return "es";
        case Pt: return "pt";
        case Fr:
        case Count: break;
    }
    return "fr";
}

std::atomic<int>& state() {
    static std::atomic<int> s{[] {
        // choix déjà fait sur cet ordinateur, sinon la langue du système
        auto l = fromCode(settingsFile().loadFileAsString());
        if (l == Count) l = fromCode(juce::SystemStats::getUserLanguage());
        if (l == Count) l = En;
        return (int)l;
    }()};
    return s;
}

}  // namespace

Id current() { return (Id)state().load(); }

void set(Id l) {
    if (l < 0 || l >= Count || l == current()) return;
    state() = (int)l;
    auto f = settingsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(code(l));
}

juce::String nativeName(Id l) {
    switch (l) {
        case Fr: return juce::String::fromUTF8("Français");
        case En: return "English";
        case Es: return juce::String::fromUTF8("Español");
        case Pt: return juce::String::fromUTF8("Português");
        case Count: break;
    }
    return {};
}

juce::String tr(const juce::String& french) {
    const auto l = current();
    if (l == Fr || french.isEmpty()) return french;
    const auto& t = table();
    auto it = t.find(french);
    if (it == t.end()) return french;
    return juce::String::fromUTF8(it->second->other[l - 1]);
}

int tableSize() { return (int)(sizeof(kRows) / sizeof(kRows[0])); }

juce::StringArray missing() {
    juce::StringArray out;
    for (const auto& r : kRows)
        for (auto* o : r.other)
            if (o == nullptr || *o == 0) out.add(juce::String::fromUTF8(r.fr));
    return out;
}

}  // namespace lang
