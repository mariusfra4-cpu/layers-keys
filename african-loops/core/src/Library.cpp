#include "Internal.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <variant>

namespace fs = std::filesystem;

namespace africanloops {

const char* const kUncategorized = "Non classés";
const char* const kIndexFile = ".africanloops-index.json";
constexpr int kIndexVersion = 4;  // 2 : coups gardés ; 3 : boucle préparée ; 4 : tempo calé sur la longueur exacte

// ---------------------------------------------------------------- styles

namespace {

// minuscules sans accents, séparateurs → espaces (« Coupé_Décalé » → « coupe decale »)
std::string plainLower(const std::string& s) {
    static const std::pair<const char*, const char*> acc[] = {
        {"à", "a"}, {"â", "a"}, {"ä", "a"}, {"á", "a"}, {"ã", "a"}, {"ç", "c"}, {"é", "e"}, {"è", "e"},
        {"ê", "e"}, {"ë", "e"}, {"î", "i"}, {"ï", "i"}, {"í", "i"}, {"ì", "i"}, {"ô", "o"}, {"ö", "o"},
        {"ó", "o"}, {"ò", "o"}, {"õ", "o"}, {"ù", "u"}, {"û", "u"}, {"ü", "u"}, {"ú", "u"}, {"ñ", "n"},
        {"ÿ", "y"}, {"œ", "oe"}, {"æ", "ae"}, {"À", "a"}, {"É", "e"}, {"È", "e"}, {"Ç", "c"}, {"Ô", "o"},
    };
    std::string r;
    for (size_t i = 0; i < s.size();) {
        bool done = false;
        for (auto& [from, to] : acc) {
            size_t n = std::strlen(from);
            if (s.compare(i, n, from) == 0) { r += to; i += n; done = true; break; }
        }
        if (done) continue;
        unsigned char c = (unsigned char)s[i++];
        r += (c == '_' || c == '.' || std::isspace(c)) ? ' ' : (char)std::tolower(c);
    }
    std::string out;  // espaces multiples → un seul
    for (char c : r)
        if (!(c == ' ' && (out.empty() || out.back() == ' '))) out += c;
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

struct Style { std::vector<const char*> words; const char* name; };
const std::vector<Style>& styles() {
    static const std::vector<Style> s = {
        {{"worship", "louange", "adoration", "praise"}, "Worship"},
        {{"gospel"}, "Gospel"},
        {{"amapiano", "log drum", "logdrum"}, "Amapiano"},
        {{"afrohouse", "afro house"}, "Afro House"},
        {{"afrobeat", "afropop", "afro pop", "naija"}, "Afrobeats"},
        {{"coupe decale", "coupe-decale", "coupedecale", "decale"}, "Coupé-Décalé"},
        {{"ndombolo", "soukous", "sebene"}, "Ndombolo"},
        {{"rumba"}, "Rumba"}, {{"makossa"}, "Makossa"}, {{"bikutsi"}, "Bikutsi"},
        {{"highlife"}, "Highlife"}, {{"mbalax"}, "Mbalax"}, {{"zouk"}, "Zouk"},
        {{"kizomba"}, "Kizomba"}, {{"gqom"}, "Gqom"}, {{"azonto"}, "Azonto"},
        {{"bongo flava", "bongo"}, "Bongo Flava"},
    };
    return s;
}

std::string styleFromText(const std::string& s) {
    std::string p = " " + plainLower(s) + " ";
    for (const auto& st : styles())
        for (const char* w : st.words)
            if (p.find(w) != std::string::npos) return st.name;
    return "";
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

}  // namespace

std::string categoryOf(const std::string& rel) {
    std::string p = rel;
    std::replace(p.begin(), p.end(), '\\', '/');
    auto slash = p.find('/');
    if (slash != std::string::npos) return trim(p.substr(0, slash));  // le nom du dossier décide : « Worship/… » → Worship
    auto dot = p.find_last_of('.');
    if (auto c = styleFromText(dot == std::string::npos ? p : p.substr(0, dot)); !c.empty()) return c;
    return kUncategorized;
}

// ---------------------------------------------------------------- JSON (index)

namespace {

struct Json;
using JsonObj = std::map<std::string, Json>;
using JsonArr = std::vector<Json>;
struct Json {
    std::variant<std::nullptr_t, bool, double, std::string, JsonArr, JsonObj> v;
    const Json& operator[](const std::string& k) const {
        static const Json none{};
        if (auto* o = std::get_if<JsonObj>(&v)) { auto it = o->find(k); if (it != o->end()) return it->second; }
        return none;
    }
    double num(double d = 0) const { auto* p = std::get_if<double>(&v); return p ? *p : d; }
    bool flag() const { auto* p = std::get_if<bool>(&v); return p && *p; }
    std::string str() const { auto* p = std::get_if<std::string>(&v); return p ? *p : ""; }
    const JsonArr& arr() const { static const JsonArr e; auto* p = std::get_if<JsonArr>(&v); return p ? *p : e; }
};

struct Parser {
    const std::string& s;
    size_t i = 0;
    bool ok = true;
    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    std::string string() {
        std::string r;
        if (!eat('"')) { ok = false; return r; }
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                    case 'n': r += '\n'; break;
                    case 't': r += '\t'; break;
                    case 'r': r += '\r'; break;
                    case 'b': r += '\b'; break;
                    case 'f': r += '\f'; break;
                    case 'u': {
                        unsigned cp = (unsigned)std::stoul(s.substr(i, 4), nullptr, 16);
                        i += 4;
                        if (cp < 0x80) r += (char)cp;
                        else if (cp < 0x800) { r += (char)(0xC0 | cp >> 6); r += (char)(0x80 | (cp & 63)); }
                        else { r += (char)(0xE0 | cp >> 12); r += (char)(0x80 | ((cp >> 6) & 63)); r += (char)(0x80 | (cp & 63)); }
                        break;
                    }
                    default: r += e;
                }
            } else r += c;
        }
        ++i;
        return r;
    }
    Json value() {
        ws();
        if (i >= s.size()) { ok = false; return {}; }
        char c = s[i];
        if (c == '{') {
            ++i; JsonObj o;
            if (eat('}')) return {o};
            do { ws(); std::string k = string(); if (!eat(':')) { ok = false; return {}; } o[k] = value(); } while (ok && eat(','));
            if (!eat('}')) ok = false;
            return {o};
        }
        if (c == '[') {
            ++i; JsonArr a;
            if (eat(']')) return {a};
            do a.push_back(value()); while (ok && eat(','));
            if (!eat(']')) ok = false;
            return {a};
        }
        if (c == '"') return {string()};
        if (!s.compare(i, 4, "true")) { i += 4; return {true}; }
        if (!s.compare(i, 5, "false")) { i += 5; return {false}; }
        if (!s.compare(i, 4, "null")) { i += 4; return {}; }
        char* end = nullptr;
        double d = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) { ok = false; return {}; }
        i = (size_t)(end - s.c_str());
        return {d};
    }
};

