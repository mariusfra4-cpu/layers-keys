#!/bin/bash
# Construit l'installateur Windows : setup/Convertisseur-STY-Setup-1.0.0.exe
# Nécessite : go, makensis (paquet nsis), curl, sha256sum.
set -e
cd "$(dirname "$0")"
./build.sh >/dev/null
P=installer/payload
mkdir -p $P setup
cp bin/sty2audio.exe bin/sty2audio32.exe installer/Lisez-moi.txt $P/
SF=$P/GeneralUser-GS.sf2
SHA=9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe
BASE=https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/684543d5e5efaef08d02be50dcda8d552478fa60
if ! { [ -f $SF ] && echo "$SHA  $SF" | sha256sum -c --quiet; }; then
  curl -fsSL -o $SF "$BASE/GeneralUser-GS.sf2"
  echo "$SHA  $SF" | sha256sum -c --quiet
fi
[ -s $P/GeneralUser-GS-LICENSE.txt ] || curl -fsSL -o $P/GeneralUser-GS-LICENSE.txt "$BASE/documentation/LICENSE.txt"
(cd installer && makensis -V2 sty2audio.nsi)
mv installer/Convertisseur-STY-Setup-1.0.0.exe setup/
ls -la setup
