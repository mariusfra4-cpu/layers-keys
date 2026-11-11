package main

// État complet de l'interface sous forme d'octets : sert au plugin VST3 (le DAW range ces octets dans son projet).

import "encoding/json"

// StateBytes : sons, réglages des 8 pistes, presets, volume, réverbération, langue.
func (u *UI) StateBytes() []byte {
	var out []byte
	u.with(func() {}) // ne lit jamais pendant qu'un réglage s'écrit
	c := *u.cfg
	c.Setup = u.snapshot()
	c.Master, c.Reverb = u.eng.master, u.eng.revAmt
	c.LeslieFast = u.eng.LeslieFast()
	c.Lang = langCode(CurLang())
	c.Learn = u.learnList()
	c.Format, c.Version = projectFormat, cfgVersion
	c.Recent, c.RecentProj, c.Project, c.Changed = nil, nil, "", false
	c.Audio, c.AudioPending = AudioCfg{}, false
	out, _ = json.Marshal(&c)
	return out
}

// SetStateBytes recharge un état écrit par StateBytes.
func (u *UI) SetStateBytes(b []byte) bool {
	c := defaultConfig()
	if err := json.Unmarshal(b, c); err != nil || (c.Format != projectFormat) {
		return false
	}
	c.sanitize()
	u.eng.Panic()
	u.cfg.Presets, u.cfg.Cur = c.Presets, c.Cur
	u.cfg.Notation = c.Notation
	u.cfg.ATMode, u.cfg.ATAmt, u.cfg.VelGamma, u.cfg.VelLo, u.cfg.VelHi = c.ATMode, c.ATAmt, c.VelGamma, c.VelLo, c.VelHi
	u.applyPlaying()
	u.learnLoad(c.Learn)
	u.eng.SetMaster(c.Master)
	u.eng.SetReverb(c.Reverb)
	u.eng.SetLeslieFast(c.LeslieFast)
	u.loadFX(c.FXT)
	for i := range u.eng.ch {
		u.eng.ch[i].Solo = false
	}
	u.applySetup(c.Setup)
	if c.Lang != "" {
		SetLang(langFromCode(c.Lang))
	}
	u.setNotation()
	u.changed = false
	u.host.Invalidate()
	return true
}
