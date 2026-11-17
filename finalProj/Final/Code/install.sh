#!/bin/bash
# install.sh - set up the Raspberry Pi so PARMCO starts by itself at power-up,
# in phone-app mode, with no keyboard, monitor or internet. (AI-generated)
#
#   sudo ./install.sh              build, enable I2C, set up Bluetooth, install
#                                  and start the boot service
#   sudo ./install.sh --console    also boot to the text console (no desktop),
#                                  without being asked
#   sudo ./install.sh --uninstall  stop and remove the boot service
#
# Safe to run more than once.
set -e

if [ "$(id -u)" -ne 0 ]; then exec sudo "$0" "$@"; fi

DIR="$(cd "$(dirname "$0")" && pwd)"
OWNER="$(stat -c %U "$DIR")"
UNIT=/etc/systemd/system/parmco.service
CONSOLE=ask

for a in "$@"; do
  case "$a" in
    --console) CONSOLE=yes ;;
    --keep-desktop) CONSOLE=no ;;
    --uninstall)
      systemctl disable --now parmco 2>/dev/null || true
      rm -f "$UNIT"
      systemctl daemon-reload
      echo "PARMCO boot service removed."
      exit 0 ;;
    *) echo "unknown option: $a"; exit 1 ;;
  esac
done

step() { echo; echo "== $*"; }

step "Checking build dependencies"
missing=""
for p in libbluetooth-dev libglib2.0-dev; do
  dpkg -s "$p" >/dev/null 2>&1 || missing="$missing $p"
done
if [ -n "$missing" ]; then
  echo "Missing:$missing"
  echo "Install them (needs internet once):  sudo apt install$missing"
  exit 1
fi
if ! ls /usr/lib/libE4235* /usr/local/lib/libE4235* /usr/lib/*/libE4235* >/dev/null 2>&1; then
  echo "Warning: libE4235 (class library) not found in the usual places; the build may fail."
fi

step "Enabling I2C for the AS5600 encoder"
if command -v raspi-config >/dev/null; then
  raspi-config nonint do_i2c 0
fi
grep -qx "i2c-dev" /etc/modules || echo "i2c-dev" >> /etc/modules
modprobe i2c-dev || true

step "Bluetooth: allow re-pairing after the phone forgets the PARMCO"
CONF=/etc/bluetooth/main.conf
if grep -qE "^[#[:space:]]*JustWorksRepairing" "$CONF"; then
  sed -i -E "s/^[#[:space:]]*JustWorksRepairing.*/JustWorksRepairing = always/" "$CONF"
else
  sed -i "/^\[General\]/a JustWorksRepairing = always" "$CONF"
fi
grep -n "^JustWorksRepairing" "$CONF"
rfkill unblock bluetooth || true

step "Building parmco_final as $OWNER"
sudo -u "$OWNER" make -C "$DIR"
sudo -u "$OWNER" mkdir -p "$DIR/logs"

step "Installing the boot service"
sed "s|@DIR@|$DIR|g" "$DIR/parmco.service" > "$UNIT"
systemctl daemon-reload
systemctl enable parmco
systemctl restart bluetooth
systemctl restart parmco

if [ "$CONSOLE" = ask ] && [ -t 0 ]; then
  echo
  echo "The desktop's Bluetooth applet can compete with PARMCO's pairing agent"
  echo "and cause repeated pairing pop-ups. Booting to the text console avoids it."
  read -r -p "Boot to the text console (auto-login) instead of the desktop? [y/N] " ans
  case "$ans" in [yY]*) CONSOLE=yes ;; *) CONSOLE=no ;; esac
fi
if [ "$CONSOLE" = yes ] && command -v raspi-config >/dev/null; then
  raspi-config nonint do_boot_behaviour B2
  echo "Boot set to console auto-login (undo: sudo raspi-config > System > Boot)."
fi

sleep 2
step "Done"
systemctl --no-pager --lines=8 status parmco || true
cat <<MSG

PARMCO now starts by itself at every power-up, motor off, waiting for the app.
  make status    is it running?
  make logs      live output (Ctrl-C to leave)
  make restart   rebuild and restart after a code change
  make run       stop the service and run by hand in this terminal
CSV logs are written to $DIR/logs
MSG
