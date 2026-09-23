// Part of the DroidNet FlthyHPs fork's native brightness command -- an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// test/host/test_native_ext.cpp -- accept/reject table for nativeExtParse()
// (src/droidnet/native_ext.h). No external deps; same tiny CHECK harness as
// test_contract_core.cpp. Every rejected line must also leave the outputs untouched.
#include "../../src/droidnet/native_ext.h"
#include <cstdio>

static int g_fail = 0, g_total = 0;
#define CHECK(cond) do { g_total++; if(!(cond)){ g_fail++; \
  printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);} } while(0)

// Jewel mask bits: bit 0 = HP0 (front), bit 1 = HP1 (rear), bit 2 = HP2 (top).
static const uint8_t FRONT = 0x01, REAR = 0x02, TOP = 0x04;

static void accepts(const char* line, uint8_t wantMask, uint8_t wantLevel, int srcLine) {
  uint8_t mask = 0xEE, level = 0xEE;
  const bool ok = nativeExtParse(line, false, mask, level);
  g_total++;
  if (!ok || mask != wantMask || level != wantLevel) {
    g_fail++;
    printf("FAIL %s:%d  accept \"%s\": ok=%d mask=0x%02x level=%u (want mask=0x%02x level=%u)\n",
           __FILE__, srcLine, line, ok ? 1 : 0, mask, level, wantMask, wantLevel);
  }
}

static void rejects(const char* line, bool piped, int srcLine) {
  uint8_t mask = 0xEE, level = 0xEE;
  const bool ok = nativeExtParse(line, piped, mask, level);
  g_total++;
  if (ok || mask != 0xEE || level != 0xEE) {
    g_fail++;
    printf("FAIL %s:%d  reject \"%s\" (piped=%d): ok=%d mask=0x%02x level=%u (outputs must stay 0xee)\n",
           __FILE__, srcLine, line ? line : "(null)", piped ? 1 : 0, ok ? 1 : 0, mask, level);
  }
}
#define ACCEPT(line, m, l) accepts((line), (m), (l), __LINE__)
#define REJECT(line)       rejects((line), false, __LINE__)
#define REJECT_PIPED(line) rejects((line), true, __LINE__)

// The three library examples (spec section 1) and every unit's fan-out, which must match the
// stock designators exactly (src/main.cpp: F,X,Y,A -> HP0; R,X,Z,A -> HP1; T,Y,Z,A -> HP2).
static void test_units() {
  ACCEPT("HAB128", FRONT | REAR | TOP, 128);
  ACCEPT("HFB255", FRONT, 255);
  ACCEPT("HTB1", TOP, 1);
  ACCEPT("HRB128", REAR, 128);
  ACCEPT("HXB128", FRONT | REAR, 128);
  ACCEPT("HYB128", FRONT | TOP, 128);
  ACCEPT("HZB128", REAR | TOP, 128);
  // uppercase only, and nothing outside F R T X Y Z A
  REJECT("hab128");
  REJECT("HaB128");
  REJECT("HfB128");
  REJECT("HrB128");
  REJECT("HtB128");
  REJECT("HxB128");
  REJECT("HyB128");
  REJECT("HzB128");
  REJECT("HSB128");     // S is a stock sequence letter, not a unit
  REJECT("HQB128");
  REJECT("H*B128");     // the contract's wildcard is not a native unit
  REJECT("HB128");      // missing unit
  REJECT("HHB128");
}

static void test_verb() {
  REJECT("HAb128");     // the verb letter is uppercase B only
  REJECT("HA128");      // no verb letter: the tail "A128" would be a live servo-class line
  REJECT("HAC128");
  REJECT("HABB128");
  REJECT("H");
  REJECT("HA");
  REJECT("HAB");        // empty level
  REJECT("");
  REJECT(nullptr);
}

static void test_levels() {
  ACCEPT("HAB1", FRONT | REAR | TOP, 1);
  ACCEPT("HAB9", FRONT | REAR | TOP, 9);
  ACCEPT("HAB10", FRONT | REAR | TOP, 10);
  ACCEPT("HAB99", FRONT | REAR | TOP, 99);
  ACCEPT("HAB100", FRONT | REAR | TOP, 100);
  ACCEPT("HAB254", FRONT | REAR | TOP, 254);
  ACCEPT("HAB255", FRONT | REAR | TOP, 255);
  ACCEPT("HAB007", FRONT | REAR | TOP, 7);     // leading zeros are accepted...
  ACCEPT("HAB01", FRONT | REAR | TOP, 1);
  ACCEPT("HAB099", FRONT | REAR | TOP, 99);
  REJECT("HAB0");                              // ...but 0 never is: setBrightness(0) wipes the buffer
  REJECT("HAB00");
  REJECT("HAB000");
  REJECT("HAB256");
  REJECT("HAB300");
  REJECT("HAB999");
  REJECT("HAB0255");                           // 4 digits, even though the value is 255
  REJECT("HAB1000");
  REJECT("HAB0001");
  REJECT("HAB65535");
  REJECT("HAB-1");                             // no sign
  REJECT("HAB+1");
  REJECT("HAB-0");
}

static void test_line_shape() {
  REJECT("HAB128 ");                           // the line must end right after the level
  REJECT(" HAB128");
  REJECT("HAB 128");
  REJECT("HA B128");
  REJECT("HAB128x");
  REJECT("HAB12a");
  REJECT("HAB1a2");
  REJECT("HAB128\r");
  REJECT("HAB128\n");
  REJECT("HAB128\t");
  REJECT("HAB128|5");                          // an unsplit pipe is trailing text too
  // merged lines (serialEvent keeps appending after the terminator)
  REJECT("HAB1283P100");
  REJECT("HAB128HAB140");
  REJECT("HAB128\rHAB140");
  REJECT("3P100HAB128");
  // anything not led by H
  REJECT("AB128");                             // a lost H leaves an inert designator tail
  REJECT("B128");
  REJECT("!HAB128");
  REJECT("!HFB:b=128");                        // the contract's B verb is a different command
  REJECT("A101");
}

// loop() strips "|<anything>" before any branch runs, so the parser only ever sees the part
// before the pipe. The caller passes piped = true when that split fired; each of these is the
// same bare "HAB128" the parser would see for HAB128|5, HAB128|-1, HAB128|65535 and HAB128|.
static void test_piped() {
  REJECT_PIPED("HAB128");
  REJECT_PIPED("HFB255");
  REJECT_PIPED("HTB1");
  REJECT_PIPED("HAB1");
  REJECT_PIPED("");
}

int main() {
  test_units();
  test_verb();
  test_levels();
  test_line_shape();
  test_piped();
  printf("%s  %d/%d native_ext checks passed\n", g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total);
  return g_fail ? 1 : 0;
}
