#include "Licence.h"

#include "Lang.h"

#include "../licence/KeyCrypto.h"
#include "../licence/LicenceCore.h"

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
 #include <IOKit/IOKitLib.h>
#endif

namespace {
const char* kDemoSecret = "JBPraise.AfricanLoopsPro.demo.v1";
const char* kLicFile = "licence.dat";
const char* kDemoFile = "demo.dat";
}  // namespace

Licence& Licence::get() {
    static Licence l;
    return l;
}

Licence::Licence() { status(); }

juce::int64 Licence::now() const { return testNow ? testNow() : juce::Time::currentTimeMillis() / 1000; }

// identifiant interne de l'ordinateur (même lecture que Layers Keys)
juce::String Licence::machineId() {
    if (testMachine.isNotEmpty()) return testMachine;
#if JUCE_WINDOWS
    wchar_t buf[128] = {};
    DWORD size = sizeof(buf);
    // vue 64 bits : la version 32 bits, la version 64 bits et le VST3 donnent le même code
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) == ERROR_SUCCESS)
        return "windows|" + juce::String(buf);
    return "windows-inconnu|" + juce::SystemStats::getComputerName();
#elif JUCE_MAC
    juce::String id;
    io_service_t svc = IOServiceGetMatchingService(0, IOServiceMatching("IOPlatformExpertDevice"));
    if (svc != 0) {
        if (auto v = (CFStringRef)IORegistryEntryCreateCFProperty(svc, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0)) {
            char s[128] = {};
            if (CFStringGetCString(v, s, sizeof(s), kCFStringEncodingUTF8)) id = juce::String(s).trim();
            CFRelease(v);
        }
        IOObjectRelease(svc);
    }
    return id.isNotEmpty() ? "mac|" + id : juce::String("mac-inconnu");
#else
    auto id = juce::File("/etc/machine-id").loadFileAsString().trim();
    return id.isNotEmpty() ? "linux|" + id : juce::String("linux-inconnu|") + juce::SystemStats::getComputerName();
#endif
}

// dossier commun à l'ordinateur, puis dossier de l'utilisateur
juce::Array<juce::File> Licence::dirs() const {
    if (testDir != juce::File()) return {testDir.getChildFile("commun"), testDir.getChildFile("utilisateur")};
    juce::Array<juce::File> d;
#if JUCE_WINDOWS
    d.add(juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory).getChildFile("JB Praise").getChildFile("African Loops"));
#elif JUCE_MAC
    d.add(juce::File("/Library/Application Support/JB Praise/African Loops"));
#endif
    d.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("African Loops").getChildFile("Licence"));
    return d;
}

juce::String Licence::code() {
    std::lock_guard<std::mutex> lk(mu);
    if (cachedCode.isEmpty())
        cachedCode = juce::String(licence::formatCode(licence::africanLoops(), machineId().toStdString()));
    return cachedCode;
}

bool Licence::readLicence() {
    const auto c = cachedCode.toStdString();
    for (const auto& d : dirs()) {
        auto lines = juce::StringArray::fromTokens(d.getChildFile(kLicFile).loadFileAsString(), true);
        lines.removeEmptyStrings();
        if (lines.size() >= 3 && lines[0] == "AL1" && licence::normalizeCode(licence::africanLoops(), lines[1].toStdString()) == c &&
            licence::verifyKey(licence::africanLoops(), licence::kAfricanLoopsPublicKey.data(), c, lines[2].toStdString()))
            return true;
    }
    return false;
}

juce::String Licence::demoMac(juce::int64 start, juce::int64 last) {
    const std::string key = std::string(kDemoSecret) + "|" + cachedCode.toStdString();
    const std::string msg = std::to_string(start) + "|" + std::to_string(last);
    auto m = keycrypto::hmacSha256((const uint8_t*)key.data(), key.size(), (const uint8_t*)msg.data(), msg.size());
    return keycrypto::toHex(m.data(), 12);
}

bool Licence::readDemo(const juce::File& dir, Demo& d) {
    auto f = dir.getChildFile(kDemoFile);
    if (!f.existsAsFile()) return false;
    auto t = juce::StringArray::fromTokens(f.loadFileAsString(), true);
    t.removeEmptyStrings();
    d = {};
    if (t.size() != 4 || t[0] != "ALD1" || !t[1].containsOnly("0123456789") || !t[2].containsOnly("0123456789")) return true;
    d.start = t[1].getLargeIntValue();
    d.last = t[2].getLargeIntValue();
    d.ok = t[3] == demoMac(d.start, d.last);
    return true;
}

