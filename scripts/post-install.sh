#!/bin/sh
set -e

echo "Creating data directory..."
mkdir -p /opt/barny/modules
chmod 1777 /opt/barny/modules

echo "Barny installed successfully."
echo ""
echo "To enable and start barny, run as your regular user (not root):"
echo "  systemctl --user daemon-reload"
echo "  systemctl --user enable --now barny.service"
