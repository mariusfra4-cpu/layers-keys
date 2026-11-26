#include "Licence.h"
#include "Library.h"
#include "Lang.h"
#include <juce_cryptography/juce_cryptography.h>
extern "C"
{
#include "monocypher-ed25519.h"
}
#if JUCE_MAC
 #include <IOKit/IOKitLib.h>
 #include <CoreFoundation/CoreFoundation.h>
#elif JUCE_WINDOWS
 #include <windows.h>
#endif

namespace spacepad
{
// clé publique de vérification (la même que Layers Keys : la clé secrète n'est que dans « JB Clés », chiffrée)
static const std::array<juce::uint8, 32> jbPublicKey { {
    0xa7, 0x9f, 0xd0, 0xbd, 0x77, 0xdf, 0x4f, 0xe5, 0xcd, 0xef, 0xc3, 0x66, 0xa4, 0xe0, 0x85, 0xe7,
    0xd3, 0x56, 0x97, 0x7a, 0x55, 0x68, 0xf1, 0x2f, 0x0f, 0xcd, 0xef, 0x7f, 0xa3, 0x94, 0x91, 0x8e } };

static const char* const msgPrefix = "Spacepad|";
static const char* const demoSecret = "JBPraise.Spacepad.demo.v1";
static const juce::int64 demoSeconds = 72 * 3600;

// ---------------------------------------------------------------------------------------------
static juce::String hexUpper (const juce::uint8* d, int n)
{
    return juce::String::toHexString (d, n, 0).toUpperCase();
}

static juce::MemoryBlock sha256 (const void* d, size_t n)
{
    juce::SHA256 h (d, n);
    return h.getRawData();
}

static juce::String hmacHex12 (const juce::String& key, const juce::String& msg)
{
    juce::MemoryBlock k (key.toRawUTF8(), key.getNumBytesAsUTF8());
    if (k.getSize() > 64) k = sha256 (k.getData(), k.getSize());
    k.ensureSize (64, true);
    juce::uint8 ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = (juce::uint8) (static_cast<const juce::uint8*> (k.getData())[i] ^ 0x36); opad[i] = (juce::uint8) (static_cast<const juce::uint8*> (k.getData())[i] ^ 0x5c); }
    juce::MemoryBlock inner (ipad, 64);
    inner.append (msg.toRawUTF8(), msg.getNumBytesAsUTF8());
    auto ih = sha256 (inner.getData(), inner.getSize());
    juce::MemoryBlock outer (opad, 64);
    outer.append (ih.getData(), ih.getSize());
    auto oh = sha256 (outer.getData(), outer.getSize());
    return juce::String::toHexString (oh.getData(), 12, 0).toLowerCase();
}

static const char* const b32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static juce::String base32 (const juce::uint8* d, int n)
{
    juce::String out;
    int buffer = 0, bits = 0;
    for (int i = 0; i < n; ++i)
    {
        buffer = (buffer << 8) | d[i]; bits += 8;
        while (bits >= 5) { out << b32[(buffer >> (bits - 5)) & 31]; bits -= 5; }
    }
    if (bits > 0) out << b32[(buffer << (5 - bits)) & 31];
    return out;
}

static bool unbase32 (const juce::String& s, juce::MemoryBlock& out)
{
    int buffer = 0, bits = 0;
    out.reset();
    for (auto c : s)
    {
        const char* p = std::strchr (b32, (char) c);
        if (c == 0 || p == nullptr) return false;
        buffer = (buffer << 5) | (int) (p - b32); bits += 5;
        if (bits >= 8) { const juce::uint8 v = (juce::uint8) ((buffer >> (bits - 8)) & 255); out.append (&v, 1); bits -= 8; }
    }
    return true;
}

static juce::String normaliseKey (const juce::String& k)
{
    return k.toUpperCase().removeCharacters (" -\r\n\t");
}

static juce::String formatKey (const juce::String& norm)
{
    juce::StringArray parts;
    for (int i = 0; i < norm.length(); i += 8) parts.add (norm.substring (i, i + 8));
    return parts.joinIntoString ("-");
}

