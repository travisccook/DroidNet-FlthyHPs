#!/usr/bin/env bash
# DroidNet FlthyHPs fork -- build the flashable image and its manifest.
# Copyright (c) 2026 Travis Cook.  SPDX-License-Identifier: MIT
#
#   bash firmware/build-image.sh
#
# Builds env FlthyHPs from clean and rewrites firmware/:
#   * DroidNet-FlthyHPs-atmega2560-<source_commit>.hex  (any older .hex here is removed)
#   * manifest.json  (source_commit, sha256, size, flashing notes, resolved dependencies)
#   * README.md
#
# source_commit is `git rev-parse --short=7 HEAD`, so the image must come from a committed tree:
# the script refuses to run while src/, include/, lib/ or platformio.ini has uncommitted changes,
# or when HEAD does not contain the native brightness branch. It does not stage or commit.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
ENV_NAME="FlthyHPs"
FW_DIR="$ROOT/firmware"

fail() { echo "build-image.sh: $*" >&2; exit 1; }

[ -x "$PIO" ] || fail "PlatformIO not found at $PIO (set PIO=/path/to/pio)."
command -v python3 >/dev/null || fail "python3 is required to write manifest.json."

DIRTY="$(git status --porcelain -- src include lib platformio.ini)"
if [ -n "$DIRTY" ]; then
  echo "$DIRTY" >&2
  fail "refusing to build: src/, include/, lib/ or platformio.ini has uncommitted changes (above). Commit first, so source_commit names what was built."
fi

SOURCE_COMMIT="$(git rev-parse --short=7 HEAD)"
git grep -q 'DroidNet native brightness BEGIN' HEAD -- src/main.cpp \
  || fail "refusing to build: HEAD ($SOURCE_COMMIT) has no native brightness branch in src/main.cpp."

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== clean build of env $ENV_NAME at $SOURCE_COMMIT"
"$PIO" run -e "$ENV_NAME" -t clean > "$WORK/clean.log" 2>&1 || { cat "$WORK/clean.log" >&2; fail "pio clean failed."; }
"$PIO" run -e "$ENV_NAME" > "$WORK/build.log" 2>&1 || { cat "$WORK/build.log" >&2; fail "pio run failed."; }
grep -E '^(RAM|Flash):' "$WORK/build.log" || true
"$PIO" pkg list -e "$ENV_NAME" > "$WORK/pkg.log" 2>&1 || { cat "$WORK/pkg.log" >&2; fail "pio pkg list failed."; }
"$PIO" --version > "$WORK/pio_version.txt"

HEX_SRC="$ROOT/.pio/build/$ENV_NAME/firmware.hex"
[ -s "$HEX_SRC" ] || fail "no $HEX_SRC after the build."

HEX_NAME="DroidNet-FlthyHPs-atmega2560-$SOURCE_COMMIT.hex"
find "$FW_DIR" -maxdepth 1 -name 'DroidNet-FlthyHPs-atmega2560-*.hex' ! -name "$HEX_NAME" -print -delete
cp "$HEX_SRC" "$FW_DIR/$HEX_NAME"

SOURCE_COMMIT="$SOURCE_COMMIT" HEX_NAME="$HEX_NAME" FW_DIR="$FW_DIR" WORK="$WORK" \
BUILT="$(date +%Y-%m-%d)" python3 - <<'PY'
import hashlib, json, os, re, sys
from pathlib import Path

work = Path(os.environ["WORK"])
fw_dir = Path(os.environ["FW_DIR"])
sha = os.environ["SOURCE_COMMIT"]
hex_name = os.environ["HEX_NAME"]
build_log = (work / "build.log").read_text()
pkg_log = (work / "pkg.log").read_text()
pio_version = (work / "pio_version.txt").read_text().strip().replace(", version ", " ")   # "PlatformIO Core 6.2.0"

def need(cond, what):
    if not cond:
        sys.exit(f"build-image.sh: could not read {what} from the PlatformIO output")

# "Dependency Graph" block of `pio run`: the library copies this build actually linked.
graph = []
in_graph = False
for line in build_log.splitlines():
    if line.startswith("Dependency Graph"):
        in_graph = True
        continue
    if in_graph:
        if line.startswith("|--"):
            graph.append(line[3:].strip())
        else:
            break
need(graph, "the Dependency Graph")

