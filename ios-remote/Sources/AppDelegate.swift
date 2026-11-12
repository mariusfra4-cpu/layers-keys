import UIKit

/// Layers Remote pour iPhone / iPad : télécommande MIDI 8 pistes (volume, Muet, Solo, Hold) + live,
/// pour Layers Keys et tous les DAW. Même fonctionnement que la version Android.
@main
final class AppDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?

    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        let w = UIWindow(frame: UIScreen.main.bounds)
        w.rootViewController = RemoteViewController()
        w.makeKeyAndVisible()
        window = w
        application.isIdleTimerDisabled = true // l'écran ne s'éteint pas en plein culte
        return true
    }

    func applicationDidBecomeActive(_ application: UIApplication) {
        application.isIdleTimerDisabled = true
    }
}

final class RemoteViewController: UIViewController {
    private let st = RemoteState()
    private let midi = MidiOut()
    private var rv: RemoteView!
    private static let key = "etat"

    override func loadView() {
        st.load(UserDefaults.standard.string(forKey: RemoteViewController.key))
        st.sustain = false
        st.panicFlash = false
        // essais automatiques (simulateur) : -LRPage 0|1|2 choisit la page, -LRDemo allume quelques boutons
        let args = ProcessInfo.processInfo.arguments
        if let i = args.firstIndex(of: "-LRPage"), i + 1 < args.count, let pg = Int(args[i + 1]) { st.page = max(0, min(2, pg)) }
        if args.contains("-LRCycle") { // essai : change de page toutes les 4 s, comme un doigt sur les onglets
            var n = 0
            Timer.scheduledTimer(withTimeInterval: 4, repeats: true) { [weak self] _ in
                guard let self = self else { return }
                n += 1
                self.st.page = n % 3
                self.rv.setNeedsDisplay()
            }
        }
        if args.contains("-LRDemo") {
            st.mute[0] = true; st.solo[1] = true; st.hold[3] = true
            st.vol[1] = 0.34; st.names[0] = "Piano"; st.names[1] = "Basse"; st.leslieFast = true; st.preset = 2
        }
        st.out = { [weak self] m in self?.midi.send(m) }
        rv = RemoteView(state: st)
        rv.midi = midi
        rv.onSave = { [weak self] in self?.save() }
        rv.onRename = { [weak self] i in self?.rename(i) }
        midi.onChange = { [weak self] in self?.rv.setNeedsDisplay() }
        view = rv
        // l'état de la connexion est redessiné régulièrement
        Timer.scheduledTimer(withTimeInterval: 1.5, repeats: true) { [weak self] _ in self?.rv.setNeedsDisplay() }
    }

    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }
    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
    override var preferredScreenEdgesDeferringSystemGestures: UIRectEdge { .all }

    private func save() {
        UserDefaults.standard.set(st.save(), forKey: RemoteViewController.key)
    }

    private func rename(_ i: Int) {
        let a = UIAlertController(title: "Nom de la piste \(i + 1)", message: nil, preferredStyle: .alert)
        a.addTextField { [weak self] f in
            f.text = self?.st.names[i]
            f.clearButtonMode = .whileEditing
        }
        a.addAction(UIAlertAction(title: "Annuler", style: .cancel))
        a.addAction(UIAlertAction(title: "OK", style: .default) { [weak self, weak a] _ in
            guard let self = self else { return }
            var n = (a?.textFields?.first?.text ?? "").replacingOccurrences(of: "|", with: "/").trimmingCharacters(in: .whitespaces)
            if n.count > 16 { n = String(n.prefix(16)) }
            self.st.names[i] = n.isEmpty ? "Piste \(i + 1)" : n
            self.save()
            self.rv.setNeedsDisplay()
        })
        present(a, animated: true)
    }
}