// ---------------------------------------------------------------------------------------------
juce::String Licence::rawMachineId()
{
   #if JUCE_MAC
    // identifiant du Mac lu directement dans le système (IOKit) : aucun programme lancé (sûr dans Logic et GarageBand)
    juce::String id;
    io_service_t svc = IOServiceGetMatchingService (0, IOServiceMatching ("IOPlatformExpertDevice"));
    if (svc != 0)
    {
        if (auto prop = (CFStringRef) IORegistryEntryCreateCFProperty (svc, CFSTR ("IOPlatformUUID"), kCFAllocatorDefault, 0))
        {
            char buf[128] = {};
            if (CFStringGetCString (prop, buf, sizeof buf, kCFStringEncodingUTF8)) id = juce::String (buf).trim();
            CFRelease (prop);
        }
        IOObjectRelease (svc);
    }
    return id.isNotEmpty() ? "mac|" + id : juce::String ("mac-inconnu");
   #elif JUCE_WINDOWS
    // MachineGuid, lu dans la vue 64 bits (même code pour l'application et le plugin)
    HKEY h = nullptr;
    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &h) == ERROR_SUCCESS)
    {
        wchar_t buf[128] = {};
        DWORD size = sizeof (buf), type = 0;
        const auto r = RegQueryValueExW (h, L"MachineGuid", nullptr, &type, (LPBYTE) buf, &size);
        RegCloseKey (h);
        if (r == ERROR_SUCCESS) return "windows|" + juce::String (buf);
    }
    return "windows-inconnu|" + juce::SystemStats::getComputerName();
   #else
    auto id = juce::File ("/etc/machine-id").loadFileAsString().trim();
    return "linux|" + (id.isNotEmpty() ? id : juce::SystemStats::getComputerName());
   #endif
}

juce::Array<juce::File> Licence::defaultDirs()
{
    juce::Array<juce::File> d;
    d.add (LibraryStore::storageFolder());
   #if JUCE_MAC
    d.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/JB Praise/Spacepad"));
   #else
    d.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("JB Praise").getChildFile ("Spacepad"));
   #endif
    return d;
}

Licence::Licence()
{
    machineId = [] { return rawMachineId(); };
    dirs = [] { return defaultDirs(); };
    now = [] { return juce::Time::currentTimeMillis() / 1000; };
    publicKey = jbPublicKey;
}

juce::String Licence::code()
{
    const juce::ScopedLock sl (lock);
    if (codeCache.isEmpty())
    {
        const auto raw = "JBS1|" + machineId().trim().toLowerCase();
        auto h = sha256 (raw.toRawUTF8(), raw.getNumBytesAsUTF8());
        const auto x = hexUpper (static_cast<const juce::uint8*> (h.getData()), 8);
        codeCache = "JBS-" + x.substring (0, 4) + "-" + x.substring (4, 8) + "-" + x.substring (8, 12) + "-" + x.substring (12, 16);
    }
    return codeCache;
}

juce::String Licence::normaliseCode (const juce::String& in)
{
    auto s = in.toUpperCase().removeCharacters (" -\r\n\t");
    if (s.startsWith ("JBS")) s = "JBS" + s.substring (3).replaceCharacter ('O', '0');   // la lettre O lue à la place du zéro
    if (! s.startsWith ("JBS") || s.length() != 19) return {};
    if (! s.substring (3).containsOnly ("0123456789ABCDEF")) return {};
    return "JBS-" + s.substring (3, 7) + "-" + s.substring (7, 11) + "-" + s.substring (11, 15) + "-" + s.substring (15, 19);
}

bool Licence::verify (const std::array<juce::uint8, 32>& pub, const juce::String& code, const juce::String& key)
{
    const auto c = normaliseCode (code);
    if (c.isEmpty()) return false;
    juce::MemoryBlock sig;
    if (! unbase32 (normaliseKey (key), sig) || sig.getSize() != 64) return false;
    const auto msg = juce::String (msgPrefix) + c;
    return crypto_ed25519_check (static_cast<const juce::uint8*> (sig.getData()), pub.data(),
                                 reinterpret_cast<const juce::uint8*> (msg.toRawUTF8()), msg.getNumBytesAsUTF8()) == 0;
}

bool Licence::readLicence()
{
    const auto c = code();
    for (auto& d : dirs())
    {
        auto lines = juce::StringArray::fromTokens (d.getChildFile ("licence.dat").loadFileAsString(), true);
        lines.removeEmptyStrings();
        if (lines.size() >= 3 && lines[0] == "SP1" && normaliseCode (lines[1]) == c && verify (publicKey, c, lines[2])) return true;
    }
    return false;
}

