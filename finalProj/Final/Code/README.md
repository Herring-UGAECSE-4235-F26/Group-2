# PARMCO final controller (RP4 + iPhone app)

AI-generated (Claude). Built from the Checkpoint 3 code in `../../CP3/Code`, revised for the
final function check. If you make a human edit, mark it with
`/* [HUMAN EDIT] ... [END HUMAN EDIT] */` and keep the AI original commented out.

## One-time setup on the Pi (self-start in phone-app mode)

```bash
cd finalProj/Final/Code
sudo apt install libbluetooth-dev libglib2.0-dev   # once, needs internet
sudo ./install.sh
```

`install.sh` does the following:

1. Checks the build dependencies and builds `parmco_final`.
2. Turns on I2C for the encoder.
3. Sets `JustWorksRepairing = always` in `/etc/bluetooth/main.conf`, so the Pi re-pairs after
   the phone forgets it.
4. Installs and starts the boot service `parmco.service`.
5. Offers to boot to the text console. Say yes: the desktop's Bluetooth applet can fight
   PARMCO's pairing agent and cause repeated pairing pop-ups. Use `--console` to skip the
   question.

After that, every power-up goes like this, with no keyboard, monitor or internet:

```
power on → Linux boots → bluetoothd → parmco_final starts (motor off, enable low)
         → waits for the Bluetooth adapter → advertises "PARMCO" → waits for the app
app connects → defaults: Manual, CW, 20 RPM, stopped, not calibrated, feedback hold ON
```

| Command | What it does |
|---|---|
| `make status` | Shows whether the service is running |
| `make logs` | Shows live program output (`journalctl -u parmco -f`) |
| `make restart` | Rebuilds and restarts after a code change |
| `make run` | Stops the service and runs the program in this terminal |
| `sudo ./install.sh --uninstall` | Removes the boot service |

Only one copy can run at a time, because both would want the GPIO pins and the BLE service.
`make run` stops the service first. If you start `sudo ./parmco_final` by hand, run
`sudo systemctl stop parmco` first. The service starts again at the next boot.

**Encoder direction (once).** Calibrate zero, then turn the blade by hand to +90° on the
protractor.

- If the app shows about 90°, the direction is correct.
- If it shows about 270°, add `-i` to the `ExecStart` line in `parmco.service` and run
  `sudo ./install.sh` again.

## What changed for the final check

| Requirement | Change |
|---|---|
| RP4 boots and runs on its own | systemd service with auto-restart. It restarts along with `bluetoothd`. The BLE server waits up to 2 minutes for the adapter at boot instead of failing once. |
| Feedback hold after every set to zero / angle target | Hold is **on by default** and reset to on at every connect. Start with `-n` to turn it off by default. |
| Hold correction | A correction now ends inside the ±1° band (tolerance 0.95°). Before, it could stop exactly on the band edge and set off another correction. |
| Maintain (Test E) | Fixed a bug: the integral stopped updating once it passed 1 RPM. The integral no longer builds up while the blade is being pushed. A change of desired RPM resets the trim. Each re-sync is sent as `EVT:MAINTAIN_RESYNC` and logged as `maintain_resync_N`. |
| Logs for the writeup | Logs are owned by the repo's user, not root, so `git add` works. A file name is never reused, which matters because the Pi's clock can repeat with no internet. A mark sent just before Stop Logging is still written. |
| App | The hold toggle shows on by default. A Maintain caption names the feedback source. |
| IR sensor calibration (extra) | **Test controls › Calibrate IR sensor** turns the motor and hold off so the blade turns freely. The Pi streams the raw sensor state 20 times a second. The app walks through three steps: (1) blades away from the sensor, turn the potentiometer until the reading stays Clear for 2 s; (2) a blade in front of the sensor, detection confirmed after 1 s; (3) an optional hand-spin check showing pulse count and IR RPM. Start, Set to Zero and angle moves are refused while it is open (`ERR:IRCAL_ACTIVE`). |

Everything else (commands, telemetry, logging columns, angle moves, over-range unlock,
input rejection) is the same as CP3. See `../Documentation/AI_Software_Documentation.md`.

## Maintain: feedback source (for the writeup)

Maintain uses the **encoder-derived RPM**: the encoder is read at 100 Hz, and RPM is the
angle change over a 0.5 s window. If the encoder is unavailable, it falls back to the IR
RPM. The encoder is used because at 20 RPM the two-blade IR signal gives one pulse every
1.5 s, which is too slow to correct a disturbance.

A stepper that stays in sync turns at exactly the commanded step rate, so Maintain does two
jobs:

- **PI trim.** Every 0.1 s a small trim (P 0.2, I 0.5/s, each limited to ±10% of the
  desired RPM) removes any steady difference between the commanded and measured RPM.
- **Lost-sync recovery.** If the measured RPM stays below half the desired RPM for 0.3 s
  (the blade is held or has stalled), the program re-aligns the coil pattern with the
  rotor's measured angle and restarts the ramp. After a push is released, the motor is
  back at the desired RPM in about 1 s.

## Pre-demo checklist (Thursday)

1. Power-cycle the Pi with the propeller off. The motor must stay quiet, and the app must
   find "PARMCO".
2. Run Stop and confirm the blade turns freely by hand.
3. Delete the app from the iPhone, reinstall it from Xcode, then *Forget This Device* in
   iOS Settings › Bluetooth. Reconnect from the app; pairing should happen once with no
   pop-up on the Pi.
4. Calibrate zero at 0°, then press Set to Zero. The blade should land at 0° and the status
   should show **Holding** with the coils off.
5. Run Tests A–G in order, with G last. Log each test with a label, e.g.
   `LOG:START:testC`.
