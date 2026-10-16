// Host tests for src/wdp/wdp_fork.h, the FlthyHPs adapter for the shared WDP device announce core.
// Fork-local (test_wdp_announce_core.cpp is the shared, byte-identical core test). Runs the
// adapter against mock_flthy.h's Serial, which records every byte and reports a fixed TX room.
//
// run.sh compiles this twice: with -DWDP_FW='"abcdef0-dirty"' (the longest fw the build script
// makes: a 7-character commit plus "-dirty") and with no WDP_FW at all (the header's "unknown").
// WDP_FW_EXPECT names the fw each compile must announce.
//
// Part of the DroidNet board forks. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
#include "mock_flthy.h"
#include "../../src/wdp/wdp_fork.h"
#include <cstdio>
#include <cstring>
#include <string>

#ifndef WDP_FW_EXPECT
#error "compile with -DWDP_FW_EXPECT='\"<the fw this build must announce>\"'"
#endif

static int g_fail = 0, g_total = 0;
#define CHECK(cond) do { g_total++; if(!(cond)){ g_fail++; \
  printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);} } while(0)

static const std::string WANT =
    std::string("@WDP1 {\"type\":\"Flthy HP Controller\",\"fw\":\"") + WDP_FW_EXPECT +
    "\",\"caps\":[\"droidnet.contract\",\"hp.servo\",\"hp.led\"]}\n";

// Boots the adapter at `now` and runs loop() passes 1 ms apart until the first byte goes out.
// Returns the time of that pass (0 if none came within 3.5 s).
static uint32_t bootAndRunToFirstByte(uint32_t now, int txFree) {
  Serial.clear();
  Serial.txFree = txFree;
  _mock_millis = now;
  wdpForkSetup();
  for (uint32_t t = now; t < now + 3500; t++) {
    _mock_millis = t;
    wdpForkTick();
    if (Serial.last[0]) return t;
  }
  return 0;
}

static void test_the_identity_this_build_announces() {
  CHECK(strcmp(WDP_FW, WDP_FW_EXPECT) == 0);
  char line[WDP_LINE_MAX];
  uint8_t n = wdpBuildLine(line, sizeof(line), WDP_FORK_TYPE, WDP_FW, WDP_FORK_CAPS);
  CHECK(n > 0);
  CHECK(std::string(line, n) == WANT);
}

static void test_the_longest_fw_fits_wdp_line_max() {
  // Whatever this compile's WDP_FW is: "abcdef0-dirty" (13 characters) is the longest
  // scripts/wdp_fw.py makes, and the real type and caps must still fit the core's buffer.
  char line[WDP_LINE_MAX];
  uint8_t n = wdpBuildLine(line, sizeof(line), WDP_FORK_TYPE, "abcdef0-dirty", WDP_FORK_CAPS);
  CHECK(n == 107);
  CHECK(n + 1 <= WDP_LINE_MAX);
  CHECK(std::string(line, n) ==
        "@WDP1 {\"type\":\"Flthy HP Controller\",\"fw\":\"abcdef0-dirty\","
        "\"caps\":[\"droidnet.contract\",\"hp.servo\",\"hp.led\"]}\n");
}

static void test_the_line_goes_out_whole_in_pieces_no_bigger_than_the_tx_room() {
  // An AVR HardwareSerial has 63 bytes free when empty; the line is longer.
  uint32_t first = bootAndRunToFirstByte(1000, 63);
  CHECK(first >= 1000 + 500 && first <= 1000 + 3000);
  CHECK(Serial.maxWrite == 63);
  for (uint32_t t = first + 1; t < first + 10; t++) { _mock_millis = t; wdpForkTick(); }
  CHECK(std::string(Serial.last) == WANT);
  CHECK(Serial.maxWrite <= 63);
}

static void test_no_room_means_no_write() {
  CHECK(bootAndRunToFirstByte(0, 0) == 0);
  CHECK(Serial.last[0] == 0);
}

static void test_finish_sends_the_rest_of_a_line_in_flight_once() {
  uint32_t first = bootAndRunToFirstByte(0, 20);
  CHECK(first != 0);
  CHECK(strlen(Serial.last) == 20);
  wdpFinish();
  CHECK(std::string(Serial.last) == WANT);
  wdpFinish();                                         // nothing in flight now: no repeat
  _mock_millis = first + 1;
  wdpForkTick();                                       // and the next line is 25-30 s away
  CHECK(std::string(Serial.last) == WANT);
}

static void test_finish_with_nothing_in_flight_writes_nothing() {
  Serial.clear();
  _mock_millis = 0;
  wdpForkSetup();
  wdpFinish();
  CHECK(Serial.last[0] == 0);
}

static void test_the_seed_follows_a3_and_micros() {
  // Boards running this same image must not announce in lockstep: the first-announce time must
  // move with what A3 reads, and with micros().
  bool seen[3001] = {};
  int distinct = 0;
  _mock_micros = 4242;
  for (int a = 0; a < 100; a++) {
    _mock_analog = a;
    uint32_t t = bootAndRunToFirstByte(0, 63);
    if (t <= 3000 && !seen[t]) { seen[t] = true; distinct++; }
  }
  CHECK(distinct >= 90);

  bool seen2[3001] = {};
  distinct = 0;
  _mock_analog = 512;
  for (unsigned long us = 0; us < 100; us++) {
    _mock_micros = us * 4;                             // micros() counts in 4 us steps at 16 MHz
    uint32_t t = bootAndRunToFirstByte(0, 63);
    if (t <= 3000 && !seen2[t]) { seen2[t] = true; distinct++; }
  }
  CHECK(distinct >= 90);
  _mock_analog = 0;
  _mock_micros = 0;
}

int main() {
  test_the_identity_this_build_announces();
  test_the_longest_fw_fits_wdp_line_max();
  test_the_line_goes_out_whole_in_pieces_no_bigger_than_the_tx_room();
  test_no_room_means_no_write();
  test_finish_sends_the_rest_of_a_line_in_flight_once();
  test_finish_with_nothing_in_flight_writes_nothing();
  test_the_seed_follows_a3_and_micros();
  printf("%s  %d/%d checks passed (fw \"%s\")\n", g_fail ? "FAILURES" : "OK",
         g_total - g_fail, g_total, WDP_FW);
  return g_fail ? 1 : 0;
}
