package main

import "testing"

func TestChords(t *testing.T) {
	cases := []struct {
		notes []int
		want  string
	}{
		{[]int{60, 64, 67}, "C"},
		{[]int{60, 63, 67}, "Cm"},
		{[]int{60, 63, 66}, "Cdim"},
		{[]int{60, 64, 68}, "Caug"},
		{[]int{60, 62, 67}, "Csus2"},
		{[]int{60, 65, 67}, "Csus4"},
		{[]int{60, 64, 67, 71}, "C Maj7"},
		{[]int{60, 64, 67, 70}, "C7"},
		{[]int{60, 63, 67, 70}, "Cm7"},
		{[]int{60, 63, 66, 70}, "Cm7b5"},
		{[]int{60, 63, 66, 69}, "Cdim7"},
		{[]int{60, 64, 67, 69}, "C6"},
		{[]int{60, 64, 67, 74}, "C(add9)"},
		{[]int{60, 64, 67, 70, 74}, "C9"},
		{[]int{60, 64, 67, 71, 74}, "C Maj9"},
		{[]int{60, 63, 67, 70, 74}, "Cm9"},
		{[]int{60, 64, 70, 74, 77}, "C11"},
		{[]int{60, 64, 67, 70, 74, 81}, "C13"},
		{[]int{60, 64, 67, 70, 61}, "C7(b9)"},
		{[]int{60, 64, 67, 70, 63 + 12}, "C7(#9)"},
		{[]int{60, 64, 67, 70, 66}, "C7(#11)"},
		{[]int{52, 60, 67}, "C/E"},
		{[]int{55, 60, 64}, "C/G"},
		{[]int{48, 55, 64, 70}, "C7"},
		{[]int{60, 67}, "C5"},
		{[]int{60}, "C"},
		{[]int{62, 66, 69, 72}, "D7"},
		{[]int{57, 60, 64, 67}, "Am7"},
		{[]int{61, 64, 68}, "C#m"},
		{[]int{68, 71, 75}, "G#m"},
		{[]int{61, 65, 68}, "Db"},
		{[]int{56, 59, 64}, "E/G#"},
		{[]int{60, 64, 69}, "Am/C"},
		{[]int{60, 61, 62}, ""},
		{[]int{63, 66, 70}, "Ebm"},
	}
	for _, c := range cases {
		got := lettersNamer.Detect(c.notes).Name
		if got != c.want {
			t.Errorf("%v : %q (attendu %q)", c.notes, got, c.want)
		}
	}
}

func TestChordsSolfege(t *testing.T) {
	n := ChordNamer{Names: noteNamesSolfege, Sharps: noteNamesSolfegeSharp, Spaced: true}
	for _, c := range []struct {
		notes []int
		want  string
	}{
		{[]int{60, 64, 67, 71}, "Do Maj7"},
		{[]int{62, 65, 69, 72}, "Ré m7"},
		{[]int{60, 64, 67}, "Do"},
		{[]int{52, 60, 67}, "Do/Mi"},
	} {
		if got := n.Detect(c.notes).Name; got != c.want {
			t.Errorf("%v : %q (attendu %q)", c.notes, got, c.want)
		}
	}
}