void Licence::writeDemo(juce::int64 start, juce::int64 last) {
    const auto content = "ALD1\n" + juce::String(start) + "\n" + juce::String(last) + "\n" + demoMac(start, last) + "\n";
    for (const auto& d : dirs())
        if (d.createDirectory()) d.getChildFile(kDemoFile).replaceWithText(content);
}

void Licence::startDemo() {
    {
        std::lock_guard<std::mutex> lk(mu);
        if (cachedCode.isEmpty()) cachedCode = juce::String(licence::formatCode(licence::africanLoops(), machineId().toStdString()));
        Demo d;
        for (const auto& dir : dirs())
            if (readDemo(dir, d)) return;  // une seule démo par ordinateur
        const auto t = now();
        writeDemo(t, t);
    }
    status();
}

juce::String Licence::activate(const juce::String& key) {
    const auto c = code();
    std::lock_guard<std::mutex> lk(mu);
    const auto k = licence::formatKey(key.toStdString());
    if (key.trim().isEmpty()) return juce::String::fromUTF8("Collez d'abord la clé de licence.");
    if (k.empty() || !licence::verifyKey(licence::africanLoops(), licence::kAfricanLoopsPublicKey.data(), c.toStdString(), k))
        return juce::String::fromUTF8("Cette clé n'est pas valable pour cet ordinateur. Vérifiez qu'elle est complète, "
                                      "ou redemandez-la avec le code affiché.");
    const auto content = "AL1\n" + c + "\n" + juce::String(k) + "\n";
    bool ok = false;
    for (const auto& d : dirs())
        if (d.createDirectory() && d.getChildFile(kLicFile).replaceWithText(content)) ok = true;
    if (!ok) return juce::String::fromUTF8("Impossible d'enregistrer la licence sur cet ordinateur (droits d'écriture refusés).");
    licensed = checked = true;
    playing = true;
    return {};
}

Licence::Status Licence::status() {
    if (cachedCode.isEmpty()) code();
    std::lock_guard<std::mutex> lk(mu);
    Status st;
    if (forced >= 0) {
        st.state = forced ? State::Licensed : State::Locked;
        playing = forced != 0;
        return st;
    }
    if (!checked || !licensed) {
        licensed = readLicence();
        checked = true;
    }
    if (licensed) {
        playing = true;
        st.state = State::Licensed;
        return st;
    }
    juce::int64 start = 0, last = 0;
    bool have = false;
    for (const auto& dir : dirs()) {
        Demo d;
        if (!readDemo(dir, d)) continue;
        st.demoUsed = true;
        if (!d.ok) {
            st.tamper = true;
            continue;
        }
        if (!have || d.start < start) start = d.start;
        if (!have || d.last > last) last = d.last;
        have = true;
    }
    if (st.tamper || !have) {
        playing = false;
        return st;
    }
    auto t = now();
    if (last > t) t = last;  // horloge remise en arrière : on garde la dernière heure connue
    const auto left = kDemoSeconds - (t - start);
    if (left <= 0) {
        playing = false;
        return st;
    }
    st.state = State::Demo;
    st.demoLeftSeconds = left;
    playing = true;
    if (t - last >= 60) writeDemo(start, t);  // l'horloge enregistrée avance (au plus une fois par minute)
    return st;
}

juce::String Licence::formatLeft(juce::int64 s) {
    const auto m = std::max<juce::int64>(0, s) / 60;
    const auto J = lang::tr("J"), H = lang::tr("H"), MIN = lang::tr("MIN");
    if (m >= 24 * 60) return juce::String(m / (24 * 60)) + " " + J + " " + juce::String((m % (24 * 60)) / 60) + " " + H;
    if (m >= 60) return juce::String(m / 60) + " " + H + " " + juce::String(m % 60).paddedLeft('0', 2);
    return juce::String(m + 1) + " " + MIN;
}

void Licence::useFolderForTests(const juce::File& dir, const juce::String& id, std::function<juce::int64()> n) {
    {
        std::lock_guard<std::mutex> lk(mu);
        testDir = dir;
        testMachine = id;
        testNow = std::move(n);
        cachedCode = {};
        licensed = checked = false;
        forced = -1;
    }
    code();
}

void Licence::forceForTests(bool on) {
    std::lock_guard<std::mutex> lk(mu);
    forced = on ? 1 : 0;
    playing = on;
}
