#pragma once
// Outils de chiffrement pour les licences (sans bibliothèque externe, sauf Monocypher pour Ed25519).
// Utilisés par le logiciel (vérifier une clé) et par « JB Générateur de clés » (fabriquer une clé).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace keycrypto {

using Bytes = std::vector<uint8_t>;

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t n);
std::array<uint8_t, 64> sha512(const uint8_t* data, size_t n);
inline std::array<uint8_t, 32> sha256(const std::string& s) { return sha256((const uint8_t*)s.data(), s.size()); }

std::array<uint8_t, 32> hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t n);
// PBKDF2-HMAC-SHA256 (même calcul que Java « PBKDF2WithHmacSHA256 »)
Bytes pbkdf2Sha256(const std::string& password, const uint8_t* salt, size_t saltLen, int iterations, size_t outLen);
// AES-256-GCM (étiquette de 16 octets à la fin de « cipherAndTag ») ; false si la clé ou les données sont fausses
bool aes256GcmDecrypt(const uint8_t key[32], const uint8_t* iv, size_t ivLen, const uint8_t* cipherAndTag, size_t n, Bytes& plain);
Bytes aes256GcmEncrypt(const uint8_t key[32], const uint8_t* iv, size_t ivLen, const uint8_t* plain, size_t n);

// Ed25519 (RFC 8032)
std::array<uint8_t, 32> ed25519PublicKey(const uint8_t seed[32]);
std::array<uint8_t, 64> ed25519Sign(const uint8_t seed[32], const uint8_t* msg, size_t n);
bool ed25519Verify(const uint8_t pub[32], const uint8_t* msg, size_t n, const uint8_t sig[64]);

// base32 (RFC 4648, sans « = »)
std::string base32Encode(const uint8_t* d, size_t n);
bool base32Decode(const std::string& s, Bytes& out);
std::string toHex(const uint8_t* d, size_t n, bool upper = false);
bool fromHex(const std::string& s, Bytes& out);

void wipe(void* p, size_t n);

}  // namespace keycrypto