std::string q(const std::string& s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { r += '\\'; r += (char)c; }
        else if (c == '\n') r += "\\n";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); r += b; }
        else r += (char)c;
    }
    return r + "\"";
}

std::string n(double d) {
    if (!std::isfinite(d)) return "0";
    std::ostringstream o;
    o.precision(10);
    o << d;
    return o.str();
}

int64_t mtimeOf(const fs::directory_entry& e) {
    std::error_code ec;
    auto t = e.last_write_time(ec);
    if (ec) return 0;
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

std::string toUtf8(const fs::path& p) { return detail::pathToUtf8(p); }
fs::path fromUtf8(const std::string& s) { return detail::pathFromUtf8(s); }

}  // namespace

bool Library::save() const {
    std::ostringstream o;
    o << "{\n \"version\": " << kIndexVersion << ",\n \"root\": " << q(root) << ",\n \"styles\": [";
    for (size_t k = 0; k < styles.size(); ++k) o << (k ? ", " : "") << q(styles[k]);
    o << "],\n \"entries\": [";
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const auto& t = e.info.tempo;
        o << (i ? ",\n  " : "\n  ") << "{\"path\": " << q(e.path) << ", \"category\": " << q(e.category)
          << (e.favorite ? ", \"favorite\": true" : "")
          << ", \"size\": " << e.size << ", \"mtime\": " << e.mtime;
        if (!e.error.empty()) o << ", \"error\": " << q(e.error);
        o << ", \"info\": {\"tempo\": {\"bpm\": " << n(t.bpm) << ", \"beats\": " << t.beats << ", \"bars\": " << t.bars
          << ", \"source\": " << q(t.source) << ", \"confidence\": " << n(t.confidence) << ", \"alternatives\": [";
        for (size_t k = 0; k < t.alternatives.size(); ++k) o << (k ? ", " : "") << n(t.alternatives[k]);
        o << "]}, \"seconds\": " << n(e.info.seconds) << ", \"rate\": " << e.info.rate << ", \"percussions\": [";
        for (size_t k = 0; k < e.info.percussions.size(); ++k) {
            const auto& p = e.info.percussions[k];
            o << (k ? ", " : "") << "{\"kind\": " << q(p.kind) << ", \"hits\": " << p.hits << ", \"share\": " << n(p.share) << "}";
        }
        o << "], \"slices\": [";
        for (size_t k = 0; k < e.info.slices.size(); ++k) o << (k ? "," : "") << n(e.info.slices[k]);
        // chaque coup : [temps, familles en bits] (kick = 1, snare = 2, hi-hat = 4, djembé = 8, talking = 16)
        o << "], \"hits\": [";
        for (size_t k = 0; k < e.info.hits.size(); ++k) {
            int mask = 0;
            for (const auto& kind : e.info.hits[k].kinds)
                for (size_t b = 0; b < percussionOrder().size(); ++b)
                    if (percussionOrder()[b] == kind) mask |= 1 << b;
            o << (k ? "," : "") << "[" << n(e.info.hits[k].time) << "," << mask << "]";
        }
        o << "]}}";
    }
    o << "\n ]\n}\n";
    std::error_code ec;
    fs::path dir = fromUtf8(root), tmp = dir / (std::string(kIndexFile) + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) return false;
        f << o.str();
        if (!f) return false;
    }
    fs::rename(tmp, dir / kIndexFile, ec);
    return !ec;
}

