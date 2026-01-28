#!/bin/sh
set -e

echo "Stopping barny processes..."
pkill -f barny-weather 2>/dev/null || true
pkill -f barny-btc-price 2>/dev/null || true
pkill -f barny-cpu-freq 2>/dev/null || true
pkill -f barny-cpu-power 2>/dev/null || true
pkill -x barny 2>/dev/null || true

echo "Removing installed files..."
rm -f /usr/bin/barny
rm -f /usr/bin/barny-weather
rm -f /usr/bin/barny-btc-price
rm -f /usr/bin/barny-cpu-freq
rm -f /usr/bin/barny-cpu-power
rm -f /usr/lib/systemd/user/barny.service
rm -f /usr/lib/systemd/user/barny-weather.service
rm -f /usr/lib/systemd/user/barny-btc-price.service
rm -f /usr/lib/systemd/user/barny-cpu-freq.service
rm -f /usr/lib/systemd/user/barny-cpu-power.service
rm -rf /etc/barny
rm -rf /usr/etc/barny

echo "Removing data directory..."
rm -rf /opt/barny

echo "Barny uninstalled successfully."
echo ""
echo "Run as your regular user to clean up systemd:"
echo "  systemctl --user daemon-reload"
