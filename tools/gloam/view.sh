#!/bin/sh
# Run on the caller's actual desktop. Never substitute an offscreen display.
set -eu
cd "$(dirname "$0")/../.."
if [ -n "${WAYLAND_SOCKET:-}" ]; then
    [ -S "/proc/self/fd/$WAYLAND_SOCKET" ] || { echo '继承的 Wayland socket 不可访问' >&2; exit 1; }
else
    case "${WAYLAND_DISPLAY:?请从当前 Wayland 桌面启动}" in
        /*) socket=$WAYLAND_DISPLAY ;;
        *) socket=${XDG_RUNTIME_DIR:?缺少 XDG_RUNTIME_DIR}/$WAYLAND_DISPLAY ;;
    esac
    [ -S "$socket" ] && [ -w "$socket" ] || { echo "无法访问 Wayland socket：$socket" >&2; exit 1; }
fi
unset DISPLAY
export BEVY_ASSET_ROOT="$PWD"
exec cargo run --locked --release --no-default-features -- "$@"
