> **About this file.** This is the README that shipped with the vendored copy of FlthyHPs in
> the owner's private working collection of droid firmware. It is *not* Ryan Sondgeroth's own
> README — he did not write it, and it is kept here only because it is part of the paper trail
> of how this code reached us, and because its command summary is a handy quick reference.
>
> For the authoritative documentation, and for the FlthyHPs manual, go to the source:
> <https://github.com/ryan-sondgeroth/FlthyHPs>.
>
> The fork-specific documentation is in [README.md](README.md).

---

# FlthyHPs - Holoprojector Control System

This project provides comprehensive control for R2-D2 style holoprojectors, combining both movement and LED display functions in a single Arduino-based system.

## Overview

**FlthyHPs v1.8** by Ryan Sondgeroth (FlthyMcNsty) is a sophisticated holoprojector control system that:

- Controls servo movement for 3 holoprojectors (Front, Rear, Top)
- Manages 7 LED NeoPixel boards per holoprojector for realistic hologram effects
- Utilizes I2C communication for integration with main droid control systems
- Supports both individual and coordinated holoprojector operations

## Hardware Requirements

- **Microcontroller**: Arduino Mega ADK
- **Servo Control**: Adafruit 16-Channel I2C PWM Servo Driver
- **LEDs**: Adafruit NeoPixel boards (7 per holoprojector, supports both RGB and RGBW)
- **Communication**: I2C bus (standardized address: 0x19/25)

## Features

### LED Functions
- **Leia Mode**: Classic blue hologram sequence
- **Color Projector**: Customizable color sequences
- **Dim Pulse**: Slow color pulsing effects
- **Rainbow Sequence**: Full spectrum cycling
- **Short Circuit**: Flickering malfunction effect
- **Auto LED Twitch**: Random activation sequences

### Servo Functions
- **Preset Positions**: 8 directional positions (Down, Center, Up, Left, etc.)
- **RC Control**: Remote control integration
- **Auto HP Twitch**: Automated random movement
- **Wag Functions**: Coordinated movement patterns

### Command Structure
Commands follow the pattern: `DT##[C][S][R][P]`
- **D**: HP Designator (F=Front, R=Rear, T=Top, A=All, X/Y/Z=Combinations)
- **T**: Type (0=LED, 1=Servo)
- **##**: Sequence number
- **C**: Color value (1-9, 0=Random)
- **S**: Speed setting (0-9)
- **R**: Random state
- **P**: Position value

## Build Instructions

### Using PlatformIO CLI

```bash
# Navigate to the FlthyHPs directory
cd FlthyHPs

# Install dependencies
~/.platformio/penv/bin/pio lib install

# Build the project
~/.platformio/penv/bin/pio run

# Upload to Arduino Mega ADK
~/.platformio/penv/bin/pio run -t upload

# Monitor serial output
~/.platformio/penv/bin/pio device monitor
```

### Dependencies

The project uses the following libraries:
- **Adafruit NeoPixel**: LED control
- **Adafruit PWM Servo Driver**: I2C servo control
- **Wire**: I2C communication
- **Servos**: Custom slow servo library (included in lib/)

## Usage Examples

```
F001     - Run Leia sequence on Front HP
A0053    - Set all HPs to green color
T1011    - Move Top HP to center position
S1       - Enter Leia Mode (Front HP down, Leia sequence)
A007|25  - Run rainbow sequence on all HPs for 25 seconds
```

## Configuration

Key settings can be modified in the source code:
- I2C Address (default: 0x19)
- LED brightness and color calibration
- Servo position presets
- Auto-twitch timing intervals

## Integration

This system is designed to integrate with the C2B5 droid control ecosystem via I2C communication, allowing coordinated control with other droid subsystems.

## Version History

- **v1.8**: Added custom "Off Colors" functionality
- **v1.7**: Bug fixes for sequence timing
- **v1.6**: Random LED sequences, PROGMEM optimization
- **v1.5**: Improved servo positioning accuracy
- **v1.4**: Enhanced sequence runtime handling
- **v1.3**: Sequence runtime values via command strings
- **v1.2**: Multiple improvements including RGBW support
- **v1.1**: Added HP twitch functionality

## Credits

- Original code by Ryan Sondgeroth (FlthyMcNsty)
- Special thanks to LostRebel, Knightshade, skelmir, and IOIIOOO (Jason Cross)
- Servo library by Big Happy Dude (BHD)

---
*Part of the C2B5 Droid Control System* 