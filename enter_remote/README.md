# Enter Remote (BLE) — Flipper Zero app

A Bluetooth LE presentation remote for the Flipper Zero, modelled on the stock
**Keynote** profile of the built‑in *Bluetooth Remote* app — with one change:

> The **OK** button emulates pressing **Enter** on a keyboard instead of **Space**.

It builds as a normal external app (`.fap`) for **Momentum** firmware — no firmware
rebuild needed. The Flipper's BLE keyboard functions (`ble_profile_hid_*`) are private
in the firmware's API table, but `application.fam` statically links them into the app
via `fap_libs=["ble_profile"]` — the same way the stock Bluetooth Remote app does it.

## Controls

| Flipper button   | Sends              | Typical effect            |
| ---------------- | ------------------ | ------------------------- |
| **OK**           | **Enter / Return** | *(this is the change)*    |
| Up / Left        | Arrow Up / Left    | Previous slide            |
| Down / Right     | Arrow Down / Right | Next slide                |
| **Back** (tap)   | Escape             | Quit slideshow mode       |
| **Back** (hold)  | —                  | Exit the app              |

When a computer is connected, the Flipper's LED glows blue (same as the stock remote).

## Its own Bluetooth identity

The app advertises under its **own name and MAC address** (name prefix `Enter`, set via
`BleProfileHidParams` in `enter_remote.c`), separate from the Flipper's serial Bluetooth
and the stock Bluetooth Remote. This is deliberate: the Flipper's serial link and the stock
remote all share one identity (`Flipper <name>`), so if this app used that too, a host that
had already paired the stock remote would keep auto-reconnecting with mismatched pairing
keys — showing up as a rapid connect/disconnect loop and never letting you pair. Giving this
app a unique address makes the host see a brand-new device, so it pairs cleanly and coexists
with the stock remote.

---

## Build & install (Ubuntu / Linux)

The build tool is **ufbt** (micro Flipper Build Tool), pointed at the **Momentum SDK**
so the app matches the firmware on the device.

### 1. Install ufbt (one time)

Ubuntu's Python is externally managed, so install into a venv:

```bash
python3 -m venv ~/.venvs/ufbt
~/.venvs/ufbt/bin/pip install ufbt
mkdir -p ~/.local/bin && ln -sf ~/.venvs/ufbt/bin/ufbt ~/.local/bin/ufbt
```

### 2. Point ufbt at the Momentum SDK (one time, and after firmware updates)

```bash
ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json --channel=release
```

Use `--channel=release` for stable Momentum (e.g. `mntm-012`), or
`--channel=development` if your Flipper runs a Momentum dev build. The SDK API
version must match the installed firmware, so rerun this after updating the
firmware on the Flipper.

### 3. Build the app

From inside the `enter_remote` folder (the one containing `application.fam`):

```bash
ufbt
```

The result is `dist/enter_remote.fap`.

### 4. Put it on the Flipper

**Option A — plug in and launch (easiest):** connect the Flipper by USB, close
qFlipper/anything else holding the serial port, then:

```bash
ufbt launch
```

This uploads the `.fap` to `SD Card/apps/Bluetooth/` and starts it immediately.

**Option B — copy the file manually:** copy `dist/enter_remote.fap` onto the SD card
under `apps/Bluetooth/` using [qFlipper](https://flipperzero.one/update) (drag‑and‑drop
in the file browser). On the Flipper it appears under **Apps → Bluetooth → Enter Remote (BLE)**.

---

## Using it

1. On the Flipper: **Apps → Bluetooth → Enter Remote (BLE)**. It starts advertising.
2. On your computer/phone: open **Bluetooth settings**, find the device whose name starts
   with **`Enter`** (e.g. `Enter <name>`) and pair it. It connects as a keyboard.
3. Open your presentation, start the slideshow, and use the buttons above.
4. Hold **Back** to quit the app (this also restores the Flipper's normal Bluetooth).

> Because this app uses its own Bluetooth identity, it appears as a **separate** device from
> your stock `Flipper <name>` remote — you can keep both paired. If you previously tried an
> older build of this app that reused the `Flipper <name>` identity and saw a connect/disconnect
> loop, remove that stale `Flipper <name>` pairing on the host once, then pair the `Enter …`
> device.

---

## Customizing which key OK sends

Open [`enter_remote.c`](enter_remote.c) and look at the `InputKeyOk` case in
`enter_remote_handle_input()`:

```c
case InputKeyOk:
    key = KEY_ENTER;   // change this to KEY_SPACE, KEY_RIGHT, etc.
    break;
```

The available keycodes are defined near the top of the file (`KEY_ENTER`, `KEY_ESC`,
`KEY_LEFT/RIGHT/UP/DOWN`). To send Space instead, add
`#define KEY_SPACE 0x2C` and use it here. Any [USB HID usage ID](https://usb.org/sites/default/files/hut1_5.pdf)
works — they're just the low byte passed to `ble_profile_hid_kb_press()`.
Rebuild with `ufbt` after editing.

---

## Troubleshooting

- **"missing symbol" / won't launch after copying:** the `.fap` must match the firmware
  on the Flipper. Rerun step 2 (with the channel your Flipper actually runs), then
  rebuild with `ufbt`.
- **Computer won't pair:** delete any existing "Flipper …" Bluetooth pairing on the
  computer, make sure no other app is using the Flipper's Bluetooth, and try again.
- **Pairing shows the passkey, you accept on both sides, then Windows says "Try connecting
  your device again":** this is a stuck/stale bond, not a code problem. Clear the bond on
  *both* ends: on Windows remove the device (Settings → Bluetooth → the `Enter …` device →
  Remove device — also remove any leftover `Flipper …` entry), and on the Flipper go to
  **Settings → Bluetooth → Forget All Paired Devices**. Then reopen the app and pair fresh.
  Restarting the PC alone does **not** clear the bond, so it won't fix this on its own.
- **App crashes on open:** bump `stack_size` in `application.fam` from `2 * 1024` to
  `4 * 1024` and rebuild.
- **Nothing happens in the presentation:** confirm the LED is blue (connected) and that
  the presentation window has keyboard focus.
