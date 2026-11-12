import UIKit

/// Toute l'interface, dessinée directement (rapide) : MIXEUR, LIVE, RÉGLAGES. Même disposition que la version Android.
/// Plusieurs doigts à la fois ; faders « relatifs » (poser le doigt ne fait jamais sauter le volume).
final class RemoteView: UIView {
    let st: RemoteState
    var midi: MidiOut?
    var onRename: ((Int) -> Void)?
    var onSave: (() -> Void)?

    // couleurs (mêmes que Layers Keys)
    static func rgb(_ v: UInt32) -> UIColor {
        UIColor(red: CGFloat((v >> 16) & 255) / 255, green: CGFloat((v >> 8) & 255) / 255, blue: CGFloat(v & 255) / 255, alpha: 1)
    }
    let BG = rgb(0x070b12), PANEL = rgb(0x111a2b), PANEL2 = rgb(0x0b1320), EDGE = rgb(0x2b3b57)
    let GOLD = rgb(0xf5c21f), GOLD_DIM = rgb(0x8a6d12), TEXT = rgb(0xeef2f8), TEXT2 = rgb(0x8b97ad)
    let RED = rgb(0xe5484d), BLUE = rgb(0x38a1ff), GREEN = rgb(0x35c26b), DARK = rgb(0x1a1400)
    let BTN = rgb(0x1c2639), BTN_EDGE = rgb(0x3a4c6b), GROOVE = rgb(0x03060a), GROOVE_EDGE = rgb(0x26364f)

    private enum Hold { case fader(Int, CGFloat, Double), hfader(CGFloat, Double), button(Int), sustain, panic }
    private var holds: [ObjectIdentifier: Hold] = [:]

    init(state: RemoteState) {
        st = state
        super.init(frame: .zero)
        isMultipleTouchEnabled = true
        backgroundColor = BG
        contentMode = .redraw
    }

    required init?(coder: NSCoder) { fatalError("non utilisé") }

    // ---------------------------------------------------------------- géométrie (zone sûre : encoche de l'iPhone)

    private var R: CGRect { bounds.inset(by: safeAreaInsets) }
    private var W: CGFloat { R.width }
    private var H: CGFloat { R.height }
    private var X0: CGFloat { R.minX }
    private var Y0: CGFloat { R.minY }
    private var top: CGFloat { Y0 + max(H * 0.12, 46) }

    private func tabX(_ i: Int) -> CGFloat { X0 + W * 0.30 + CGFloat(i) * (W * 0.15 + 8) }
    private var tabW: CGFloat { W * 0.15 }

    private var stripW: CGFloat { (W - 10) / 9 }
    private func stripX(_ i: Int) -> CGFloat { X0 + 5 + CGFloat(i) * stripW }
    private var btnTop: CGFloat { top + 32 }
    private var btnH: CGFloat { max(38, H * 0.10) }
    private var fadTop: CGFloat { btnTop + btnH + 10 }
    private var fadBot: CGFloat { Y0 + H - 30 }

    // ---------------------------------------------------------------- dessin

    override func draw(_ rect: CGRect) {
        BG.setFill()
        UIRectFill(bounds)
        drawTop()
        switch st.page {
        case 0: drawMixer()
        case 1: drawLive()
        default: drawSettings()
        }
    }

    private func text(_ s: String, _ x: CGFloat, _ y: CGFloat, _ size: CGFloat, _ col: UIColor, center: Bool, maxW: CGFloat = 10000) {
        var fs = size
        var font = UIFont.boldSystemFont(ofSize: fs)
        var w = (s as NSString).size(withAttributes: [.font: font]).width
        if w > maxW && w > 0 {
            fs = max(fs * maxW / w, 7)
            font = UIFont.boldSystemFont(ofSize: fs)
            w = (s as NSString).size(withAttributes: [.font: font]).width
        }
        let h = font.lineHeight
        let p = CGPoint(x: center ? x - w / 2 : x, y: y - h / 2)
        (s as NSString).draw(at: p, withAttributes: [.font: font, .foregroundColor: col])
    }

