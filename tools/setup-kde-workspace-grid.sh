#!/usr/bin/env bash
set -euo pipefail

if ! command -v qdbus6 >/dev/null 2>&1 || ! command -v gdbus >/dev/null 2>&1; then
    echo "qdbus6 and gdbus are required on KDE Plasma." >&2
    exit 1
fi

manager=(qdbus6 org.kde.KWin /VirtualDesktopManager)
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

read_count() {
    "${manager[@]}" count | grep -oE '[0-9]+' | tail -n 1
}

read_desktops() {
    qdbus6 --literal org.kde.KWin /VirtualDesktopManager desktops
}

create_grid() {
    # Insert four workspaces before the user's existing desktop. This keeps
    # that desktop at position 5 instead of leaving it at the top-left.
    "${manager[@]}" createDesktop 0 "RayNeo Corner NW" >/dev/null
    "${manager[@]}" createDesktop 1 "RayNeo Above" >/dev/null
    "${manager[@]}" createDesktop 2 "RayNeo Corner NE" >/dev/null
    "${manager[@]}" createDesktop 3 "RayNeo Left" >/dev/null

    # Append the four workspaces after the user's existing center desktop.
    "${manager[@]}" createDesktop 5 "RayNeo Right" >/dev/null
    "${manager[@]}" createDesktop 6 "RayNeo Corner SW" >/dev/null
    "${manager[@]}" createDesktop 7 "RayNeo Below" >/dev/null
    "${manager[@]}" createDesktop 8 "RayNeo Corner SE" >/dev/null
}

count=$(read_count)
if [[ -z "$count" ]]; then
    echo "Could not read the KDE virtual-desktop count." >&2
    exit 1
fi

if (( count == 1 )); then
    create_grid
elif (( count == 9 )) && [[ "${1:-}" == "--repair-old-grid" ]]; then
    echo "Removing only desktops named RayNeo Grid ..."
    desktop_data=$(read_desktops)
    mapfile -t old_ids < <(
        printf '%s\n' "$desktop_data" |
            grep -oE '"[0-9a-f-]{36}", "RayNeo Grid [^"]+"' |
            sed -E 's/^"([0-9a-f-]{36})".*/\1/'
    )
    if (( ${#old_ids[@]} != 8 )); then
        echo "Expected 8 old RayNeo Grid desktops, found ${#old_ids[@]}." >&2
        echo "No desktops were removed." >&2
        exit 1
    fi
    for ((i=${#old_ids[@]}-1; i>=0; --i)); do
        qdbus6 org.kde.KWin /VirtualDesktopManager removeDesktop "${old_ids[i]}" >/dev/null
    done
    create_grid
else
    echo "KDE currently has $count virtual desktops."
    echo "This helper only configures a fresh one-desktop session." >&2
    echo "For the old RayNeo prototype grid, run: $0 --repair-old-grid" >&2
    echo "For another existing layout, configure the center workspace manually." >&2
    exit 2
fi

"$script_dir/enable-rayneo-kwin-sticky.sh"

gdbus call --session \
    --dest org.kde.KWin \
    --object-path /VirtualDesktopManager \
    --method org.freedesktop.DBus.Properties.Set \
    org.kde.KWin.VirtualDesktopManager rows '<uint32 3>' >/dev/null
qdbus6 org.kde.KWin /KWin setCurrentDesktop 5 >/dev/null

cat <<'EOF'
KDE workspace grid ready.

The existing main workspace is now in the center:
  desktop 5: center
  desktop 4: left
  desktop 6: right
  desktop 2: above
  desktop 8: below

The four corner desktops are intentionally unused.
EOF
