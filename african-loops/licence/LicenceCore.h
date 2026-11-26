#pragma once
// Licences des logiciels JB Praise : une clé = un ordinateur, sans connexion Internet.
//
//  - Chaque ordinateur a un « code » (JBA-XXXX-XXXX-XXXX-XXXX pour African Loops Pro,
//    JBK-… pour Layers Keys) calculé à partir de son identifiant interne.
//  - La clé de licence est la SIGNATURE Ed25519 de ce code. Seul « JB Générateur de clés »
//    connaît la clé secrète : le logiciel ne contient que la clé publique, qui sert à
//    vérifier, jamais à fabriquer, une licence.
//  - Même format que Layers Keys (base32 par groupes de 8).

#include <array>
#include <cstdint>
#include <string>

namespace licence {

struct Product {
    const char* name;        // « African Loops Pro »
    const char* codePrefix;  // « JBA »
    const char* codeSalt;    // « JBA1| » : mélangé à l'identifiant de l'ordinateur
    const char* msgPrefix;   // « AfricanLoopsPro| » : signé avec le code
};

const Product& africanLoops();
const Product& layersKeys();

// clé publique d'African Loops Pro
extern const std::array<uint8_t, 32> kAfricanLoopsPublicKey;

// Code de l'ordinateur à partir de son identifiant brut.
std::string formatCode(const Product& p, const std::string& machineId);
// Code saisi à la main (espaces, tirets, O au lieu de 0, texte autour) → forme propre, ou "" si invalide.
std::string normalizeCode(const Product& p, const std::string& s);
// Clé : signature du code (générateur seulement).
std::string makeKey(const Product& p, const uint8_t seed[32], const std::string& code);
// Cette clé est-elle la bonne pour ce code ?
bool verifyKey(const Product& p, const uint8_t pub[32], const std::string& code, const std::string& key);
// Clé remise en forme (groupes de 8), ou "" si illisible.
std::string formatKey(const std::string& key);

// Graine de signature chiffrée par mot de passe (même format que le générateur Android) :
// sel(16) + iv(12) + AES-256-GCM ; mot de passe en majuscules, sans espaces ni tirets.
bool unlockSeed(const std::string& blobHex, const std::string& password, uint8_t seed[32]);
std::string lockSeed(const uint8_t seed[32], const std::string& password, const uint8_t salt[16], const uint8_t iv[12]);

}  // namespace licence