m = re.search(r"^Platform (\S+) @ (\S+) \(required: ([^)]*)\)", pkg_log, re.M)
need(m, "the platform")
platform = f"{m.group(1)} @ {m.group(2)}"
platform_required = m.group(3)

def pkg(name):
    m = re.search(rf"^\S+\s+{re.escape(name)} @ (\S+) \(required: ([^)]*)\)", pkg_log, re.M)
    need(m, name)
    return m.group(1), m.group(2)

fw_ver, _ = pkg("framework-arduino-avr")
tc_ver, _ = pkg("toolchain-atmelavr")
neo_ver, neo_req = pkg("Adafruit NeoPixel")
pwm_ver, pwm_req = pkg("Adafruit PWM Servo Driver Library")

hex_bytes = (fw_dir / hex_name).read_bytes()
sha256 = hashlib.sha256(hex_bytes).hexdigest()

status = ("NEVER FLASHED TO HARDWARE — built and host-tested only. "
          "Flash in place per the runbook (spec section 6); take an avrdude flash dump first.")
unplug = ("Unplug the WCB serial lead from the Mega (D0/RX0 and D1/TX0, or the whole connector) "
          "while flashing: UART0 is shared with the USB-serial chip.")

manifest = {
    "built": os.environ["BUILT"],
    "toolchain": f"{pio_version}, toolchain-atmelavr @ {tc_ver}",
    "status": status,
    "flash_via": ("DroidNet web UI -> Flash tab: select Board Type Arduino Mega, upload the .hex, flash. "
                  + unplug),
    "board": "FlthyHPs Holoprojectors (Ryan Sondgeroth) — Arduino Mega ADK, ATmega2560",
    "source_repo": "https://github.com/travisccook/DroidNet-FlthyHPs",
    "source_commit": sha,
    "native_commands": [
        {
            "wire": "H<unit>B<level>",
            "library_id": "flthy.led.brightness",
            "summary": ("unit F R T X Y Z A (stock designator fan-out), level 1-255; RAM only, "
                        "anything else ignored"),
        }
    ],
    "artifacts": [
        {
            "file": hex_name,
            "role": "PRIMARY",
            "chip": "atmega2560",
            "mcu": "ATmega2560",
            "env": "FlthyHPs",
            "droidnet_flash": {
                "file_type": "hex",
                "select_chip": "Arduino Mega (select it by hand; do not rely on auto-detect)",
                "flash_address": "n/a",
                "note": ("On a ttyACM Mega the Booster does not detect the ATmega2560 signature: "
                         "auto-detect probes avr109 and stk500v1, then guesses with avrdude -n in the "
                         "order uno, nano, mega, and it probes the first /dev/ttyACM* port, which may "
                         "be another device. Select Board Type Arduino Mega. " + unplug +
                         " Flashing uses avrdude with the STK500v2 'wiring' programmer @ 115200."),
            },
            "size_bytes": len(hex_bytes),
            "sha256": sha256,
        }
    ],
    "dependencies": {
        "platform": f"{platform} (required: {platform_required}; unpinned)",
        "framework": f"framework-arduino-avr @ {fw_ver}",
        "toolchain": f"toolchain-atmelavr @ {tc_ver}",
        "libraries": {
            "Adafruit NeoPixel": f"{neo_ver} (required: {neo_req})",
            "Adafruit PWM Servo Driver Library": f"{pwm_ver} (required: {pwm_req})",
        },
        "dependency_graph": graph,
        "note": ("Resolved by `pio pkg list` and linked per the `pio run` Dependency Graph for this "
                 "image. The requirements float, so a later rebuild may resolve newer versions."),
    },
}
(fw_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")

readme = f"""# Prebuilt firmware — DroidNet FlthyHPs fork

Ready-to-flash image built from this repo at commit `{sha}` by `firmware/build-image.sh`, for
flashing through the DroidNet **Flash** tab. Details (sha256, size, dependency versions) are in
[`manifest.json`](manifest.json).

> **NEVER FLASHED TO HARDWARE.** Built and host-tested only. Flash in place per the runbook
> (spec section 6); take an avrdude flash dump first.

Target: Arduino Mega ADK — **ATmega2560**.

| File | |
| ---- | --- |
| `{hex_name}` | Holoprojector firmware, with the native brightness command `H<unit>B<level>` |

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
"""
(fw_dir / "README.md").write_text(readme)
print(f"wrote firmware/{hex_name}  ({len(hex_bytes)} B, sha256 {sha256})")
print("wrote firmware/manifest.json and firmware/README.md")
PY
