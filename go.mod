module minipiano

go 1.21

require (
	github.com/ebitengine/oto/v3 v3.3.3
	github.com/ebitengine/purego v0.8.0
	golang.org/x/image v0.20.0
	golang.org/x/sys v0.25.0
	golang.org/x/text v0.18.0
)

replace github.com/ebitengine/oto/v3 => ./third_party/oto

replace github.com/ebitengine/purego => ./third_party/purego8

replace golang.org/x/image => ./third_party/image

replace golang.org/x/sys => ./third_party/sys

replace golang.org/x/text => ./third_party/text
