package main

// Lecteur d'échantillons : sert aux SoundFont (SF2) et aux fichiers WAV / MP3.

import "math"

// Une « région » = un échantillon avec la zone du clavier où il joue et ses réglages.
type Region struct {
	KeyLo, KeyHi int
	VelLo, VelHi int

	Data  []int16
	Rate  float64 // fréquence d'échantillonnage du fichier
	Root  int     // note d'origine
	Tune  float64 // accord fin total (cents)
	Scale float64 // cents par demi-ton (100 = normal)

	Start, End         int // zone jouable dans Data (End exclu)
	LoopStart, LoopEnd int
	LoopMode           int // 0 pas de boucle, 1 boucle, 3 boucle tant que la touche est tenue

	AttenDB  float64
	Pan      float64 // -1..1
	Delay    float64 // enveloppe de volume (secondes)
	Attack   float64
	Hold     float64
	Decay    float64
	SusDB    float64 // niveau de maintien : atténuation en dB
	Release  float64
	KeyHold  float64 // timecents par touche (écart à la note 60)
	KeyDecay float64

	FcHz, QdB                   float64 // filtre passe-bas propre au son
	VibCents, VibRate, VibDelay float64
	Excl                        int
}

func newPlainRegion(data []int16, rate float64, root int) *Region {
	return &Region{
		KeyLo: 0, KeyHi: 127, VelLo: 0, VelHi: 127,
		Data: data, Rate: rate, Root: root, Scale: 100,
		Start: 0, End: len(data),
		SusDB: 0, Release: 0.08, Attack: 0.001,
		FcHz: 20000, VibRate: 5,
	}
}

type sampleSource struct{ regions []*Region }

func (s *sampleSource) Spawn(e *Engine, ch *Channel, n, vel int) []voiceI {
	var out []voiceI
	for _, r := range s.regions {
		if n < r.KeyLo || n > r.KeyHi || vel < r.VelLo || vel > r.VelHi {
			continue
		}
		out = append(out, newSampVoice(r, n, vel, ch))
	}
	return out
}

// runSteady produit dst[n:end] quand la voix est en sustain ou en relâchement (même résultat que la boucle générale, en plus rapide).
func (v *sampVoice) runSteady(d []int16, dst []float64, n, end int, inc float64) {
	level, gain, pos := v.level, v.gain, v.pos
	rel := v.stage == stRelease
	relMul := v.relMul
	loop := v.loopActive
	limit := v.endIdx
	if loop {
		limit = v.le
	}
	le, ll := float64(v.le), float64(v.ll)
	stop := float64(v.endIdx - 1)
	dst = dst[:end]
	for ; n < end; n++ {
		if rel {
			level *= relMul
			if level < 1e-4 {
				v.done = true
				break
			}
		}
		i := int(pos)
		fr := pos - float64(i)
		var s0, s1, s2, s3 float64
		if i >= 1 && i+2 < limit && i+2 < len(d) {
			w := d[i-1 : i+3 : i+3]
			s0, s1, s2, s3 = float64(w[0])*(1.0/32768), float64(w[1])*(1.0/32768), float64(w[2])*(1.0/32768), float64(w[3])*(1.0/32768)
		} else {
			s0, s1, s2, s3 = v.at(i-1), v.at(i), v.at(i+1), v.at(i+2)
		}
		y := s1 + 0.5*fr*(s2-s0+fr*(2*s0-5*s1+4*s2-s3+fr*(3*(s1-s2)+s3-s0)))
		dst[n] = y * level * gain
		pos += inc
		if loop {
			for pos >= le {
				pos -= ll
			}
		} else if pos >= stop {
			v.done = true
			break
		}
	}
	v.level, v.pos = level, pos
}

const (
	stDelay = iota
	stAttack
	stHold
	stDecay
	stSustain
	stRelease
)

type sampVoice struct {
	vbase
	r       *Region
	pos     float64
	baseInc float64
	gain    float64

	loopMode   int
	loopActive bool
	ls, le, ll int
	endIdx     int

	stage    int
	level    float64
	cnt      float64
	attInc   float64
	decMul   float64
	susLevel float64
	relMul   float64
	done     bool
	holdCnt  float64

	lfoPh, lfoInc float64
	vibBase       float64 // cents propres à l'échantillon + réglage du canal
	vibRateUser   bool
	vibElapsed    float64
	vibDelay      float64
	pitchMul      float64
}

