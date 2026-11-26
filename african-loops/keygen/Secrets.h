#pragma once
// Graines de signature chiffrées (sel 16 + iv 12 + AES-256-GCM, clé = PBKDF2-SHA256 300 000 tours du mot de passe).
// Sans le mot de passe, elles sont inutilisables.
namespace secrets {
// Layers Keys : même graine et même mot de passe que le générateur Android
inline const char* kLayersKeys = "b4a087ec16231c4850591644e6f2e72b7b538e131250c6d1e16e42b6a48f50d60d574adadc076a62696a3978b8b443b1772e2a746ade89a9cc6f5ef8cbccc91e7ed4893e0f10c464ed52018a";
// African Loops Pro
inline const char* kAfricanLoops = "458ad33707503a5211e4e2de95c90e66bfc8c5615e66a6e120880e2b9c5fc774bfdde361526e62926d41212617e6ad611686a42b8feb9aae7da46fd4581c87662bcf01d85545836b79e59510";
}  // namespace secrets
