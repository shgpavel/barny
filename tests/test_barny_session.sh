#!/bin/sh
set -eu

SOURCE_ROOT="${1:-}"
if [ -z "$SOURCE_ROOT" ]; then
    echo "usage: $0 <source-root>" >&2
    exit 1
fi

SESSION_SCRIPT="$SOURCE_ROOT/config/barny-session.in"
if [ ! -f "$SESSION_SCRIPT" ]; then
    echo "error: session script not found: $SESSION_SCRIPT" >&2
    exit 1
fi

TMPDIR="$(mktemp -d)"
cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT INT TERM

# Regression guard: when running as root, XDG_RUNTIME_DIR must be ignored.
FAKEBIN="$TMPDIR/fakebin"
mkdir -p "$FAKEBIN"
cat > "$FAKEBIN/id" <<'EOF'
#!/bin/sh
if [ "${1:-}" = "-u" ]; then
    echo 0
    exit 0
fi
exec /bin/id "$@"
EOF
chmod +x "$FAKEBIN/id"

XDG_ROOT_TEST="$TMPDIR/xdg-root"
mkdir -p "$XDG_ROOT_TEST/barny"
printf '12345\n' > "$XDG_ROOT_TEST/barny/barny.pid"

PATH="$FAKEBIN:/bin" \
XDG_RUNTIME_DIR="$XDG_ROOT_TEST" \
HOME="$TMPDIR/home-root" \
sh "$SESSION_SCRIPT" stop >/dev/null 2>&1 || true

if [ ! -f "$XDG_ROOT_TEST/barny/barny.pid" ]; then
    echo "error: root-mode stop should not use XDG_RUNTIME_DIR pidfiles" >&2
    exit 1
fi

# Regression guard: invalid pid values like -1 must not be treated as running.
XDG_PID_TEST="$TMPDIR/xdg-pid"
mkdir -p "$XDG_PID_TEST/barny"
printf -- '-1\n' > "$XDG_PID_TEST/barny/barny.pid"

STATUS_OUTPUT="$TMPDIR/status.out"
PATH="/bin" \
XDG_RUNTIME_DIR="$XDG_PID_TEST" \
HOME="$TMPDIR/home-pid" \
sh "$SESSION_SCRIPT" status >"$STATUS_OUTPUT" 2>&1 || true

status_text="$(cat "$STATUS_OUTPUT")"
case "$status_text" in
    *"barny: stopped"*)
        ;;
    *)
        echo "error: expected invalid pid to report barny as stopped" >&2
        echo "$status_text" >&2
        exit 1
        ;;
esac

if [ -f "$XDG_PID_TEST/barny/barny.pid" ]; then
    echo "error: invalid pid file should be removed by status path" >&2
    exit 1
fi
