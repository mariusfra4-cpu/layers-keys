package com.jbpraise.keygen;

/** Graine de signature chiffrée (AES-256-GCM, mot de passe de JB). Sans le mot de passe, elle est inutilisable. */
final class Secret {
    private Secret() {}
    // African Loops Pro (mot de passe à part)
    static final String BLOB_AL = "458ad33707503a5211e4e2de95c90e66bfc8c5615e66a6e120880e2b9c5fc774bfdde361526e62926d41212617e6ad611686a42b8feb9aae7da46fd4581c87662bcf01d85545836b79e59510";
    // Spacepad (mot de passe à part)
    static final String BLOB_SP = "3cf09207c04c01e6a7c6125a13847716907d9af38ee42913344b6aeecfbafdc84089bd0f61f501f059c47b8b95d173511aa39120f5412f65c17e9481c087663be5801f97267b3b007b5d5b6f";
    static final String BLOB = "b4a087ec16231c4850591644e6f2e72b7b538e131250c6d1e16e42b6a48f50d60d574adadc076a62696a3978b8b443b1772e2a746ade89a9cc6f5ef8cbccc91e7ed4893e0f10c464ed52018a";
}