    private func box(_ r: CGRect, _ rad: CGFloat, _ fill: UIColor, _ edge: UIColor?) {
        let p = UIBezierPath(roundedRect: r, cornerRadius: rad)
        fill.setFill()
        p.fill()
        if let e = edge {
            e.setStroke()
            p.lineWidth = 1
            p.stroke()
        }
    }

    private func button(_ r: CGRect, _ label: String, _ on: Bool, _ onCol: UIColor, _ ts: CGFloat) {
        if on { box(r, 8, onCol, nil) } else { box(r, 8, BTN, BTN_EDGE) }
        text(label, r.midX, r.midY, ts, on ? DARK : TEXT, center: true, maxW: r.width - 6)
    }

    private func drawTop() {
        box(CGRect(x: 0, y: 0, width: bounds.width, height: top), 0, PANEL2, nil)
        let th = top - Y0
        text("LAYERS REMOTE", X0 + 14, Y0 + th * 0.40, th * 0.30, GOLD, center: false)
        text("by JB Praise", X0 + 14, Y0 + th * 0.78, th * 0.18, TEXT2, center: false)
        let tabs = ["MIXEUR", "LIVE", "RÉGLAGES"]
        for i in 0..<3 {
            button(CGRect(x: tabX(i), y: Y0 + th * 0.16, width: tabW, height: th * 0.68), tabs[i], st.page == i, GOLD, th * 0.26)
        }
        let ok = midi?.connected ?? false
        let sx = X0 + W * 0.79, sw = W * 0.21 - 10
        box(CGRect(x: sx, y: Y0 + th * 0.2, width: sw, height: th * 0.6), th * 0.3, ok ? RemoteView.rgb(0x0f2a1a) : RemoteView.rgb(0x2a1214), ok ? GREEN : RED)
        (ok ? GREEN : RED).setFill()
        UIBezierPath(ovalIn: CGRect(x: sx + th * 0.21, y: Y0 + th * 0.41, width: th * 0.18, height: th * 0.18)).fill()
        text(ok ? "CONNECTÉ" : "NON CONNECTÉ", sx + sw / 2 + th * 0.12, Y0 + th * 0.5, th * 0.22, ok ? GREEN : RED, center: true, maxW: sw - th * 0.6)
    }

    private func drawMixer() {
        for i in 0..<9 {
            let x = stripX(i), w = stripW - 6
            let master = i == 8
            box(CGRect(x: x, y: top + 6, width: w, height: Y0 + H - top - 10), 10, master ? RemoteView.rgb(0x151d10) : PANEL, master ? GOLD_DIM : EDGE)
            let name = master ? "GÉNÉRAL" : "\(i + 1)  \(st.names[i])"
            text(name, x + w / 2, top + 20, 12, master ? GOLD : (st.mute[i] ? TEXT2 : TEXT), center: true, maxW: w - 8)
            if !master {
                let bw = (w - 16) / 3
                button(CGRect(x: x + 4, y: btnTop, width: bw, height: btnH), "M", st.mute[i], RED, btnH * 0.42)
                button(CGRect(x: x + 8 + bw, y: btnTop, width: bw, height: btnH), "S", st.solo[i], GOLD, btnH * 0.42)
                button(CGRect(x: x + 12 + 2 * bw, y: btnTop, width: bw, height: btnH), "H", st.hold[i], BLUE, btnH * 0.42)
            } else {
                text("volume", x + w / 2, btnTop + btnH / 2, 11, TEXT2, center: true, maxW: w - 8)
            }
            let v = master ? st.master : st.vol[i]
            drawFader(x + w / 2, fadTop, fadBot, w, CGFloat(v), (!master && st.mute[i]) ? RemoteView.rgb(0x6b7486) : GOLD)
            text("\(Int((v * 100).rounded())) %", x + w / 2, Y0 + H - 16, 12, master ? GOLD : TEXT2, center: true, maxW: w - 8)
        }
    }

