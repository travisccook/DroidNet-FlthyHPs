# Prebuilt firmware — DroidNet FlthyHPs fork

Ready-to-flash image built from this repo at commit `75755de` by `firmware/build-image.sh`, for
flashing through the DroidNet **Flash** tab. Details (sha256, size, dependency versions) are in
[`manifest.json`](manifest.json).

> **NEVER FLASHED TO HARDWARE.** Built and host-tested only. Flash in place per the runbook
> (spec section 6); take an avrdude flash dump first.

Target: Arduino Mega ADK — **ATmega2560**.

| File | |
| ---- | --- |
| `DroidNet-FlthyHPs-atmega2560-75755de.hex` | Holoprojector firmware, with the native brightness command `H<unit>B<level>` |

## Before you flash

1. **Unplug the WCB serial lead from the Mega: both D0/RX0 and D1/TX0, or the whole connector.**
   UART0 is shared with the USB-serial chip, and opening USB resets the Mega. Keep it unplugged
   until the flash is done.
2. Take a flash dump over SSH on the Booster the Mega is plugged into. It is the exact rollback.
   Check `avrdude -?` on that Booster first:

   ```bash
   avrdude -C /etc/avrdude.conf -p atmega2560 -c wiring -P <port> -b 115200 -U flash:r:flthy-predump-YYYYMMDD.hex:i
   ```

## Flashing via DroidNet

1. In the Flash tab, set Board Type to **Arduino Mega**. Do not rely on auto-detect: on a ttyACM
   Mega the Booster does not detect the ATmega2560 signature. It probes avr109 and stk500v1, then
   guesses with `avrdude -n` in the order uno, nano, mega, and it probes the first `/dev/ttyACM*`
   port, which may be another device.
2. Upload the `.hex` and flash. The Booster runs `avrdude` with the STK500v2 `wiring` programmer
   @ 115200.
3. Reconnect the WCB serial lead.

## Rollback

With the WCB serial lead still unplugged, write the dump back over SSH (not the Flash tab, whose
avrdude timeout may be too short for a full 256 KB image):

```bash
avrdude -C /etc/avrdude.conf -p atmega2560 -c wiring -P <port> -b 115200 -D -U flash:w:flthy-predump-YYYYMMDD.hex:i
```

## Rebuild

```bash
bash firmware/build-image.sh
```

It refuses to run while `src/`, `include/`, `lib/` or `platformio.ini` has uncommitted changes,
builds env `FlthyHPs` from clean, and rewrites this folder: the `.hex` named after the commit,
`manifest.json` and this README. It does not commit.
