#!/bin/bash
# Construit « Layers Keys.app » pour Mac (Apple Silicon + Intel dans un seul binaire), sans compilateur C.
# Fonctionne depuis Linux, macOS ou Windows (bash) : il suffit de Go.
set -e
cd "$(dirname "$0")"
OUT=dist/mac
rm -rf "$OUT" && mkdir -p "$OUT/arm" "$OUT/app/Layers Keys.app/Contents/MacOS" "$OUT/app/Layers Keys.app/Contents/Resources"
for a in arm64 amd64; do
  CGO_ENABLED=0 GOOS=darwin GOARCH=$a go build -trimpath -ldflags "-s -w" -o "$OUT/arm/LayersKeys_$a" .
done
APP="$OUT/app/Layers Keys.app/Contents"
go run tools/lipo/main.go "$APP/MacOS/LayersKeys" "$OUT/arm/LayersKeys_arm64" "$OUT/arm/LayersKeys_amd64"
chmod 755 "$APP/MacOS/LayersKeys"
cp mac/Info.plist "$APP/Info.plist"
cp mac/icon.icns "$APP/Resources/icon.icns"
printf 'APPL????' > "$APP/PkgInfo"
python3 - <<'PY'
import os, zipfile
root = "dist/mac/app"
z = zipfile.ZipFile("dist/LayersKeys_Mac.zip", "w", zipfile.ZIP_DEFLATED)
for d, _, fs in os.walk(root):
    for f in sorted(fs):
        p = os.path.join(d, f)
        zi = zipfile.ZipInfo(os.path.relpath(p, root), date_time=(2026, 1, 1, 0, 0, 0))
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = (0o755 if f == "LayersKeys" else 0o644) << 16
        z.writestr(zi, open(p, "rb").read())
z.writestr(zipfile.ZipInfo("LISEZMOI_Mac.txt", date_time=(2026, 1, 1, 0, 0, 0)), open("mac/LISEZMOI_Mac.txt", "rb").read())
z.writestr(zipfile.ZipInfo("COMMENT_CONSTRUIRE_MAC.txt", date_time=(2026, 1, 1, 0, 0, 0)), open("mac/COMMENT_CONSTRUIRE_MAC.txt", "rb").read())
z.close()
PY
ls -la dist/LayersKeys_Mac.zip
