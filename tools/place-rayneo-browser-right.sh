#!/usr/bin/env bash
set -euo pipefail

if ! command -v qdbus6 >/dev/null 2>&1; then
    echo "qdbus6 was not found. This helper requires KDE Plasma 6." >&2
    exit 1
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
script_name=rayneo-place-browser-right
qdbus6 org.kde.KWin /Scripting unloadScript "$script_name" >/dev/null 2>&1 || true
script_id=$(qdbus6 org.kde.KWin /Scripting loadScript \
    "$script_dir/rayneo-place-browser-right.js" "$script_name")
qdbus6 org.kde.KWin "/Scripting/Script${script_id}" org.kde.kwin.Script.run >/dev/null
qdbus6 org.kde.KWin /Scripting unloadScript "$script_name" >/dev/null 2>&1 || true

echo "Existing Chrome/Chromium windows moved to KDE desktop 2 on the source monitor."
