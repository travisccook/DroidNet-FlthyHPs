// Part of the DroidNet FlthyHPs fork's native brightness command -- an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// test/host/fuzz_native_ext.cpp -- DIFFERENTIAL + ROBUSTNESS fuzzer for nativeExtParse()
// (src/droidnet/native_ext.h). FORK-LOCAL: this file belongs to the FlthyHPs fork only.
// Do not confuse it with test/host/fuzz_parsers.cpp, which is shared byte-identically across
// the RSeries, PSI and Flthy forks and must not be edited here.
//
// Deterministic (fixed seed), self-contained, needs nothing but clang. run.sh builds it with:
//
//   clang++ -std=c++17 -Wall -Wextra -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
//           fuzz_native_ext.cpp -o /tmp/flthy_native_ext_fuzz && /tmp/flthy_native_ext_fuzz
//
// Every line is checked with BOTH piped = false and piped = true against model(), an
// independently written statement of the grammar (length window + std::string::find + a
// lookup table + std::stoi, where the parser uses an if-chain and a hand-rolled digit loop).
// Every line is copied into a HEAP buffer of exactly strlen()+1 bytes first, so a single byte
// read past the NUL is an AddressSanitizer abort, not a silent success. A rejected line must
// also leave mask and level untouched.
//
//   A. unit and verb positions over every non-NUL byte
//   B. every digit string of length 0-5 behind every valid unit
//   C. every string of length 0-5 over a 13-character grammar alphabet
//   D. every non-NUL byte at every position of HAB255, and appended as a 7th byte
//   E. a fixed-seed random sweep, half grammar-shaped, half whole-byte-range
//
// MUTATION DRILL (how to convince yourself this file has teeth), in a throwaway copy:
//   cp -R <repo> /tmp/mut && cd /tmp/mut
//   sed -i '' 's/if (piped || line == nullptr/if (line == nullptr/' src/droidnet/native_ext.h
//   clang++ -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=all \
//           test/host/fuzz_native_ext.cpp -o /tmp/mut_fuzz && /tmp/mut_fuzz     # => FAILURES
#include "../../src/droidnet/native_ext.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static long g_total = 0;
static long g_fail = 0;
static int  g_reported = 0;

static std::string esc(const std::string& s) {
  std::string out;
  for (unsigned char ch : s) {
    if (ch >= 0x20 && ch < 0x7f && ch != '\\' && ch != '"') {
      out += static_cast<char>(ch);
    } else {
      char buf[8];
      snprintf(buf, sizeof buf, "\\x%02x", ch);
      out += buf;
    }
  }
  return out;
}

// The grammar, written a second time without looking at the parser's structure.
static bool model(const std::string& s, bool piped, uint8_t& mask, uint8_t& level) {
  if (piped) return false;
  if (s.size() < 4 || s.size() > 6) return false;          // "H" + unit + "B" + 1..3 digits
  if (s[0] != 'H' || s[2] != 'B') return false;
  static const std::string kUnits = "FRTXYZA";
  static const uint8_t kMasks[] = {0x01, 0x02, 0x04, 0x03, 0x05, 0x06, 0x07};
  const std::string::size_type u = kUnits.find(s[1]);
  if (u == std::string::npos) return false;
  const std::string digits = s.substr(3);
  for (char ch : digits) {
    if (ch < '0' || ch > '9') return false;
  }
  const int v = std::stoi(digits);
  if (v < 1 || v > 255) return false;
  mask = kMasks[u];
  level = static_cast<uint8_t>(v);
  return true;
}

// One differential check, both piped values. `s` must not contain a NUL.
static void check(const std::string& s) {
  for (int p = 0; p < 2; ++p) {
    const bool piped = (p == 1);
    char* buf = static_cast<char*>(malloc(s.size() + 1));
    memcpy(buf, s.c_str(), s.size() + 1);
    uint8_t gotMask = 0xEE, gotLevel = 0xEE;
    const bool got = nativeExtParse(buf, piped, gotMask, gotLevel);
    free(buf);
    uint8_t wantMask = 0xEE, wantLevel = 0xEE;
    const bool want = model(s, piped, wantMask, wantLevel);
    g_total++;
    if (got != want || gotMask != wantMask || gotLevel != wantLevel) {
      g_fail++;
      if (g_reported++ < 20) {
        printf("FAIL line=\"%s\" piped=%d: parser ok=%d mask=0x%02x level=%u, model ok=%d mask=0x%02x level=%u\n",
               esc(s).c_str(), piped ? 1 : 0, got ? 1 : 0, gotMask, gotLevel,
               want ? 1 : 0, wantMask, wantLevel);
      }
    }
  }
}

