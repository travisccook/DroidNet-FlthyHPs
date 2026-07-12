# DroidNet_FlthyHPs

DroidNet contract fork of the **Flthy Holoprojectors** firmware (FlthyHPs by Ryan Sondgeroth),
customized to implement the **Driveable-Animation Contract** (**LED effects only — never servo motion**)
so Cantina Studio can drive the three holoprojector jewels (F/R/T) with arbitrary RGB, beat-locked
accents, a Studio-seeded beat-clock, and autonomous board-side section playback — **additively**, with
every native `DT##` LED command, servo command, `S#` sequence, and I²C intake still working.

- **Role:** sub-project E of the Cantina dome light-show program (moderate; the roomiest board).
- **Seeded from:** `travisccook/C2B5` @ `2fbd4023` (subdir `FlthyHPs`), 2026-07-12 — git-tracked files only.
- **MCU/toolchain:** ATmega2560 (Arduino Mega ADK), Arduino/AVR. 8 KB SRAM / 256 KB flash — ample.
- **Fork spec (authoritative):** `DroidNetSignalBooster/docs/superpowers/specs/2026-07-12-flthy-hps-fork-design.md`
  (+ the contract v1.1 amendment and transport spec).
- **LED-only guarantee:** the `!` contract path never writes `HP_command`; `M:v=show` pins
  `enableTwitchHP[*]=false` so no autonomous servo motion during a show.
- **First code tasks:** grow `INPUTBUFFERLEN` 10→64; refactor verb P to layer, not flush.
- **GitHub remote:** TBD (created later).
- **Status:** baseline import — no contract code added yet.