    private func drawFader(_ cx: CGFloat, _ y0: CGFloat, _ y1: CGFloat, _ w: CGFloat, _ v: CGFloat, _ col: UIColor) {
        let gw: CGFloat = 8
        box(CGRect(x: cx - gw / 2, y: y0, width: gw, height: y1 - y0), gw / 2, GROOVE, GROOVE_EDGE)
        let capH = max(30, (y1 - y0) * 0.09), capW = min(w * 0.78, 80)
        let yy = y1 - capH / 2 - v * (y1 - y0 - capH)
        box(CGRect(x: cx - gw / 2, y: yy, width: gw, height: y1 - yy), gw / 2, col.withAlphaComponent(0.67), nil)
        GROOVE_EDGE.setFill()
        for k in 0...10 {
            let ty = y1 - capH / 2 - CGFloat(k) / 10 * (y1 - y0 - capH)
            UIRectFill(CGRect(x: cx - capW / 2, y: ty - 0.5, width: capW / 2 - gw, height: 1))
        }
        box(CGRect(x: cx - capW / 2, y: yy - capH / 2, width: capW, height: capH), 6, RemoteView.rgb(0x2f3747), RemoteView.rgb(0x4a5568))
        col.setFill()
        UIRectFill(CGRect(x: cx - capW / 2 + 4, y: yy - 1.5, width: capW - 8, height: 3))
    }

    // ---- LIVE

    private struct LiveGeo { var presets: [CGRect]; var leslie, sustain, panic, reverb: CGRect }

    private var liveGeo: LiveGeo {
        let y0 = top + 12, h = Y0 + H - y0 - 12
        let lw = W * 0.58
        let gy = y0 + 22, gh = h - 22
        let bw = (lw - 14 - 30) / 4, bh = (gh - 10) / 2
        var ps: [CGRect] = []
        for k in 0..<8 {
            ps.append(CGRect(x: X0 + 14 + CGFloat(k % 4) * (bw + 10), y: gy + CGFloat(k / 4) * (bh + 10), width: bw, height: bh))
        }
        let rx = X0 + lw + 10, rw = X0 + W - rx - 14, rh = (h - 30) / 4
        return LiveGeo(presets: ps,
                       leslie: CGRect(x: rx, y: y0, width: rw, height: rh),
                       sustain: CGRect(x: rx, y: y0 + rh + 10, width: rw, height: rh),
                       panic: CGRect(x: rx, y: y0 + 2 * (rh + 10), width: rw, height: rh),
                       reverb: CGRect(x: rx, y: y0 + 3 * (rh + 10), width: rw, height: rh))
    }

    private func drawLive() {
        let g = liveGeo
        text("PRESETS", X0 + 14, top + 20, 12, GOLD, center: false)
        for (k, r) in g.presets.enumerated() {
            button(r, "PRESET \(k + 1)", st.preset == k, GOLD, min(r.height * 0.2, 20))
        }
        let ts = min(g.leslie.height * 0.3, 20)
        button(g.leslie, st.leslieFast ? "LESLIE : RAPIDE" : "LESLIE : LENT", st.leslieFast, GOLD, ts)
        button(g.sustain, "SUSTAIN (tiens)", st.sustain, BLUE, ts)
        button(g.panic, "TOUT ARRÊTER", st.panicFlash, RED, ts)
        let r = g.reverb
        box(r, 10, PANEL, EDGE)
        text("RÉVERB \(Int((st.reverb * 100).rounded())) %", r.midX, r.minY + r.height * 0.27, min(r.height * 0.22, 15), TEXT, center: true)
        let tx0 = r.minX + 20, tx1 = r.maxX - 20, ty = r.minY + r.height * 0.66
        box(CGRect(x: tx0, y: ty - 4, width: tx1 - tx0, height: 8), 4, GROOVE, GROOVE_EDGE)
        box(CGRect(x: tx0, y: ty - 4, width: (tx1 - tx0) * CGFloat(st.reverb), height: 8), 4, GOLD, nil)
        GOLD.setFill()
        let kx = tx0 + (tx1 - tx0) * CGFloat(st.reverb)
        UIBezierPath(ovalIn: CGRect(x: kx - 12, y: ty - 12, width: 24, height: 24)).fill()
    }