bool Library::loadIndex(const std::string& root, Library& out) {
    std::ifstream f(fromUtf8(root) / kIndexFile, std::ios::binary);
    if (!f) return false;
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Parser p{s};
    Json j = p.value();
    if (!p.ok) return false;
    out = Library{};
    out.root = root;
    for (const auto& js : j["styles"].arr())
        if (!js.str().empty()) out.styles.push_back(js.str());
    for (const auto& je : j["entries"].arr()) {
        Entry e;
        e.path = je["path"].str();
        e.category = je["category"].str();
        e.favorite = je["favorite"].flag();
        e.size = (int64_t)je["size"].num();
        e.mtime = (int64_t)je["mtime"].num();
        e.error = je["error"].str();
        const auto& ji = je["info"];
        const auto& jt = ji["tempo"];
        e.info.tempo.bpm = jt["bpm"].num();
        e.info.tempo.beats = (int)jt["beats"].num();
        e.info.tempo.bars = (int)jt["bars"].num();
        e.info.tempo.source = jt["source"].str();
        e.info.tempo.confidence = jt["confidence"].num();
        for (const auto& a : jt["alternatives"].arr()) e.info.tempo.alternatives.push_back(a.num());
        e.info.seconds = ji["seconds"].num();
        e.info.rate = (int)ji["rate"].num();
        for (const auto& jp : ji["percussions"].arr())
            e.info.percussions.push_back({jp["kind"].str(), (int)jp["hits"].num(), jp["share"].num()});
        for (const auto& s2 : ji["slices"].arr()) e.info.slices.push_back(s2.num());
        for (const auto& jh : ji["hits"].arr()) {
            const auto& pair = jh.arr();
            if (pair.size() != 2) continue;
            Hit h;
            h.time = pair[0].num();
            const int mask = (int)pair[1].num();
            for (size_t b = 0; b < percussionOrder().size(); ++b)
                if (mask & (1 << b)) h.kinds.push_back(percussionOrder()[b]);
            if (!h.kinds.empty()) e.info.hits.push_back(std::move(h));
        }
        // index d'une ancienne version : la loop sera analysée à nouveau
        if ((int)j["version"].num() < kIndexVersion) e.size = -1;
        if (!e.path.empty()) out.entries.push_back(std::move(e));
    }
    return true;
}

