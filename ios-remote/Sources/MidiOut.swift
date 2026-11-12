import Foundation
import CoreMIDI

/// Sorties MIDI de l'iPhone (tout est déjà dans iOS, rien à installer) :
///   - Wi-Fi / partage de connexion : la session « réseau MIDI » d'Apple (RTP-MIDI). Le Mac la voit dans
///     Configuration audio et MIDI > Réseau ; Windows avec rtpMIDI. iOS l'annonce lui-même (Bonjour).
///   - Câble vers un Mac : Configuration audio et MIDI > l'iPhone > « Activer » (audio et MIDI entre appareils) :
///     iOS crée une destination « IDAM MIDI Host » ;
///   - interface MIDI USB / Bluetooth branchée à l'iPhone.
/// Chaque message part vers toutes ces destinations, et aussi par une source virtuelle « Layers Remote »
/// (pour les applis musicales du même iPhone).
final class MidiOut {
    private var client = MIDIClientRef()
    private var port = MIDIPortRef()
    private var source = MIDIEndpointRef()
    var onChange: (() -> Void)?

    init() {
        MIDIClientCreateWithBlock("Layers Remote" as CFString, &client) { [weak self] _ in
            DispatchQueue.main.async { self?.onChange?() }
        }
        MIDIOutputPortCreate(client, "Sortie" as CFString, &port)
        MIDISourceCreate(client, "Layers Remote" as CFString, &source)
        let s = MIDINetworkSession.default()
        s.isEnabled = true
        s.connectionPolicy = .anyone
        NotificationCenter.default.addObserver(forName: NSNotification.Name(rawValue: MIDINetworkNotificationSessionDidChange),
                                               object: nil, queue: .main) { [weak self] _ in self?.onChange?() }
    }

    /// ordinateurs connectés par le réseau
    func networkPeers() -> [String] {
        MIDINetworkSession.default().connections().map { $0.host.name }
    }

    /// destinations hors réseau (câble vers le Mac, interface MIDI…)
    func cableDestinations() -> [String] {
        var out: [String] = []
        let net = MIDINetworkSession.default().destinationEndpoint()
        for i in 0..<MIDIGetNumberOfDestinations() {
            let d = MIDIGetDestination(i)
            if d == net { continue }
            var name: Unmanaged<CFString>?
            if MIDIObjectGetStringProperty(d, kMIDIPropertyDisplayName, &name) == noErr, let n = name?.takeRetainedValue() {
                out.append(n as String)
            }
        }
        return out
    }

    var connected: Bool { !networkPeers().isEmpty || !cableDestinations().isEmpty }

    var sessionName: String { MIDINetworkSession.default().networkName }

    func send(_ bytes: [UInt8]) {
        guard !bytes.isEmpty else { return }
        let size = 1024
        let buf = UnsafeMutableRawPointer.allocate(byteCount: size, alignment: 8)
        defer { buf.deallocate() }
        let list = buf.assumingMemoryBound(to: MIDIPacketList.self)
        var pkt = MIDIPacketListInit(list)
        bytes.withUnsafeBufferPointer { b in
            pkt = MIDIPacketListAdd(list, size, pkt, 0, bytes.count, b.baseAddress!)
        }
        for i in 0..<MIDIGetNumberOfDestinations() {
            MIDISend(port, MIDIGetDestination(i), list)
        }
        MIDIReceived(source, list)
    }
}
