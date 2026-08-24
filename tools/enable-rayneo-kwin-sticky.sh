#!/usr/bin/env bash
set -euo pipefail

if ! command -v qdbus6 >/dev/null 2>&1; then
    echo "qdbus6 was not found. This helper requires KDE Plasma 6." >&2
    exit 1
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
script_path="$script_dir/rayneo-kwin-sticky.js"

qdbus6 org.kde.KWin /Scripting unloadScript rayneo-kwin-sticky >/dev/null 2>&1 || true
script_id=$(qdbus6 org.kde.KWin /Scripting loadScript "$script_path" rayneo-kwin-sticky)
qdbus6 org.kde.KWin "/Scripting/Script${script_id}" org.kde.kwin.Script.run >/dev/null

echo "KWin integration enabled: the RayNeo viewport will stay visible on all desktops."
