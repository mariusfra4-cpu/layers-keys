#!/bin/bash
cd "$(dirname "$0")"
OUT=${OUT:-/tmp/emu}; mkdir -p "$OUT"
python3 emu.py "$OUT" "$1" 2>&1 | tee "$OUT/resultat.txt"
exit ${PIPESTATUS[0]}
