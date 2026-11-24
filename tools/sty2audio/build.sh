#!/bin/bash
# Compile sty2audio pour Windows (64 et 32 bits), Linux et macOS.
set -e
cd "$(dirname "$0")"
mkdir -p bin
GOOS=windows GOARCH=amd64 go build -trimpath -ldflags "-s -w" -o bin/sty2audio.exe .
GOOS=windows GOARCH=386   go build -trimpath -ldflags "-s -w" -o bin/sty2audio32.exe .
GOOS=linux   GOARCH=amd64 go build -trimpath -ldflags "-s -w" -o bin/sty2audio-linux .
GOOS=darwin  GOARCH=arm64 go build -trimpath -ldflags "-s -w" -o bin/sty2audio-mac-arm64 .
GOOS=darwin  GOARCH=amd64 go build -trimpath -ldflags "-s -w" -o bin/sty2audio-mac-intel .
ls -la bin