    // ---- RÉGLAGES

    private var setRowH: CGFloat { max(42, (Y0 + H - top) / 7.5) }

    private struct SetGeo { var chanMinus, chanPlus, chanBox, mode, sendAll: CGRect; var y: CGFloat }

    private var setGeo: SetGeo {
        let x = X0 + 18, rh = setRowH, w = W - 36
        let y = top + 12 + rh * 2.3 + 10
        let cw = (w - 20) / 3
        return SetGeo(chanMinus: CGRect(x: x + 6, y: y + rh * 0.15, width: rh * 0.7, height: rh * 0.7),
                      chanPlus: CGRect(x: x + cw - 6 - rh * 0.7, y: y + rh * 0.15, width: rh * 0.7, height: rh * 0.7),
                      chanBox: CGRect(x: x, y: y, width: cw, height: rh),
                      mode: CGRect(x: x + cw + 10, y: y, width: cw, height: rh),
                      sendAll: CGRect(x: x + 2 * (cw + 10), y: y, width: cw, height: rh), y: y)
    }

    private func drawSettings() {
        let x = X0 + 18, rh = setRowH, w = W - 36
        let ts = min(rh * 0.32, 15)
        var y = top + 12
        box(CGRect(x: x, y: y, width: w, height: rh * 2.3), 10, PANEL, EDGE)
        text("CONNEXION (réseau MIDI Apple : Wi-Fi, partage de connexion ou câble)", x + 14, y + rh * 0.35, ts, GOLD, center: false, maxW: w - 28)
        let peers = midi?.networkPeers() ?? []
        let cables = midi?.cableDestinations() ?? []
        var st1 = "Wi-Fi : « \(midi?.sessionName ?? "") » · " + (peers.isEmpty ? "aucun ordinateur connecté" : "connecté à : " + peers.joined(separator: ", "))
        if !cables.isEmpty { st1 += " · câble / MIDI : " + cables.joined(separator: ", ") }
        text(st1, x + 14, y + rh * 0.8, ts, TEXT, center: false, maxW: w - 28)
        text("Mac : Configuration audio et MIDI > Fenêtre > Afficher le studio MIDI > Réseau > choisis l'iPhone > Se connecter.", x + 14, y + rh * 1.25, ts * 0.8, TEXT2, center: false, maxW: w - 28)
        text("Câble vers le Mac : Configuration audio et MIDI > ton iPhone > Activer.  Windows : rtpMIDI (gratuit), puis connecte l'iPhone.", x + 14, y + rh * 1.65, ts * 0.8, TEXT2, center: false, maxW: w - 28)
        text("Aucun Wi-Fi ? Active le partage de connexion de l'iPhone et connecte l'ordinateur dessus.", x + 14, y + rh * 2.0, ts * 0.8, TEXT2, center: false, maxW: w - 28)
        let g = setGeo
        y = g.y
        box(g.chanBox, 10, PANEL, EDGE)
        text("CANAL MIDI : \(st.channel + 1)", g.chanBox.midX, g.chanBox.midY, ts, TEXT, center: true, maxW: g.chanBox.width - rh * 1.6)
        button(g.chanMinus, "−", false, GOLD, ts * 1.3)
        button(g.chanPlus, "+", false, GOLD, ts * 1.3)
        button(g.mode, st.momentary ? "BOUTONS : MOMENTANÉS" : "BOUTONS : BASCULE", !st.momentary, BLUE, ts * 0.9)
        button(g.sendAll, "RENVOYER TOUT", false, GOLD, ts * 0.9)
        y += rh + 18
        text("Layers Keys : Paramètres > « Associer la télécommande Layers Remote ». DAW : MIDI Learn sur chaque bouton.", x, y, ts * 0.8, TEXT2, center: false, maxW: w)
        text("Touche le nom d'une piste (page MIXEUR) pour la renommer.", x, y + 20, ts * 0.8, TEXT2, center: false, maxW: w)
    }

