#!/usr/bin/env bash
# Run a NodalKit application on a private session bus with an accessibility bus
# and (unless WAYLAND_DISPLAY is already set) a headless Weston compositor, then
# drive it with atspi_live_check.py.
#
# Usage: tools/atspi_live_check.sh <application-binary> "<Application Name>"
# Needs: dbus-run-session, at-spi2-core, weston, python3-gi, gir1.2-atspi-2.0.
set -euo pipefail

if [[ -z "${NK_ATSPI_CHECK_SESSION:-}" ]]; then
    exec env NK_ATSPI_CHECK_SESSION=1 dbus-run-session -- "$0" "$@"
fi

app_binary=$1
app_name=$2
tools_dir=$(cd "$(dirname "$0")" && pwd)

launcher=""
for candidate in /usr/libexec/at-spi-bus-launcher /usr/lib/at-spi2-core/at-spi-bus-launcher; do
    if [[ -x "$candidate" ]]; then
        launcher=$candidate
        break
    fi
done
if [[ -z "$launcher" ]]; then
    echo "at-spi-bus-launcher not found; install at-spi2-core" >&2
    exit 2
fi

pids=()
cleanup() {
    for pid in "${pids[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT

"$launcher" --launch-immediately &
pids+=($!)

if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
    export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-$(mktemp -d)}
    chmod 700 "$XDG_RUNTIME_DIR"
    weston --backend=headless-backend.so --socket=nk-atspi-check --idle-time=0 &
    pids+=($!)
    export WAYLAND_DISPLAY=nk-atspi-check
    sleep 2
fi

"$app_binary" &
pids+=($!)

python3 "$tools_dir/atspi_live_check.py" "$app_name" 20