static const uint64_t kSeed = 0xF17B1E55B21A7001ull;   // fixed seed: every run is identical
static uint64_t g_rng = kSeed;
static uint32_t rnd() {                          // xorshift64*, deterministic everywhere
  g_rng ^= g_rng >> 12; g_rng ^= g_rng << 25; g_rng ^= g_rng >> 27;
  return static_cast<uint32_t>((g_rng * 2685821657736338717ull) >> 32);
}

// A. unit and verb positions over every non-NUL byte.
static void section_a() {
  for (int b = 1; b < 256; ++b) {
    std::string unit = "H?B128";
    unit[1] = static_cast<char>(b);
    check(unit);
    std::string verb = "HA?128";
    verb[2] = static_cast<char>(b);
    check(verb);
    std::string lead = "?AB128";
    lead[0] = static_cast<char>(b);
    check(lead);
  }
}

// B. every digit string of length 0-5 (leading zeros included) behind every valid unit.
static void digit_strings(const std::string& prefix, int remaining) {
  check(prefix);
  if (remaining == 0) return;
  for (char d = '0'; d <= '9'; ++d) digit_strings(prefix + d, remaining - 1);
}
static void section_b() {
  for (char u : std::string("FRTXYZA")) {
    digit_strings(std::string("H") + u + "B", 5);
  }
}

// C. every string of length 0-5 over a grammar alphabet: the prefix letters, their lowercase
// twins, a non-unit letter, the digit boundaries, and the separators that show up in merges.
static void all_strings(const std::string& alphabet, const std::string& prefix, int remaining) {
  check(prefix);
  if (remaining == 0) return;
  for (char ch : alphabet) all_strings(alphabet, prefix + ch, remaining - 1);
}
static void section_c() {
  all_strings("HBAFhbQ0125| ", "", 5);
}

// D. every non-NUL byte at every position of HAB255, and appended as a 7th byte.
static void section_d() {
  const std::string base = "HAB255";
  for (size_t pos = 0; pos < base.size(); ++pos) {
    for (int b = 1; b < 256; ++b) {
      std::string s = base;
      s[pos] = static_cast<char>(b);
      check(s);
    }
  }
  for (int b = 1; b < 256; ++b) check(base + static_cast<char>(b));
}

// E. fixed-seed random sweep.
static void section_e(long iterations) {
  static const char kGrammar[] = "HBFRTXYZAhb0123456789|- \r\n";
  for (long i = 0; i < iterations; ++i) {
    const bool shaped = (rnd() & 1U) != 0U;
    std::string s;
    if (shaped) {
      s = "H";
      s += "FRTXYZAfQ"[rnd() % 9];
      s += (rnd() % 8 == 0) ? 'b' : 'B';
      const unsigned n = rnd() % 6;                 // 0-5 tail characters
      for (unsigned k = 0; k < n; ++k) {
        s += (rnd() % 10 < 8) ? static_cast<char>('0' + rnd() % 10)
                              : kGrammar[rnd() % (sizeof kGrammar - 1)];
      }
    } else {
      const unsigned n = rnd() % 11;                // 0-10 bytes, whole non-NUL range
      for (unsigned k = 0; k < n; ++k) s += static_cast<char>(1 + rnd() % 255);
    }
    check(s);
  }
}

int main() {
  const long iterations = 300000;
  section_a();
  section_b();
  section_c();
  section_d();
  section_e(iterations);
  printf("%s  %ld/%ld native_ext fuzz checks passed  (seed 0x%016llx, %ld random lines)\n",
         g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total,
         static_cast<unsigned long long>(kSeed), iterations);
  return g_fail ? 1 : 0;
}
