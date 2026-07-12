# Magic Panel Serial Command Reference

## Overview
The Magic Panel supports both I2C and Serial communication protocols using JawaLite commands. This document provides a comprehensive reference for all available commands and display sequences.

## Communication Protocols

### I2C Communication
- **I2C Address**: `0x20` (decimal 20)
- **Baud Rate**: 9600
- **Command Format**: `&20,xCOMMAND,VALUE\r`
- **Example**: `&20,x54,2\r` - Triggers pattern 2 (Turn panel on for 2 seconds)

### Serial Communication
- **Baud Rate**: 9600
- **Command Format**: `%COMMANDVALUE\r`
- **Example**: `%T2\r` - Triggers pattern 2 (Turn panel on for 2 seconds)
- **Connection**: Pin 3 (RX) of Magic Panel to "To Slave" signal pin on MarcDuino Slave

### Command Termination
All commands must be terminated with a carriage return (`\r`)

## Supported JawaLite Commands

### Command Types
| Command | Description | Format | Example |
|---------|-------------|--------|---------|
| **A** | Turn panel ON | `%A` | `%A\r` |
| **D** | Turn panel OFF (Default) | `%D` | `%D\r` |
| **P** | Set panel mode | `%P[0-1]` | `%P1\r` |
| **T** | Trigger pattern | `%T[0-55]` | `%T20\r` |

### Panel Modes (P Command)
- **P0**: Patterns display for a given time then turn off (default behavior)
- **P1**: Patterns display continuously until T00 command is received

## Display Sequences (T Command)

### Basic Control (0-4)
| Mode | Description | Duration |
|------|-------------|----------|
| **T00** | Turn Panel OFF | Immediate |
| **T01** | Turn Panel ON Indefinitely | Until T00 |
| **T02** | Turn Panel ON | 2 seconds |
| **T03** | Turn Panel ON | 5 seconds |
| **T04** | Turn Panel ON | 10 seconds |

### Toggle & Alert Sequences (5-7)
| Mode | Description | Duration |
|------|-------------|----------|
| **T05** | Toggle Sequence: Top and Bottom halves alternate | Variable |
| **T06** | Alert Sequence: Rapid flash | 4 seconds |
| **T07** | Alert Sequence: Rapid flash | 10 seconds |

### Trace Sequences (8-15)
| Mode | Description | Type |
|------|-------------|------|
| **T08** | Trace UP: Bottom to top filling panel | Type 1 |
| **T09** | Trace UP: Bottom to top individually | Type 2 |
| **T10** | Trace DOWN: Top to bottom filling panel | Type 1 |
| **T11** | Trace DOWN: Top to bottom individually | Type 2 |
| **T12** | Trace RIGHT: Left to right filling panel | Type 1 |
| **T13** | Trace RIGHT: Left to right individually | Type 2 |
| **T14** | Trace LEFT: Right to left filling panel | Type 1 |
| **T15** | Trace LEFT: Right to left individually | Type 2 |

### Expand & Compress Sequences (16-19)
| Mode | Description | Type |
|------|-------------|------|
| **T16** | Expand from center filling panel | Type 1 |
| **T17** | Ring expands from center | Type 2 |
| **T18** | Compress from edge filling panel | Type 1 |
| **T19** | Ring compresses from edge | Type 2 |

### Pattern Sequences (20-30)
| Mode | Description | Duration |
|------|-------------|----------|
| **T20** | Cross Sequence: X pattern | 3 seconds |
| **T21** | Cylon Column: Left-right-left scan | Variable |
| **T22** | Cylon Row: Top-bottom-top scan | Variable |
| **T23** | Eye Scan: Rows then columns | Variable |
| **T24** | Fade Out/In: Random fade | Variable |
| **T25** | Fade Out: Random fade out only | Variable |
| **T26** | Flash Sequence: Rapid flash | 5 seconds |
| **T27** | Flash V: Left/Right halves alternate | Variable |
| **T28** | Flash Q: Quadrants flash rapidly | Variable |
| **T29** | Two Loop: Dual pixels loop on 2nd ring | Variable |
| **T30** | One Loop: Single pixel loops on 2nd ring | Variable |

### Test & Logo Sequences (31-34)
| Mode | Description | Notes |
|------|-------------|-------|
| **T31** | Test Type 1: Sequential fill then clear | Full panel |
| **T32** | Test Type 2: Individual pixel test | Each pixel |
| **T33** | AI Logo: Aurebesh characters | 3 seconds |
| **T34** | 2GWD Logo: 2-G-W-D sequence | 1s per char |

### Quadrant Sequences (35-38)
| Mode | Description | Order |
|------|-------------|-------|
| **T35** | Quadrant Type 1 | TL, TR, BR, BL |
| **T36** | Quadrant Type 2 | TR, TL, BL, BR |
| **T37** | Quadrant Type 3 | TR, BR, BL, TL |
| **T38** | Quadrant Type 4 | TL, BL, BR, TR |

### Animation Sequences (39-43)
| Mode | Description | Duration |
|------|-------------|----------|
| **T39** | Random Pixels: Individual flashes | 6 seconds |
| **T40** | Countdown: 9 to 0 | 10 seconds |
| **T41** | Countdown: 3 to 0 | 4 seconds |
| **T42** | Alert (MarcDuino style) | 4 seconds |
| **T43** | Alert (MarcDuino style) | 8 seconds |

### Shape Sequences (44-47)
| Mode | Description | Type |
|------|-------------|------|
| **T44** | Smiley Face | Static |
| **T45** | Sad Face | Static |
| **T46** | Heart | Static |
| **T47** | Flash Checkerboard | Animated |

### Advanced Sequences (48-55)
| Mode | Description | Type |
|------|-------------|------|
| **T48** | Compress In: TL/BR fill | Type 1 |
| **T49** | Compress In: TL/BR fill & clear | Type 2 |
| **T50** | Explode Out: Center fill | Type 1 |
| **T51** | Explode Out: Center fill & clear | Type 2 |
| **T52** | VU Meter: Columns bottom-up | Type 1 |
| **T53** | VU Meter: Rows left-right | Type 2 |
| **T54** | VU Meter: Columns top-down | Type 3 |
| **T55** | VU Meter: Rows right-left | Type 4 |

## Usage Examples

### Serial Examples
```
%A\r          # Turn panel ON
%D\r          # Turn panel OFF
%P1\r         # Set always-on mode
%T20\r        # Display cross pattern
%T00\r        # Turn OFF (useful in always-on mode)
```

### I2C Examples (MarcDuino)
```
&20,x41\r     # Turn panel ON (hex 41 = 'A')
&20,x44\r     # Turn panel OFF (hex 44 = 'D')
&20,x54,20\r  # Trigger pattern 20 (hex 54 = 'T')
```

## Special Features

### AlwaysOn Mode
- Default: `false` (patterns time out)
- Can be changed via P command or in code
- When `true`, patterns loop until T00 is received

### Pattern Timing
- Each pattern has predefined timing
- In normal mode, patterns auto-terminate
- In alwaysOn mode, patterns loop continuously

### Confirmation Feedback
When changing panel mode with P command:
- Panel flashes twice
- Displays "0" for normal mode (P0)
- Displays "1" for always-on mode (P1)

## Hardware Notes
- 8x8 LED matrix display
- ATmega328 microcontroller
- I2C pull-up resistors required for I2C mode
- Serial connection is point-to-point (no address needed)