bool Library::scan(const std::string& rootIn, Library& lib, std::string& error, Progress progress, AudioLoader loader,
                   int threads) {
    std::error_code ec;
    fs::path root = fs::absolute(fromUtf8(rootIn), ec);
    if (ec || !fs::is_directory(root, ec)) { error = "dossier introuvable"; return false; }
    if (!loader) loader = loadWAV;
    std::map<std::string, Entry> old;
    std::vector<std::string> oldStyles;
    if (Library prev; loadIndex(toUtf8(root), prev)) {
        for (auto& e : prev.entries) old[e.path] = std::move(e);
        oldStyles = prev.styles;
    }

    lib = Library{};
    lib.root = toUtf8(root);
    lib.styles = oldStyles;
    std::vector<size_t> todo;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }  // dossier illisible : on continue
        const auto& de = *it;
        std::string name = toUtf8(de.path().filename());
        if (de.is_directory(ec)) {
            if (!name.empty() && (name[0] == '.' || name == "__MACOSX")) it.disable_recursion_pending();
            continue;
        }
        // « ._Groove.wav » : petit fichier caché créé par les Mac à côté de chaque loop, sans son
        if (!isAudioFile(name) || name.rfind("._", 0) == 0) continue;
        Entry e;
        e.path = toUtf8(fs::relative(de.path(), root, ec));
        e.category = categoryOf(e.path);
        e.size = (int64_t)de.file_size(ec);
        e.mtime = mtimeOf(de);
        auto o = old.find(e.path);
        if (o != old.end()) {
            e.favorite = o->second.favorite;  // l'étoile reste, même si le fichier a changé
            if (!o->second.category.empty()) e.category = o->second.category;  // style choisi à la main : gardé aussi
        }
        if (o != old.end() && o->second.size == e.size && o->second.mtime == e.mtime && o->second.error.empty()) {
            e.info = o->second.info;
        } else {
            todo.push_back(lib.entries.size());
        }
        lib.entries.push_back(std::move(e));
    }

    // analyse en parallèle sur la moitié des cœurs (le reste pour le son)
    unsigned workers = threads > 0 ? (unsigned)threads : std::max(1u, std::thread::hardware_concurrency() / 2);
    std::atomic<size_t> next{0};
    std::mutex mu;
    int done = 0;
    auto work = [&] {
        for (size_t j; (j = next++) < todo.size();) {
            Entry& e = lib.entries[todo[j]];
            Audio a;
            std::string err;
            if (!loader(toUtf8(root / fromUtf8(e.path)), a, err)) {
                e.error = err.empty() ? "fichier illisible" : err;
            } else {
                e.info = analyse(a, e.path);
            }
            std::lock_guard<std::mutex> lk(mu);
            ++done;
            if (progress) progress(done, (int)todo.size(), e.path);
        }
    };
    std::vector<std::thread> pool;
    for (unsigned w = 1; w < workers; ++w) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();

    auto lower = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    std::sort(lib.entries.begin(), lib.entries.end(), [&](const Entry& a, const Entry& b) {
        if (a.category != b.category) return a.category < b.category;
        return lower(a.path) < lower(b.path);
    });
    if (!lib.save()) { error = "impossible d'écrire l'index dans le dossier"; return false; }
    return true;
}