juce::String Licence::activate (const juce::String& key)
{
    const auto c = code();
    const auto k = normaliseKey (key);
    if (k.isEmpty()) return L ("Collez d'abord la cl\xc3\xa9 de licence.", "Paste the licence key first.", "Pega primero la clave de licencia.");
    if (! verify (publicKey, c, k))
        return L ("Cette cl\xc3\xa9 n'est pas valable pour cet ordinateur. V\xc3\xa9rifiez qu'elle est compl\xc3\xa8te, ou redemandez-la avec le code affich\xc3\xa9.",
                  "This key is not valid for this computer. Check that it is complete, or ask again with the code shown.",
                  "Esta clave no es v\xc3\xa1lida para este ordenador. Comprueba que est\xc3\xa1 completa o vuelve a pedirla con el c\xc3\xb3""digo mostrado.");
    const auto content = "SP1\n" + c + "\n" + formatKey (k) + "\n";
    bool ok = false;
    for (auto& d : dirs()) ok = writeSharedFile (d.getChildFile ("licence.dat"), content) || ok;
    if (! ok) return L ("Impossible d'enregistrer la licence sur cet ordinateur (droits d'\xc3\xa9""criture refus\xc3\xa9s).", "Could not save the licence on this computer (write access denied).", "No se pudo guardar la licencia (escritura denegada).");
    const juce::ScopedLock sl (lock);
    licensed = checked = true;
    return {};
}

juce::String Licence::demoMac (juce::int64 start, juce::int64 last)
{
    return hmacHex12 (juce::String (demoSecret) + "|" + code(), juce::String (start) + "|" + juce::String (last));
}

void Licence::startDemo()
{
    for (auto& d : dirs()) if (d.getChildFile ("demo.dat").existsAsFile()) return;   // une seule démo par ordinateur
    const auto t = now();
    const auto content = "SPD1\n" + juce::String (t) + "\n" + juce::String (t) + "\n" + demoMac (t, t) + "\n";
    for (auto& d : dirs()) writeSharedFile (d.getChildFile ("demo.dat"), content);
}

Licence::Status Licence::status()
{
    {
        const juce::ScopedLock sl (lock);
        if (! checked || ! licensed) { licensed = readLicence(); checked = true; }
        if (licensed) return { State::Licensed, {}, false, false };
    }
    Status st;
    juce::int64 start = 0, last = 0;
    bool have = false;
    for (auto& d : dirs())
    {
        auto f = d.getChildFile ("demo.dat");
        if (! f.existsAsFile()) continue;
        st.demoUsed = true;
        auto t = juce::StringArray::fromTokens (f.loadFileAsString(), true);
        t.removeEmptyStrings();
        if (t.size() != 4 || t[0] != "SPD1") { st.tamper = true; continue; }
        const auto s = t[1].getLargeIntValue(), la = t[2].getLargeIntValue();
        if (t[3] != demoMac (s, la)) { st.tamper = true; continue; }
        if (! have || s < start) start = s;
        if (! have || la > last) last = la;
        have = true;
    }
    if (st.tamper || ! have) return st;
    auto t = now();
    if (last > t) t = last;                              // horloge remise en arrière : on garde la dernière heure connue
    const auto left = demoSeconds - (t - start);
    if (left <= 0) return st;
    st.state = State::Demo;
    st.demoLeft = juce::RelativeTime::seconds ((double) left);
    if (t - last >= 60)
    {
        const auto content = "SPD1\n" + juce::String (start) + "\n" + juce::String (t) + "\n" + demoMac (start, t) + "\n";
        for (auto& d : dirs()) if (d.getChildFile ("demo.dat").existsAsFile()) writeSharedFile (d.getChildFile ("demo.dat"), content);
    }
    return st;
}

juce::String Licence::formatDemoLeft (juce::RelativeTime r)
{
    const int m = juce::jmax (0, (int) r.inMinutes());
    if (m >= 24 * 60) return juce::String (m / (24 * 60)) + L (" j ", " d ", " d ") + juce::String ((m % (24 * 60)) / 60) + " h";
    if (m >= 60) return juce::String (m / 60) + " h " + juce::String (m % 60).paddedLeft ('0', 2) + " min";
    return juce::String (m + 1) + " min";
}
}
