// Part of the DroidNet Driveable-Animation Contract — an additive layer bolted onto
// Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// src/droidnet/dn_strip.h — the three jewels' strip type: Adafruit_NeoPixel with ONE thing changed.
// show() is where a frame is latched onto the LEDs, by the stock firmware and by the contract layer
// alike, so it is the one place contract v1.3's S9 limit can be kept for every path at once: a
// jewel goes from dark to lit across its whole field at most three times a second, whatever lines
// arrive. show() asks the gate in src/contract/ContractFlthy.h (DnStrip::show() is defined there,
// next to the state it reads); a frame the gate holds back is not latched, and is shown later.
// The stock firmware's own looks are never held back: the gate acts only on a jewel the contract
// has, or let go of in the last 340 ms. Include after <Adafruit_NeoPixel.h>.
#pragma once
struct DnStrip : public Adafruit_NeoPixel {
  void show();
};
