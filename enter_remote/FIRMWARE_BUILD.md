# Firmware rebuild — no longer needed

This file used to describe rebuilding Momentum firmware from source to "unlock" the
private `ble_profile_hid_*` API so this app could link against it.

**That is unnecessary.** Those functions are private in the firmware API table on
purpose: apps are meant to *statically link* the BLE HID profile code into their own
`.fap` instead. The stock Bluetooth Remote app does exactly this. This app now does
too, via one line in `application.fam`:

```python
fap_libs=["ble_profile"],
```

With that in place, a plain `ufbt` build against the stock Momentum SDK produces a
`.fap` that loads and runs on unmodified Momentum firmware.

See [README.md](README.md) for the full build & install steps.
