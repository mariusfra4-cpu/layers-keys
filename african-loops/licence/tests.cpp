// Tests des licences : chiffrement (vecteurs officiels et valeurs calculées par Python « cryptography »),
// codes, clés, et compatibilité avec les clés de Layers Keys.
#include "KeyCrypto.h"
#include "LicenceCore.h"

#include <cstdio>
#include <cstring>
#include <random>

using namespace keycrypto;

static int failures = 0;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            ++failures;                                 \
            std::printf("ÉCHEC ligne %d : ", __LINE__); \
            std::printf(__VA_ARGS__);                   \
            std::printf("\n");                          \
        }                                               \
    } while (0)

static Bytes hx(const char* s) {
    Bytes b;
    fromHex(s, b);
    return b;
}

int main() {
    // --- SHA-256 / SHA-512 (FIPS 180)
    CHECK(toHex(sha256((const uint8_t*)"abc", 3).data(), 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256");
    {
        const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        CHECK(toHex(sha256((const uint8_t*)m, std::strlen(m)).data(), 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 long");
    }
    CHECK(toHex(sha512((const uint8_t*)"abc", 3).data(), 64) ==
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
          "sha512");
    CHECK(toHex(sha512(nullptr, 0).data(), 64).substr(0, 16) == "cf83e1357eefb8bd", "sha512 vide");
    {
        std::string big(1000, 'a');  // plusieurs blocs
        CHECK(toHex(sha512((const uint8_t*)big.data(), big.size()).data(), 64).substr(0, 16) == "67ba5535a46e3f86", "sha512 1000 a");
    }
    // --- HMAC (RFC 4231, cas 2)
    {
        auto m = hmacSha256((const uint8_t*)"Jefe", 4, (const uint8_t*)"what do ya want for nothing?", 28);
        CHECK(toHex(m.data(), 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", "hmac");
    }
    // --- PBKDF2 (comme Java PBKDF2WithHmacSHA256)
    {
        auto k = pbkdf2Sha256("TESTTESTTEST", (const uint8_t*)"saltsaltsaltsalt", 16, 1000, 32);
        CHECK(toHex(k.data(), 32) == "cb02bd005916984d73f0e742fc544ac426fd53cc2b355b7bafd45ba6d6e0e8e7", "pbkdf2");
    }
    // --- AES-256-GCM
    {
        Bytes key(32), iv(12);
        for (int i = 0; i < 32; ++i) key[(size_t)i] = (uint8_t)i;
        for (int i = 0; i < 12; ++i) iv[(size_t)i] = (uint8_t)i;
        const std::string msg = "Bonjour African Loops Pro ! 1234567890";
        auto ct = aes256GcmEncrypt(key.data(), iv.data(), 12, (const uint8_t*)msg.data(), msg.size());
        CHECK(toHex(ct.data(), ct.size()) ==
                  "056db871aa90b03bcc27e5e2d288164dcfb9e844835b0f0e5747c4a52c5b3386342699c496f1d14688917f6da774e4901020867542e9",
              "aes-gcm : %s", toHex(ct.data(), ct.size()).c_str());
        Bytes back;
        CHECK(aes256GcmDecrypt(key.data(), iv.data(), 12, ct.data(), ct.size(), back) && std::string(back.begin(), back.end()) == msg, "gcm retour");
        ct[3] ^= 1;
        CHECK(!aes256GcmDecrypt(key.data(), iv.data(), 12, ct.data(), ct.size(), back), "gcm : donnée modifiée acceptée");
        key[0] ^= 1;
        ct[3] ^= 1;
        CHECK(!aes256GcmDecrypt(key.data(), iv.data(), 12, ct.data(), ct.size(), back), "gcm : mauvaise clé acceptée");
    }
    // --- Ed25519 (RFC 8032, test 1 et 2)
    {
        auto seed = hx("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
        auto pub = ed25519PublicKey(seed.data());
        CHECK(toHex(pub.data(), 32) == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "ed25519 clé publique");
        auto sig = ed25519Sign(seed.data(), nullptr, 0);
        CHECK(toHex(sig.data(), 64) ==
                  "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
              "ed25519 signature");
        CHECK(ed25519Verify(pub.data(), nullptr, 0, sig.data()), "ed25519 vérification");
        auto seed2 = hx("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
        const uint8_t m2 = 0x72;
        auto sig2 = ed25519Sign(seed2.data(), &m2, 1);
        CHECK(toHex(sig2.data(), 64).substr(0, 32) == "92a009a9f0d4cab8720e820b5f642540", "ed25519 test 2");
        // signatures fausses refusées
        std::mt19937 rng(7);
        int refused = 0;
        for (int i = 0; i < 200; ++i) {
            auto bad = sig;
            bad[(size_t)(rng() % 64)] ^= (uint8_t)(1u << (rng() % 8));
            if (!ed25519Verify(pub.data(), nullptr, 0, bad.data())) ++refused;
        }
        CHECK(refused == 200, "ed25519 : %d / 200 signatures fausses refusées", refused);
        const uint8_t other = 1;
        CHECK(!ed25519Verify(pub.data(), &other, 1, sig.data()), "ed25519 : autre message accepté");
    }
    // --- base32
    {
        const uint8_t d[] = {'f', 'o', 'o', 'b', 'a', 'r'};
        CHECK(base32Encode(d, 6) == "MZXW6YTBOI", "base32");
        Bytes b;
        CHECK(base32Decode("MZXW6YTBOI", b) && b.size() == 6 && std::memcmp(b.data(), d, 6) == 0, "base32 retour");
        CHECK(!base32Decode("MZXW6YTB0I", b), "base32 : caractère invalide accepté");
    }
    // --- codes et clés
    {
        using namespace licence;
        const auto& al = africanLoops();
        const auto& lk = layersKeys();
        CHECK(formatCode(al, "  Windows|ABCD-1234 ") == "JBA-3B95-35EF-DEAA-F6E8", "code : %s", formatCode(al, "Windows|ABCD-1234").c_str());
        CHECK(normalizeCode(al, "jba 3b95 35ef deaa f6e8") == "JBA-3B95-35EF-DEAA-F6E8", "code saisi en minuscules");
        CHECK(normalizeCode(al, "Mon code : JBA-3B95-35EF-DEAA-F6E8 merci") == "JBA-3B95-35EF-DEAA-F6E8", "code dans un message");
        CHECK(normalizeCode(al, "JBA-3B95-35EF-DEAA-F6EO") == "JBA-3B95-35EF-DEAA-F6E0", "lettre O");
        CHECK(normalizeCode(al, "JBK-3B95-35EF-DEAA-F6E8").empty(), "code Layers Keys accepté pour African Loops");
        CHECK(normalizeCode(al, "JBA-3B95-35EF-DEAA").empty(), "code trop court accepté");
        uint8_t seed[32];
        for (int i = 0; i < 32; ++i) seed[i] = (uint8_t)i;
        // mêmes clés que Layers Keys (Go) et que le générateur Android
        CHECK(makeKey(lk, seed, "JBK-1A2B-3C4D-5E6F-7A8B") ==
                  "BIBB67FT-U5AOFOQQ-EJRTD5S3-ZOOQ7ZBW-ZKCCYMUX-STXPDKG3-3EL2DAT4-PY5XNHOU-DCAXCE52-6KSQXBIW-FXDEK5F5-JLYASQKQ-JVWJIBY",
              "clé Layers Keys : %s", makeKey(lk, seed, "JBK-1A2B-3C4D-5E6F-7A8B").c_str());
        const auto k = makeKey(al, seed, "JBA-7F3C-91D2-4B8E-A05D");
        CHECK(k == "2PVJM63G-KRRHQHXJ-34HS3UFF-YI4CXY7G-N5YXNCWH-CJBRLSRV-QUL4RNX6-V77XRIAN-5RHL435L-NPMVW7WT-SPNXRRYP-LXPQYXP5-CH4TICY",
              "clé African Loops : %s", k.c_str());
        auto pub = keycrypto::ed25519PublicKey(seed);
        CHECK(verifyKey(al, pub.data(), "JBA-7F3C-91D2-4B8E-A05D", k), "bonne clé refusée");
        std::string lower = k;
        for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
        CHECK(verifyKey(al, pub.data(), "jba7f3c91d24b8ea05d", " " + lower + "\n"), "clé en minuscules / espaces refusée");
        CHECK(!verifyKey(al, pub.data(), "JBA-7F3C-91D2-4B8E-A05E", k), "clé acceptée pour un autre ordinateur");
        CHECK(!verifyKey(lk, pub.data(), "JBK-7F3C-91D2-4B8E-A05D", k), "clé African Loops acceptée par Layers Keys");
        CHECK(!verifyKey(al, kAfricanLoopsPublicKey.data(), "JBA-7F3C-91D2-4B8E-A05D", k), "clé de test acceptée par le vrai logiciel");
        CHECK(!verifyKey(al, pub.data(), "JBA-7F3C-91D2-4B8E-A05D", k.substr(0, 40)), "clé incomplète acceptée");
        CHECK(formatKey("abc").empty(), "clé invalide mise en forme");
    }
    // --- graine chiffrée : lue exactement comme le générateur Android (blob fabriqué par Java)
    {
        const char* javaBlob =
            "6465666768696a6b6c6d6e6f7071727332333435363738393a3b3c3d1877c0be45e94b811ed0ff11bd6118a18ce0370f812e7e25a170d35e81973b9de05ec7bcd7c525cfe0758eb10968e52e";
        uint8_t seed[32] = {};
        CHECK(licence::unlockSeed(javaBlob, "test-test test", seed), "blob Java illisible");
        bool same = true;
        for (int i = 0; i < 32; ++i) same &= seed[i] == i;
        CHECK(same, "graine du blob Java fausse");
        CHECK(!licence::unlockSeed(javaBlob, "TEST-TEST-TESX", seed), "mauvais mot de passe accepté");
        uint8_t salt[16], iv[12];
        for (int i = 0; i < 16; ++i) salt[i] = (uint8_t)(100 + i);
        for (int i = 0; i < 12; ++i) iv[i] = (uint8_t)(50 + i);
        uint8_t s0[32];
        for (int i = 0; i < 32; ++i) s0[i] = (uint8_t)i;
        CHECK(licence::lockSeed(s0, "TESTTESTTEST", salt, iv) == javaBlob, "blob différent de Java");
    }
    if (failures) {
        std::printf("%d test(s) de licence en échec\n", failures);
        return 1;
    }
    std::printf("Tous les tests de licence passent.\n");
    return 0;
}
