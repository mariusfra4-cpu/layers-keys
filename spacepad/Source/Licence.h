// Spacepad by JB Praise — licence : une clé = un ordinateur, sans Internet (même principe que Layers Keys).
//
//   - Chaque ordinateur a un « code » JBS-XXXX-XXXX-XXXX-XXXX, calculé à partir de son identifiant interne.
//   - La clé de licence est la signature Ed25519 de « Spacepad|<code> ». Seul « JB Clés » (le générateur, protégé par
//     le mot de passe de JB) sait signer : le logiciel ne contient que la clé publique, qui vérifie sans pouvoir fabriquer.
//   - Démo de 3 jours à partir du clic sur « Essayer 3 jours ». Date de début écrite à deux endroits et protégée par une
//     empreinte ; une horloge remise en arrière est repérée.
//   - Licence et démo sont dans le dossier commun : l'application, le VST3 et l'AU partagent le même état.
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <functional>

namespace spacepad
{
class Licence
{
public:
    enum class State { Locked, Demo, Licensed };
    struct Status { State state = State::Locked; juce::RelativeTime demoLeft; bool demoUsed = false, tamper = false; };

    Licence();
    juce::String code();                                  // code de cet ordinateur (à envoyer à JB Praise)
    Status status();                                      // état actuel (met aussi à jour l'horloge de la démo)
    juce::String activate (const juce::String& key);      // "" si la clé est bonne, sinon le message d'erreur
    void startDemo();

    static juce::String normaliseCode (const juce::String&);      // "JBS-XXXX-…" ou "" si invalide (O accepté pour 0)
    static bool verify (const std::array<juce::uint8, 32>& pub, const juce::String& code, const juce::String& key);
    static juce::String formatDemoLeft (juce::RelativeTime);

    // remplaçables pour les tests
    std::function<juce::String()> machineId;
    std::function<juce::Array<juce::File>()> dirs;
    std::function<juce::int64()> now;                    // secondes
    std::array<juce::uint8, 32> publicKey;

    static juce::String rawMachineId();                   // identifiant de l'ordinateur (Mac : IOKit, Windows : MachineGuid)
    static juce::Array<juce::File> defaultDirs();

private:
    juce::String demoMac (juce::int64 start, juce::int64 last);
    bool readLicence();
    juce::CriticalSection lock;
    juce::String codeCache;
    bool licensed = false, checked = false;
};
}
