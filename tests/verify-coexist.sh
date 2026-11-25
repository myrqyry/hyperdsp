#!/usr/bin/env bash
# Test real modules in isolated libobs; no user plugin/profile changes.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
sound="${1:?Usage: verify-coexist.sh /path/to/sound [git-ref]}"
ref="${2:-HEAD}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir "$work/sound"
git -C "$sound" archive "$ref" | tar -x -C "$work/sound"
cmake -S "$work/sound" -B "$work/sound-build" -G Ninja \
    -DFETCH_OBS=OFF -DSOUND_EXPERIMENTAL=ON -DSOUND_EFFECTS=yamnet \
    -DSOUND_TOOLS_MENU=OFF -DCMAKE_BUILD_TYPE=Release \
    -DOBS_PLUGIN_DESTINATION=bin/64bit -DOBS_DATA_DESTINATION=data
cmake --build "$work/sound-build" --parallel 4
cmake --install "$work/sound-build" --prefix "$work/stage"
cmake -S "$root" -B "$work/dsp-build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/dsp-build" --parallel 4
ctest --test-dir "$work/dsp-build" --output-on-failure
args=("$work/dsp-build/hyperdsp.so" "$root"
      "$work/stage/bin/64bit/live-latent-lab-sound.so" "$work/stage/data")
for order in hyperdsp-first sound-first; do
    extra=()
    if [[ "$order" == sound-first ]]; then extra+=(sound-first); fi
    timeout 30 "$work/dsp-build/verify-mic-obs" "${args[@]}" "${extra[@]}" >"$work/$order.log" 2>&1 || {
        cat "$work/$order.log"
        exit 1
    }
    printf '%s: ' "$order"
    grep 'MIC_OBS_ACTUAL_AUDIO' "$work/$order.log"
done
echo HYPERDSP_SOUND_COEXIST_PASS=1
