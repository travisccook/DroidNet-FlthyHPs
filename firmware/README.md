# Prebuilt firmware — DroidNet FlthyHPs fork

Ready-to-flash image built from this repo at commit `310f372`, for flashing through the
DroidNet **Flash** tab. Details (sha256, size) are in [`manifest.json`](manifest.json).

> **NEVER FLASHED TO HARDWARE.** Compiles cleanly and verified against DroidNet's flasher
> logic only. Bench-test before field use.

Target: Arduino Mega ADK — **ATmega2560**.

| File | |
| ---- | --- |
| `DroidNet-FlthyHPs-atmega2560-310f372.hex` | Holoprojector firmware |

## Flashing via DroidNet

Upload the `.hex`. DroidNet reads the ATmega2560 signature (`0x1e9801` → Mega) and flashes
with `avrdude` (STK500v2 `wiring` programmer @ 115200). No manual board selection needed.

## Rebuild

```bash
~/.platformio/penv/bin/pio run -e FlthyHPs
```
