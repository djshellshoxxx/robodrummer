#!/usr/bin/env bash
# Run inside an existing X display (normally xvfb-run). Actual popup peers need
# an EWMH manager; bare Xvfb is insufficient for JUCE's native popup properties.
set -euo pipefail
if [ "$#" -eq 0 ] || [ -z "${DISPLAY:-}" ]; then
  echo '::error::Usage: xvfb-run -a bash scripts/ci/with_window_manager.sh <command> [args...]' >&2
  exit 1
fi
command -v xprop >/dev/null || { echo '::error::xprop (x11-utils) is required for GUI readiness' >&2; exit 1; }
manager_ready() {
  local manager
  manager="$(xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | sed -nE 's/.*window id # (0x[0-9a-fA-F]+).*/\1/p')" || return 1
  [[ "$manager" =~ ^0x[0-9a-fA-F]+$ ]] && (( manager != 0 )) || return 1
  xprop -id "$manager" _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -Fq "window id # $manager"
}
if manager_ready; then exec "$@"; fi
command -v openbox >/dev/null || { echo '::error::Openbox is required for native GUI popup checks' >&2; exit 1; }
temporary="$(mktemp -d)"
manager_pid=''
cleanup() {
  if [ -n "$manager_pid" ]; then kill "$manager_pid" 2>/dev/null || true; wait "$manager_pid" 2>/dev/null || true; fi
  rm -rf "$temporary"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# Keep every generated WM file isolated; never redirect HOME or overwrite a
# developer's settings when the wrapper is used locally.
mkdir -p "$temporary/cache"
cat > "$temporary/rc.xml" <<'XML'
<openbox_config xmlns="http://openbox.org/3.4/rc"><theme><name>Clearlooks</name></theme><focus><focusNew>yes</focusNew></focus></openbox_config>
XML
XDG_CACHE_HOME="$temporary/cache" openbox --sm-disable --config-file "$temporary/rc.xml" > "$temporary/openbox.log" 2>&1 &
manager_pid=$!
deadline=$((SECONDS + 10))
until manager_ready; do
  if ! kill -0 "$manager_pid" 2>/dev/null || (( SECONDS >= deadline )); then
    cat "$temporary/openbox.log" >&2
    echo '::error::Window manager did not become ready; GUI checks were not run' >&2
    exit 1
  fi
  sleep 0.05
done
"$@"
