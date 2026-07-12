// Host check of the firmware layer ContractFlthy.h against mock_flthy.h.
// Mostly a TYPE-CHECK — it proves the firmware C++ compiles with the verified board
// API signatures (catches typos / wrong arity / type errors before the bench). Every
// fork entry point is exercised so the compiler instantiates and type-checks it, and
// the main.cpp '!' dispatch body is replicated verbatim. Plus a focused behavioral
// guard that _entryFrom threads a scored NATIVE section's native code into the
// ScoreEntry (the drift the 2026-07-12 fork audit fixed — nativeCode stayed -1).
#include "mock_flthy.h"
#include "../../src/contract/ContractFlthy.h"
#include <cstdio>

int main() {
  // parse + fan-out
  ParsedContract p;
  contractParse("HFA:i=solid,c=0080FF,d=0,b=255,m=180", p);
  applyContract(p);
  applyContractToUnit(0, p);
  applyContractToUnit(1, p);
  applyContractToUnit(2, p);

  // exercise every verb
  { ParsedContract q; if (contractParse("H*A:i=rainbow", q))            applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=pulse,c=FFFFFF", q))     applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=scan,c=00FF00", q))      applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=sparkle,c=FF00FF", q))   applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=meter,c=00FF00", q))     applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=native:6", q))           applyContract(q); }
  { ParsedContract q; if (contractParse("HTA:i=solid,c=112233,at=44,am=1", q)) applyContract(q); }  // Phase-2 schedule
  { ParsedContract q; if (contractParse("HFP:c=FFFFFF,d=120,b=200", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("**C:bpm=128,ph=40,bpb=4,beat=44", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("H*B:v=180", q))                applyContract(q); }
  { ParsedContract q; if (contractParse("H*L:v=190,band=mid", q))       applyContract(q); }
  { ParsedContract q; if (contractParse("HFQ", q))                      applyContract(q); }
  { ParsedContract q; if (contractParse("HFM:v=show", q))               applyContract(q); }
  { ParsedContract q; if (contractParse("HFM:v=idle", q))               applyContract(q); }
  { ParsedContract q; if (contractParse("**X", q))                      applyContract(q); }

  // beat tick + per-hp render (cases 101..108 route here)
  contractBeatTick();
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) contractRenderHP(hp);

  // replicate the main.cpp '!' branch body verbatim to type-check it
  {
    char inputBuffer[80] = "!HFA:i=flash,c=FF0000,s=200";
    if (inputBuffer[0] == '!') contractHandle(&inputBuffer[1]);
  }

  // behavioral guard: _entryFrom must thread the parsed native code into the
  // ScoreEntry (regression guard for the nativeCode-stayed-(-1) drift the audit fixed;
  // without it a scored native section fell through to an unhandled LEDFunction=109).
  { ParsedContract q;
    if (!contractParse("HFA:i=native:3,at=44", q)) { printf("FAIL: Flthy parse\n"); return 1; }
    ScoreEntry e = _entryFrom(q.params);
    if (e.nativeCode != 3 || e.effect != CE_NATIVE) {
      printf("FAIL: Flthy _entryFrom did not thread nativeCode (got %d)\n", e.nativeCode);
      return 1;
    }
  }

  printf("ContractFlthy.h type-check + score-native guard OK\n");
  return 0;
}
