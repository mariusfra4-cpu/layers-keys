#include "LicenceCore.h"

#include "KeyCrypto.h"

#include <algorithm>
#include <cctype>

namespace licence {

const Product& africanLoops() {
    static const Product p{"African Loops Pro", "JBA", "JBA1|", "AfricanLoopsPro|"};
    return p;
}

const Product& spacepad() {
    static const Product p{"Spacepad", "JBS", "JBS1|", "Spacepad|"};
    return p;
}

const Product& layersKeys() {
    static const Product p{"Layers Keys", "JBK", "JBK1|", "LayersKeys|"};
    return p;
}

// clé publique d'African Loops Pro (la clé secrète n'est que dans « JB Générateur de clés », chiffrée)
const std::array<uint8_t, 32> kAfricanLoopsPublicKey = {
    0x76, 0x38, 0x54, 0xa9, 0x9b, 0xae, 0xf4, 0xac, 0x17, 0xf8, 0x05, 0x71, 0xfe, 0xd9, 0xaa, 0xe8,
    0x4e, 0xb2, 0x58, 0xa1, 0xc4, 0xe3, 0x9f, 0x59, 0x1e, 0xd2, 0xdb, 0x0a, 0x93, 0xf1, 0x20, 0x8b};

const std::array<uint8_t, 32> kSpacepadPublicKey = {
    0xc9, 0xce, 0xb2, 0x20, 0xb5, 0x30, 0xbc, 0x16, 0xdb, 0x1f, 0x47, 0xb8, 0x16, 0x0f, 0x2a, 0x3d,
    0x5f, 0x25, 0x19, 0x20, 0x8f, 0x46, 0x7c, 0xc3, 0x3a, 0x28, 0xb2, 0x60, 0xb0, 0x39, 0x7e, 0xc1};

namespace {
std::string lowerTrim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    s = s.substr(i);
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string group(const std::string& prefix, const std::string& hex16) {
    return prefix + "-" + hex16.substr(0, 4) + "-" + hex16.substr(4, 4) + "-" + hex16.substr(8, 4) + "-" + hex16.substr(12, 4);
}

std::string cleanKey(const std::string& key) {
    std::string k;
    for (char c : key)
        if (!std::isspace((unsigned char)c) && c != '-') k += (char)std::toupper((unsigned char)c);
    return k;
}
}  // namespace

std::string formatCode(const Product& p, const std::string& machineId) {
    auto h = keycrypto::sha256(std::string(p.codeSalt) + lowerTrim(machineId));
    return group(p.codePrefix, keycrypto::toHex(h.data(), 8, true));
}

std::string normalizeCode(const Product& p, const std::string& in) {
    std::string s;
    for (char c : in)
        if (!std::isspace((unsigned char)c) && c != '-') s += (char)std::toupper((unsigned char)c);
    const std::string pre = p.codePrefix;
    // le code peut être au milieu d'un message (WhatsApp…) : on le cherche
    for (size_t at = s.find(pre); at != std::string::npos; at = s.find(pre, at + 1)) {
        if (at + pre.size() + 16 > s.size()) break;
        std::string h = s.substr(at + pre.size(), 16);
        std::replace(h.begin(), h.end(), 'O', '0');  // la lettre O lue à la place du zéro
        if (std::all_of(h.begin(), h.end(), [](char c) { return std::isdigit((unsigned char)c) || (c >= 'A' && c <= 'F'); }))
            return group(pre, h);
    }
    return {};
}

std::string formatKey(const std::string& key) {
    const auto k = cleanKey(key);
    keycrypto::Bytes sig;
    if (k.empty() || !keycrypto::base32Decode(k, sig) || sig.size() != 64) return {};
    std::string out;
    for (size_t i = 0; i < k.size(); i += 8) {
        if (!out.empty()) out += '-';
        out += k.substr(i, 8);
    }
    return out;
}

std::string makeKey(const Product& p, const uint8_t seed[32], const std::string& code) {
    const auto c = normalizeCode(p, code);
    if (c.empty()) return {};
    const std::string msg = std::string(p.msgPrefix) + c;
    auto sig = keycrypto::ed25519Sign(seed, (const uint8_t*)msg.data(), msg.size());
    return formatKey(keycrypto::base32Encode(sig.data(), sig.size()));
}

bool verifyKey(const Product& p, const uint8_t pub[32], const std::string& code, const std::string& key) {
    const auto c = normalizeCode(p, code);
    if (c.empty()) return false;
    keycrypto::Bytes sig;
    if (!keycrypto::base32Decode(cleanKey(key), sig) || sig.size() != 64) return false;
    const std::string msg = std::string(p.msgPrefix) + c;
    return keycrypto::ed25519Verify(pub, (const uint8_t*)msg.data(), msg.size(), sig.data());
}

namespace {
std::string normPassword(const std::string& p) {
    std::string n;
    for (char c : p)
        if (!std::isspace((unsigned char)c) && c != '-') n += (char)std::toupper((unsigned char)c);
    return n;
}
}  // namespace

bool unlockSeed(const std::string& blobHex, const std::string& password, uint8_t seed[32]) {
    keycrypto::Bytes blob, plain;
    if (!keycrypto::fromHex(blobHex, blob) || blob.size() < 28 + 16) return false;
    auto k = keycrypto::pbkdf2Sha256(normPassword(password), blob.data(), 16, 300000, 32);
    const bool ok = keycrypto::aes256GcmDecrypt(k.data(), blob.data() + 16, 12, blob.data() + 28, blob.size() - 28, plain) && plain.size() == 32;
    if (ok) std::copy(plain.begin(), plain.end(), seed);
    keycrypto::wipe(k.data(), k.size());
    if (!plain.empty()) keycrypto::wipe(plain.data(), plain.size());
    return ok;
}

std::string lockSeed(const uint8_t seed[32], const std::string& password, const uint8_t salt[16], const uint8_t iv[12]) {
    auto k = keycrypto::pbkdf2Sha256(normPassword(password), salt, 16, 300000, 32);
    auto ct = keycrypto::aes256GcmEncrypt(k.data(), iv, 12, seed, 32);
    keycrypto::wipe(k.data(), k.size());
    keycrypto::Bytes blob(salt, salt + 16);
    blob.insert(blob.end(), iv, iv + 12);
    blob.insert(blob.end(), ct.begin(), ct.end());
    return keycrypto::toHex(blob.data(), blob.size());
}

}  // namespace licence