func newSampVoice(r *Region, n, vel int, ch *Channel) *sampVoice {
	v := &sampVoice{r: r, pitchMul: 1}
	cents := float64(n-r.Root)*r.Scale + r.Tune
	rate := r.Rate
	if rate <= 0 {
		rate = 44100
	}
	v.baseInc = rate / SR * math.Pow(2, cents/1200)
	v.pos = float64(r.Start)
	v.endIdx = r.End
	if v.endIdx > len(r.Data) {
		v.endIdx = len(r.Data)
	}

	// niveau : atténuation du son + dynamique (vitesse de frappe)
	vf := float64(vel) / 127
	att := r.AttenDB + dynDB(ch.P[pDyn])*(1-vf)
	v.gain = math.Pow(10, -att/20) * 0.5

	// boucle
	v.loopMode = r.LoopMode
	if (r.LoopMode == 1 || r.LoopMode == 3) && r.LoopEnd > r.LoopStart+1 && r.LoopEnd <= len(r.Data) {
		v.ls, v.le, v.ll = r.LoopStart, r.LoopEnd, r.LoopEnd-r.LoopStart
		v.loopActive = true
	}

	// enveloppe de volume
	keyHold := math.Pow(2, float64(60-n)*r.KeyHold/1200)
	keyDecay := math.Pow(2, float64(60-n)*r.KeyDecay/1200)
	v.cnt = r.Delay * SR
	hold := r.Hold * keyHold
	decay := r.Decay * keyDecay
	attack := r.Attack
	if attack < 0.0005 {
		attack = 0.0005
	}
	v.attInc = 1 / (attack * SR)
	v.susLevel = math.Pow(10, -r.SusDB/20)
	if decay < 0.0005 {
		v.decMul = 0
	} else {
		v.decMul = math.Pow(10, -5/(decay*SR))
	}
	if v.cnt <= 0 {
		v.stage = stAttack
	} else {
		v.stage = stDelay
	}

	v.pan = r.Pan
	v.excl = r.Excl
	if r.FcHz < 19000 {
		v.sfFc, v.sfQ = r.FcHz, r.QdB
	}
	v.rel = r.Release
	if v.rel < 0.005 {
		v.rel = 0.005
	}

	// vibrato
	v.vibBase = r.VibCents + vibCents(ch.P[pVib])
	v.vibRateUser = ch.P[pVib] > 0.005
	v.lfoInc = twoPi * r.VibRate / SR
	if v.vibRateUser {
		v.lfoInc = twoPi * rateHz(ch.P[pRate]) / SR
	}
	v.vibDelay = r.VibDelay
	v.holdCnt = hold * SR
	return v
}

func (v *sampVoice) base() *vbase { return &v.vbase }

func (v *sampVoice) enterRelease() {
	v.stage = stRelease
	t := v.rel
	if t < 0.005 {
		t = 0.005
	}
	v.relMul = math.Pow(10, -5/(t*SR))
	if v.loopMode == 3 {
		v.loopActive = false
	}
}

func (v *sampVoice) at(i int) float64 {
	d := v.r.Data
	if v.loopActive && i >= v.le {
		i = v.ls + (i-v.le)%v.ll
	}
	if i < 0 {
		i = 0
	} else if i >= len(d) {
		i = len(d) - 1
	}
	return float64(d[i]) * (1.0 / 32768)
}

func (v *sampVoice) render(e *Engine, dst []float64) bool {
	if v.released && v.stage != stRelease {
		v.enterRelease()
	}
	d := v.r.Data
	const chunk = 32
	for off := 0; off < len(dst) && !v.done; off += chunk {
		end := off + chunk
		if end > len(dst) {
			end = len(dst)
		}
		// vibrato / molette de modulation
		vw := e.vibFor(&v.vbase)
		depth := v.vibBase + vw*80
		if depth != 0 && v.vibElapsed >= v.vibDelay {
			if !v.vibRateUser && vw > 0.01 {
				// la molette impose une vitesse de vibrato agréable si le son n'en a pas
				v.lfoInc = twoPi * 5 / SR
			}
			v.pitchMul = math.Exp2(depth * math.Sin(v.lfoPh) / 1200)
		} else {
			v.pitchMul = 1
		}
		v.lfoPh += v.lfoInc * chunk
		if v.lfoPh > twoPi {
			v.lfoPh -= twoPi
		}
		v.vibElapsed += chunk / SR
		inc := v.baseInc * e.bendS * v.pitchMul

		for n := off; n < end; n++ {
			if v.stage >= stSustain { // niveau constant ou relâchement : boucle serrée, sans test d'étape
				v.runSteady(d, dst, n, end, inc)
				break
			}
			switch v.stage {
			case stDelay:
				v.cnt--
				if v.cnt <= 0 {
					v.stage = stAttack
				}
			case stAttack:
				v.level += v.attInc
				if v.level >= 1 {
					v.level = 1
					v.stage = stHold
					v.cnt = v.holdCnt
				}
			case stHold:
				v.cnt--
				if v.cnt <= 0 {
					v.stage = stDecay
					if v.susLevel >= 0.999 {
						v.stage = stSustain
					}
				}
			case stDecay:
				v.level *= v.decMul
				if v.level <= v.susLevel {
					v.level = v.susLevel
					v.stage = stSustain
				}
			case stRelease:
				v.level *= v.relMul
				if v.level < 1e-4 {
					v.done = true
				}
			}
			if v.done {
				break
			}
			i := int(v.pos)
			fr := v.pos - float64(i)
			limit := v.endIdx
			if v.loopActive {
				limit = v.le
			}
			var s0, s1, s2, s3 float64
			if i >= 1 && i+2 < limit {
				s0 = float64(d[i-1])
				s1 = float64(d[i])
				s2 = float64(d[i+1])
				s3 = float64(d[i+2])
				s0, s1, s2, s3 = s0*(1.0/32768), s1*(1.0/32768), s2*(1.0/32768), s3*(1.0/32768)
			} else {
				s0, s1, s2, s3 = v.at(i-1), v.at(i), v.at(i+1), v.at(i+2)
			}
			y := s1 + 0.5*fr*(s2-s0+fr*(2*s0-5*s1+4*s2-s3+fr*(3*(s1-s2)+s3-s0)))
			dst[n] = y * v.level * v.gain
			v.pos += inc
			if v.loopActive {
				for v.pos >= float64(v.le) {
					v.pos -= float64(v.ll)
				}
			} else if v.pos >= float64(v.endIdx-1) {
				v.done = true
				break
			}
		}
	}
	v.age += float64(len(dst)) / SR
	return !v.done
}
