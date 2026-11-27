#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$MODULE" == hyperdsp && "$PUBLIC_NAME" == hyperdsp ]]
if [[ "$TARGET" != linux-x86_64 ]]; then
    pin=e9eb704437e606721ee92d7d064c4b8168fd8f53
    engine="$RUNNER_TEMP/obs-ci-engine"
    git init -q "$engine"
    git -C "$engine" fetch --depth=1 https://github.com/myrqyry/obs-plugin-ci.git "$pin"
    git -C "$engine" checkout --detach FETCH_HEAD
    [[ "$(git -C "$engine" rev-parse HEAD)" == "$pin" ]]
    rustup toolchain install 1.93.1 --profile minimal
    cargo +1.93.1 build --release --locked --manifest-path "$engine/Cargo.toml"
    exec python "$engine/scripts/native_caller.py" "$root"
fi
case "$OBS_VERSION" in
  32.2.0) layout=legacy ;;
  33.0.3) layout=modern ;;
  *) echo "Unsupported OBS SDK: $OBS_VERSION" >&2; exit 1 ;;
esac
[[ "$ARCHIVE" == "hyperdsp-linux-x86_64-obs$OBS_VERSION-test.tar.gz" ]]
sudo apt-get install -y --no-install-recommends ladspa-sdk libsamplerate0-dev
pin=e9eb704437e606721ee92d7d064c4b8168fd8f53
engine="$RUNNER_TEMP/obs-ci-engine"
git init -q "$engine"
git -C "$engine" fetch --depth=1 https://github.com/myrqyry/obs-plugin-ci.git "$pin"
git -C "$engine" checkout --detach FETCH_HEAD
[[ "$(git -C "$engine" rev-parse HEAD)" == "$pin" ]]
rustup toolchain install 1.93.1 --profile minimal
cargo +1.93.1 build --release --locked --manifest-path "$engine/Cargo.toml"
obsci="$engine/target/release/obs-ci"
work="$RUNNER_TEMP/hyperdsp-build"
mkdir "$work"
bash "$engine/scripts/prepare-linux-sdk.sh" "$work/obs" "$OBS_VERSION"
sdk="$work/obs/sdk"
export LD_LIBRARY_PATH="$sdk/lib"
test -s "$sdk/include/obs/obs-module.h"
test -s "$sdk/lib/libobs.so"
test -s "$sdk/lib/libobs-frontend-api.so"
test -s /usr/lib/ladspa/amp.so
cmake -S "$root" -B "$work/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DOBS_INCLUDE_DIR="$sdk/include/obs" \
  -DOBS_LIBRARY="$sdk/lib/libobs.so" -DOBS_FRONTEND_LIBRARY="$sdk/lib/libobs-frontend-api.so" \
  -DBUILD_TESTING=ON -DHYPERDSP_BUILD_BUILDER=ON -DHYPERDSP_BUILD_CLOCK_SYNC=ON \
  -DHYPERDSP_PACKAGE_LAYOUT="$layout"
cmake --build "$work/build" --parallel 4
mkdir "$work/home"
export HOME="$work/home" XDG_CONFIG_HOME="$work/home"
xvfb-run -a ctest --test-dir "$work/build" --output-on-failure --no-tests=error --output-junit "$work/ctest.xml"
python3 "$root/tests/verify-ci-report.py" "$work/ctest.xml" "$MINIMUM_TESTS"
mkdir "$work/stage"
cmake --install "$work/build" --prefix "$work/stage/hyperdsp"
"$obsci" pack --stage "$work/stage" --output "$RUNNER_TEMP/plugin-ci" --archive-name "$ARCHIVE"
archive="$RUNNER_TEMP/plugin-ci/$ARCHIVE"
"$obsci" inspect-module "$archive" --module hyperdsp --target linux-x86_64 --obs-version "$OBS_VERSION"
python3 "$root/tests/verify-ci-package.py" "$archive" "$work/build" "$OBS_VERSION" "$work/extracted"
if [[ "$OBS_VERSION" == 32.2.0 ]]; then
    source="$RUNNER_TEMP/plugin-ci-source"
    mkdir "$source"
    git -C "$root" archive --format=tar.gz --prefix=hyperdsp/ -o "$source/hyperdsp-source.tar.gz" HEAD -- . ':!.meristem' ':!AGENTS.md'
    (cd "$source" && sha256sum hyperdsp-source.tar.gz > SHA256SUMS)
    "$obsci" payload-manifest "$archive" > "$source/PAYLOAD-SHA256SUMS"
    "$obsci" audit-source "$source/hyperdsp-source.tar.gz" --binary "$archive"
fi
