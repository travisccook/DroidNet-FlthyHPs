// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// One scenario per process: sim_main --list | sim_main <scenario>. Exit 0 = pass.
#include "sim.h"
using namespace sim;
#include "scenarios_native.inc"
#include "scenarios_contract.inc"
#include "scenarios_findings.inc"
#include "scenarios_v13.inc"
#include "scenarios_review.inc"
#include "scenarios_v14.inc"

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: %s --list | <scenario>\n", argv[0]); return 2; }
  if (std::string(argv[1]) == "--list") { for (auto& kv : registry()) printf("%s\n", kv.first.c_str()); return 0; }
  auto it = registry().find(argv[1]);
  if (it == registry().end()) { printf("no such scenario: %s\n", argv[1]); return 2; }
  srand(7);
  setup();
  it->second();
  if (ledOnlyChecked && ledOnlyViolations) { printf("  FAIL LED-only invariant: %ld violating pass(es)\n", ledOnlyViolations); failures++; }
  if (holdChecked && holdViolations) { printf("  FAIL hold invariant: %ld pass(es) moved a servo the contract had just let go of\n", holdViolations); failures++; }
  return failures ? 1 : 0;
}
