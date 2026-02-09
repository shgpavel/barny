#!/bin/sh
set -e

prefix="${1:-/usr}"
bindir="${2:-bin}"
if [ -z "$bindir" ]; then
    bindir="bin"
fi
case "$bindir" in
    /*) bindir_path="$bindir" ;;
    *) bindir_path="$prefix/$bindir" ;;
esac
session_bin="$bindir_path/barny-session"

echo "Creating data directory..."
mkdir -p /opt/barny/modules
chmod 1777 /opt/barny/modules

echo "Barny installed successfully."
echo ""

if command -v systemctl >/dev/null 2>&1 && [ -f /usr/lib/systemd/user/barny.service ]; then
    echo "To enable and start barny, run as your regular user (not root):"
    echo "  systemctl --user daemon-reload"
    echo "  systemctl --user enable --now barny.service"
elif command -v barny-session >/dev/null 2>&1 || [ -x "$session_bin" ]; then
    echo "To start barny with OpenRC/supervise-daemon, add to your Sway config:"
    echo "  exec barny-session start"
    echo ""
    echo "Or run manually:"
    echo "  barny-session start"
    echo "  barny-session status"
    echo "  barny-session stop"
else
    echo "No service manager configured. Start barny manually:"
    echo "  barny &"
fi
