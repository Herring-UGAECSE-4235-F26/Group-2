# PARMCO CP3 software (RP4 controller + iPhone app)

AI-generated (Claude), building on the team's earlier AI-generated CP3 code.
No human edits yet. If you make one, mark it with
`/* [HUMAN EDIT] ... [END HUMAN EDIT] */` and keep the AI original commented out.

## Files

| File | Role |
|---|---|
| `parmco_cp3_controller.c` | Main program: motor thread, encoder/IR/log/telemetry threads, command parser, RFCOMM server (Android) |
| `ble_server.c/.h` | BLE GATT server for the iPhone app (BlueZ D-Bus API) |
| `as5600.c/.h` | AS5600 magnetic encoder over I2C |
| `motor_driver.s` | ARM assembly H-bridge interface: `step_routine`, `enable_routine`, `step_at_deadline` (step-timing loop) |
| `Makefile` | Build |
| `../iOS/PARMCO/` | SwiftUI iPhone app (Xcode project) |

## Build and run on the Pi

```bash
sudo apt install libbluetooth-dev libglib2.0-dev     # once
make
sudo ./parmco_cp3            # options: -i invert encoder, -H hold on, -s secure BLE, -l logdir
```

It has to run as root, because the class library maps `/dev/mem`, the motor thread uses
real-time priority, and BLE registration needs it. On start-up, the program prints the step
mode, and it shows `[BLE] GATT application registered` and `[BLE] LE advertisement registered`.

**Encoder direction check, once.** Calibrate zero in the app, then turn the blade by hand to
+90° on the protractor. If the app shows about 270° instead of about 90°, restart with
`sudo ./parmco_cp3 -i`. The program works out which way the motor's steps move the
encoder by itself, the first time the motor moves.

## Protocol (same over RFCOMM and BLE; newline-terminated text)

| Phone → RP4 | Effect / reply |
|---|---|
| `HELLO` | `PARMCO:CONNECTED` + telemetry |
| `START` / `STOP` | Run at desired RPM / coils off, feedback off, unlock off |
| `DIR:CW` / `DIR:CCW` | Direction |
| `FASTER` / `SLOWER` | ±5% of desired RPM |
| `RPM:x` | 10–100 (10–400 with unlock); otherwise `ERR:INVALID_RPM` and speed unchanged |
| `MODE:MANUAL` / `MODE:MAINTAIN` | Mode (legacy `MAINTAIN:ON/OFF` also accepted) |
| `CALIBRATE_ZERO` | Store encoder zero (motor must be stopped) |
| `SET_ZERO` / `ANGLE:x` | Encoder-driven move at desired RPM; `ERR:NOT_CALIBRATED` until calibrated |
| `UNLOCK:ON/OFF` | Over-range unlock (auto-off on stop) |
| `LOG:START[:label]` / `LOG:STOP` | `LOG:STARTED:<file>` / `LOG:STOPPED:<file>` |
| `MARK[:text]` | Text into the log `event` column |
| `HOLD:ON/OFF` | Feedback hold after position moves (final-check feature) |
| `STATUS` | Telemetry now |

RP4 → phone, 5 times a second:
`TEL:ir=..;enc=..;ang=..;eok=..;cal=0|1;run=0|1;dir=CW|CCW;rpm=..;mode=MANUAL|MAINTAIN;unlock=0|1;act=IDLE|RUN|MOVE|HOLD;en=0|1;hold=0|1;log=0|1;logt=..;file=..`
The Android app still receives the older `RPM_ACTUAL:`, `ENCODER_RPM:` and `ANGLE_ACTUAL:` lines once a second.

## How the key functions work

* **Step timing.** `step_at_deadline` busy-waits on the ARM generic timer (CNTPCT) until an
  absolute deadline, then calls `step_routine`. C advances the deadline by one step period
  each time, so the time spent inside the step routine doesn't stretch the period. The motor
  thread runs at SCHED_FIFO priority, and starts and speed changes ramp at 200 RPM/s.
* **IR RPM.** The IR pin is polled about every 0.5 ms through `E4235_Read`. RPM comes from the
  time between the last falling edges (2 pulses per rev), not from counting pulses in a
  window. The reading drops to 0 if no pulse arrives within 2.5 periods (at least 2 s).
* **Encoder RPM.** The encoder is sampled at 100 Hz and its angle unwrapped. RPM is the angle
  change over a 0.5 s sliding window.
* **Position moves.** The error is target minus encoder angle, taking the shorter way round
  (this handles the 359°/0° wrap). The motor steps that many full steps, waits 150 ms,
  re-reads the encoder, and corrects if the error is still over 0.75 step (up to 4 tries).
* **Logging.** A separate 100 Hz thread writes the appendix columns, timestamped with
  CLOCK_MONOTONIC, into a 64 KB buffered file, so logging never blocks the motor thread.
  Files go to `./logs/`.

## Library attribution

* **ECSE 4235 class library (`libE4235`).** Provided by the instructor. All GPIO access uses
  `E4235_Init/Select/Read/Write`.
* **BlueZ / libbluetooth.** The Linux Bluetooth stack. The RFCOMM socket server and the
  `bluetoothd` daemon that hosts the BLE service come from here. Needed because the phone
  link is Bluetooth.
* **GLib/GIO (GDBus), LGPL-2.1+.** A D-Bus client and server library. BlueZ's BLE peripheral
  API is only available over D-Bus, and GDBus is the standard C binding for it.
* **Linux i2c-dev (`/dev/i2c-1`).** The kernel I2C driver used by `as5600.c` to read the
  encoder. It's the standard Linux interface to the Pi's hardware I2C controller.
* **POSIX threads, librt/libm.** Threads, monotonic clock, math.
* **iPhone app.** Apple SwiftUI and CoreBluetooth frameworks.
