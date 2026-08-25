#!/usr/bin/env bash
set -euo pipefail

if ! command -v qdbus6 >/dev/null 2>&1 || ! command -v gdbus >/dev/null 2>&1; then
    echo "qdbus6 and gdbus are required on KDE Plasma." >&2
    exit 1
fi

manager=(qdbus6 org.kde.KWin /VirtualDesktopManager)
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
count=$(${manager[@]} count | grep -oE '[0-9]+' | tail -n 1)

if [[ "$count" == "1" ]]; then
    center_id=$(qdbus6 --literal org.kde.KWin /VirtualDesktopManager desktops |
        grep -oE '"[0-9a-f-]{36}", "[^"]+"' |
        head -n 1 | sed -E 's/^"([0-9a-f-]{36})".*/\1/')
    "${manager[@]}" setDesktopName "$center_id" Center >/dev/null
    "${manager[@]}" createDesktop 1 Right >/dev/null
elif [[ "$count" == "2" ]]; then
    echo "KDE already has two workspaces; leaving existing windows in place."
else
    echo "KDE currently has $count workspaces. This helper expects one or two." >&2
    exit 2
fi

gdbus call --session \
    --dest org.kde.KWin \
    --object-path /VirtualDesktopManager \
    --method org.freedesktop.DBus.Properties.Set \
    org.kde.KWin.VirtualDesktopManager rows '<uint32 1>' >/dev/null
qdbus6 org.kde.KWin /KWin setCurrentDesktop 1 >/dev/null
"$script_dir/enable-rayneo-kwin-sticky.sh"

cat <<'EOF'
Simple RayNeo workspace setup ready:
  desktop 1: Center
  desktop 2: Right

Put Chrome/YouTube on the source monitor of desktop 2.
EOF
