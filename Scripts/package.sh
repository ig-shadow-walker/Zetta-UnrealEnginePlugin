#!/usr/bin/env bash
# Builds a ready-to-install copy of the Zetta 3D plugin for one engine version, with prebuilt binaries, so
# Blueprint-only projects (no C++ toolchain) can use it. Nothing is uploaded: the zip lands in dist/.
#
#   Scripts/package.sh "/Users/Shared/Epic Games/UE_5.8"            # macOS
#   Scripts/package.sh "C:/Program Files/Epic Games/UE_5.8"         # Windows (Git Bash)
#
# Build each engine version you support on each platform you ship (macOS for Mac, Windows for Win64), then
# merge the Binaries/ folders of the same engine version into one zip if you want a single download.
set -euo pipefail

ENGINE="${1:?Usage: Scripts/package.sh <path to the engine, e.g. /Users/Shared/Epic Games/UE_5.8>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(python3 -c "import json,sys; print(json.load(open('$ROOT/Zetta3D.uplugin'))['VersionName'])")"
ENGINE_VERSION="$(python3 -c "import json; v=json.load(open('$ENGINE/Engine/Build/Build.version')); print(f\"{v['MajorVersion']}.{v['MinorVersion']}\")")"

case "$(uname -s)" in
  Darwin) PLATFORMS="Mac"; UAT="$ENGINE/Engine/Build/BatchFiles/RunUAT.sh" ;;
  Linux) PLATFORMS="Linux"; UAT="$ENGINE/Engine/Build/BatchFiles/RunUAT.sh" ;;
  *) PLATFORMS="Win64"; UAT="$ENGINE/Engine/Build/BatchFiles/RunUAT.bat" ;;
esac

OUT="$ROOT/dist/Zetta3D-$VERSION-UE$ENGINE_VERSION-$PLATFORMS"
rm -rf "$OUT"
mkdir -p "$ROOT/dist"

"$UAT" BuildPlugin -Plugin="$ROOT/Zetta3D.uplugin" -Package="$OUT/Zetta3D" -TargetPlatforms="$PLATFORMS" -Rocket 2>&1 \
  | tee "$ROOT/dist/build-UE$ENGINE_VERSION-$PLATFORMS.log"

# Stamp the engine version so the editor doesn't ask to rebuild it, and keep only what an install needs.
python3 - "$OUT/Zetta3D/Zetta3D.uplugin" "$ENGINE_VERSION" <<'EOF'
import json, sys
path, engine = sys.argv[1], sys.argv[2]
data = json.load(open(path))
data["EngineVersion"] = engine + ".0"
json.dump(data, open(path, "w"), indent="\t")
EOF
rm -rf "$OUT/Zetta3D/Intermediate"

(cd "$OUT" && zip -qr "../Zetta3D-$VERSION-UE$ENGINE_VERSION-$PLATFORMS.zip" Zetta3D)
echo "Packaged: dist/Zetta3D-$VERSION-UE$ENGINE_VERSION-$PLATFORMS.zip"
