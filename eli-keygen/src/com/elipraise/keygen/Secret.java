package com.elipraise.keygen;

/** Graine de signature chiffrée (AES-256-GCM, mot de passe d'Eli Praise). Sans le mot de passe, elle est inutilisable. */
final class Secret {
    private Secret() {}
    static final String BLOB = "a81520b638d8492be786f4b7acfa77ed78d429008096b415ab861d88c9bc58114f1174b87965413ab5abbef5cb16c6ec30ba7d93ab0cfd5450efef164c7a035b3d964f26860543aac347dcdc";
}