const Entry* Library::find(const std::string& absolutePath) const {
    std::error_code ec;
    fs::path p = fs::absolute(fromUtf8(absolutePath), ec), r = fs::absolute(fromUtf8(root), ec);
    const std::string rel = toUtf8(p.lexically_relative(r));
    if (rel.empty() || rel.rfind("..", 0) == 0) return nullptr;
    for (const auto& e : entries)
        if (e.path == rel) return &e;
    return nullptr;
}

bool Library::upToDate(const Entry& e) const {
    if (!e.error.empty() || e.size < 0 || e.info.tempo.source.empty()) return false;
    std::error_code ec;
    fs::directory_entry de(fromUtf8(root) / fromUtf8(e.path), ec);
    if (ec || !de.is_regular_file(ec)) return false;
    return (int64_t)de.file_size(ec) == e.size && mtimeOf(de) == e.mtime;
}

bool Library::setFavorite(const std::string& path, bool on) {
    for (auto& e : entries)
        if (e.path == path) { e.favorite = on; return true; }
    return false;
}

int Library::favoriteCount() const {
    int n = 0;
    for (const auto& e : entries) n += e.favorite ? 1 : 0;
    return n;
}

bool Library::addStyle(const std::string& name) {
    const std::string n = trim(name);
    if (n.empty() || n == kUncategorized) return false;
    for (const auto& c : categories())
        if (c.name == n) return false;
    styles.push_back(n);
    return true;
}

std::vector<CategoryCount> Library::categories() const {
    std::map<std::string, int> count;
    for (const auto& s : styles) count[s] += 0;  // styles créés à la main, même vides
    for (const auto& e : entries) count[e.category]++;
    std::vector<CategoryCount> out;
    for (auto& [name, c] : count)
        if (name != kUncategorized) out.push_back({name, c});
    if (auto it = count.find(kUncategorized); it != count.end()) out.push_back({it->first, it->second});  // à la fin
    return out;
}

int Library::renameCategory(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from == kUncategorized) return 0;
    int n = 0;
    for (auto& e : entries)
        if (e.category == from) { e.category = to; ++n; }
    bool known = false;
    for (auto& s : styles) {
        if (s == from) { s = to; ++n; }
        known |= s == to;
    }
    if (n > 0 && !known && to != kUncategorized) styles.push_back(to);  // le nouveau nom reste même si le style se vide
    styles.erase(std::remove(styles.begin(), styles.end(), std::string(kUncategorized)), styles.end());
    std::sort(styles.begin(), styles.end());
    styles.erase(std::unique(styles.begin(), styles.end()), styles.end());
    return n;
}

int Library::removeCategory(const std::string& name) {
    if (name == kUncategorized) return 0;
    int n = 0;
    for (auto& e : entries)
        if (e.category == name) { e.category = kUncategorized; ++n; }  // les loops restent, dans « Non classés »
    const auto before = styles.size();
    styles.erase(std::remove(styles.begin(), styles.end(), name), styles.end());
    return n + (int)(before - styles.size());
}

bool Library::setCategory(const std::string& path, const std::string& category) {
    for (auto& e : entries)
        if (e.path == path) { e.category = category; return true; }
    return false;
}

std::vector<const Entry*> Library::search(const std::string& category, const std::string& text) const {
    const std::string qt = plainLower(text);
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (!category.empty() && e.category != category) continue;
        if (!qt.empty() && plainLower(e.path).find(qt) == std::string::npos) continue;
        out.push_back(&e);
    }
    return out;
}

}  // namespace africanloops
