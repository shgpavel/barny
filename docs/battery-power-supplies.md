# Battery / power-supply sysfs notes

Reference for the `battery` module and a future "all batteries" popup (laptop +
wireless mouse/keyboard/headset/controller).

## Where the data lives

Everything is under `/sys/class/power_supply/<name>/`. Each supply exposes
individual attribute files (`type`, `status`, `capacity`, ...) and a combined
`uevent` file with the same values as `POWER_SUPPLY_*=` lines. The `battery`
module reads `uevent` (one open, line-parsed) rather than many small files.

`readdir()` order here is **not** sorted or stable — never rely on "the first
entry". Match on attributes instead.

## The three kinds of supply (real data from this laptop)

### 1. AC adapter — `POWER_SUPPLY_TYPE=Mains` (ignore for battery %)
```
POWER_SUPPLY_NAME=ADP1
POWER_SUPPLY_TYPE=Mains
POWER_SUPPLY_ONLINE=1          # 1 = plugged in, 0 = on battery
```
Useful as the "is the charger connected?" signal.

### 2. System battery — `type=Battery`, no `SCOPE` (or `SCOPE=System`)
```
POWER_SUPPLY_NAME=BAT0
POWER_SUPPLY_TYPE=Battery
POWER_SUPPLY_STATUS=Full       # Charging | Discharging | Full | Not charging
POWER_SUPPLY_PRESENT=1
POWER_SUPPLY_CAPACITY=100      # percent
POWER_SUPPLY_CAPACITY_LEVEL=Full
POWER_SUPPLY_ENERGY_NOW=71670000     # µWh  (or CHARGE_NOW in µAh on some HW)
POWER_SUPPLY_ENERGY_FULL=71670000
POWER_SUPPLY_ENERGY_FULL_DESIGN=71000000
POWER_SUPPLY_POWER_NOW=0             # µW instantaneous draw (for time-remaining)
POWER_SUPPLY_VOLTAGE_NOW=17249000
POWER_SUPPLY_CYCLE_COUNT=76
POWER_SUPPLY_TECHNOLOGY=Li-ion
POWER_SUPPLY_MODEL_NAME=L21L4PD8
POWER_SUPPLY_MANUFACTURER=LGES
```
Rich attribute set: energy, power draw, cycle count, health (ENERGY_FULL vs
ENERGY_FULL_DESIGN), etc.

### 3. Peripheral battery — `type=Battery` **and** `SCOPE=Device`
```
POWER_SUPPLY_NAME=hidpp_battery_0
POWER_SUPPLY_TYPE=Battery
POWER_SUPPLY_STATUS=Discharging
POWER_SUPPLY_CAPACITY=65
POWER_SUPPLY_SCOPE=Device      # <-- the key marker
POWER_SUPPLY_MODEL_NAME=PRO X Wireless
POWER_SUPPLY_MANUFACTURER=Logitech
```
Wireless mice, keyboards, headsets, and game controllers show up as batteries
too. They are distinguished by **`POWER_SUPPLY_SCOPE=Device`** and typically
expose only a minimal set (name, status, capacity, model/manufacturer). Names
are often `hidpp_battery_N` (Logitech HID++) or `hid-<mac>-battery`.

## The gotcha (and the bug it caused)

The system battery and a wireless-mouse battery are **both** `type=Battery`.
Selecting "the first `type=Battery`" picks whichever `readdir()` returns first —
here that was the mouse, so the bar showed `65% Discharging` while the laptop
was actually `100% Full` on AC.

**Rule:** to find the *laptop* battery, require `type=Battery` and reject
`SCOPE=Device`. Missing scope file == system battery. This is exactly what
`detect_battery()` in `src/modules/battery.c` now does.

## Future: an "all batteries" popup

Everything needed for a hover/click popup listing every battery is already in
sysfs — no daemon or D-Bus (UPower) required.

- **Enumerate:** scan `/sys/class/power_supply/*`, keep `type=Battery` (both
  scopes this time), read each `uevent` once.
- **Per row:** icon by `SCOPE`/name (laptop vs mouse/keyboard/headset), a label
  from `MODEL_NAME`/`MANUFACTURER` (fall back to `NAME`), `CAPACITY%`, and
  `STATUS`. Color the bar low/charging like the inline module already does
  (`current < 20` red, charging green, etc.).
- **Charger line:** show `Mains ONLINE=1/0` as a "Plugged in / On battery" header.
- **Time remaining (system battery only):** `ENERGY_NOW / POWER_NOW` hours when
  discharging, `(ENERGY_FULL - ENERGY_NOW) / POWER_NOW` when charging. Guard
  `POWER_NOW == 0`. Some hardware uses `CHARGE_*` (µAh) instead of `ENERGY_*`
  (µWh) — handle both.
- **Health (system battery):** `ENERGY_FULL / ENERGY_FULL_DESIGN` as a wear %.
- **Reuse the popup infra:** build it as a native-render popup like the tray
  menu (`src/modules/menu.c`) or the hover popups (`src/modules/popup.c` +
  `barny_popup_*`). Peripheral batteries update slowly, so a 5–10s refresh (the
  module's `update_interval_ms`) is plenty.

### Caveats
- Peripheral batteries appear/disappear as devices connect/sleep; enumerate
  live each refresh rather than caching a fixed device list at init.
- `CAPACITY` for HID++ devices can be coarse (buckets, or a `CAPACITY_LEVEL` of
  `Full/High/Normal/Low/Critical` instead of a precise percent) — fall back to
  `CAPACITY_LEVEL` when `CAPACITY` is absent.
- The inline `battery` module should keep showing only the **system** battery;
  the popup is where peripheral batteries belong.