    // ---------------------------------------------------------------- toucher

    private func tick() { UIImpactFeedbackGenerator(style: .light).impactOccurred() }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches { down(t) }
        setNeedsDisplay()
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches { move(t) }
        setNeedsDisplay()
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches { up(t) }
        setNeedsDisplay()
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches { up(t) }
        setNeedsDisplay()
    }

    private func down(_ t: UITouch) {
        let p = t.location(in: self)
        let id = ObjectIdentifier(t)
        if p.y < top {
            for i in 0..<3 where p.x >= tabX(i) && p.x <= tabX(i) + tabW {
                st.page = i
                tick()
                onSave?()
            }
            return
        }
        switch st.page {
        case 0:
            for s in 0..<9 {
                let sx = stripX(s), w = stripW - 6
                guard p.x >= sx && p.x <= sx + w else { continue }
                if s < 8 && p.y < btnTop - 4 { onRename?(s); return }
                if s < 8 && p.y >= btnTop && p.y <= btnTop + btnH {
                    let bw = (w - 16) / 3
                    let b = max(0, min(2, Int((p.x - sx - 4) / (bw + 4))))
                    holds[id] = .button(s * 3 + b)
                    st.pressTrackButton(s, b, down: true)
                    tick()
                    return
                }
                if p.y >= fadTop - 10 {
                    holds[id] = .fader(s, p.y, s == 8 ? st.master : st.vol[s])
                }
                return
            }
        case 1:
            let g = liveGeo
            for (k, r) in g.presets.enumerated() where r.contains(p) { st.choosePreset(k); tick(); onSave?(); return }
            if g.leslie.contains(p) { st.toggleLeslie(); tick(); onSave?(); return }
            if g.sustain.contains(p) { holds[id] = .sustain; st.setSustain(true); tick(); return }
            if g.panic.contains(p) { holds[id] = .panic; st.panic(); tick(); return }
            if g.reverb.contains(p) { holds[id] = .hfader(p.x, st.reverb) }
        default:
            let g = setGeo
            if g.chanMinus.contains(p) || (g.chanBox.contains(p) && p.x < g.chanBox.midX) {
                st.channel = (st.channel + 15) % 16; tick(); onSave?()
            } else if g.chanPlus.contains(p) || g.chanBox.contains(p) {
                st.channel = (st.channel + 1) % 16; tick(); onSave?()
            } else if g.mode.contains(p) {
                st.momentary.toggle(); tick(); onSave?()
            } else if g.sendAll.contains(p) {
                st.sendAll(); tick()
            }
        }
    }

    private func move(_ t: UITouch) {
        let p = t.location(in: self)
        guard let h = holds[ObjectIdentifier(t)] else { return }
        switch h {
        case let .fader(s, y0, v0):
            st.setFader(s, v0 + Double((y0 - p.y) / (fadBot - fadTop)))
        case let .hfader(x0, v0):
            st.setReverb(v0 + Double((p.x - x0) / (W * 0.35)))
        default:
            break
        }
    }

    private func up(_ t: UITouch) {
        let id = ObjectIdentifier(t)
        guard let h = holds[id] else { return }
        switch h {
        case let .button(k): st.pressTrackButton(k / 3, k % 3, down: false)
        case .sustain: st.setSustain(false)
        case .panic: st.panicFlash = false
        case .fader, .hfader: onSave?()
        }
        holds[id] = nil
    }
}
