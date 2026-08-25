#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
user_bin_dir="${HOME}/.local/bin"
mkdir -p "$user_bin_dir"
ln -sfn "$repo_dir/run-rayneo" "$user_bin_dir/rayneo"

echo "Installed: $user_bin_dir/rayneo"
if [[ ":${PATH}:" != *":$user_bin_dir:"* ]]; then
    echo "Add this directory to PATH if 'rayneo' is not found:"
    echo "  export PATH=\"$user_bin_dir:\$PATH\""
fi
echo "Start the viewer with: rayneo"
