/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* This file contains mips-specific codegen factory support. */

#include "oat/runtime/oat_support_entrypoints.h"

namespace art {

bool genAddLong(CompilationUnit* cUnit, RegLocation rlDest,
                RegLocation rlSrc1, RegLocation rlSrc2)
{
  rlSrc1 = loadValueWide(cUnit, rlSrc1, kCoreReg);
  rlSrc2 = loadValueWide(cUnit, rlSrc2, kCoreReg);
  RegLocation rlResult = oatEvalLoc(cUnit, rlDest, kCoreReg, true);
  /*
   *  [v1 v0] =  [a1 a0] + [a3 a2];
   *  addu v0,a2,a0
   *  addu t1,a3,a1
   *  sltu v1,v0,a2
   *  addu v1,v1,t1
   */

  opRegRegReg(cUnit, kOpAdd, rlResult.lowReg, rlSrc2.lowReg, rlSrc1.lowReg);
  int tReg = oatAllocTemp(cUnit);
  opRegRegReg(cUnit, kOpAdd, tReg, rlSrc2.highReg, rlSrc1.highReg);
  newLIR3(cUnit, kMipsSltu, rlResult.highReg, rlResult.lowReg, rlSrc2.lowReg);
  opRegRegReg(cUnit, kOpAdd, rlResult.highReg, rlResult.highReg, tReg);
  oatFreeTemp(cUnit, tReg);
  storeValueWide(cUnit, rlDest, rlResult);
  return false;
}

bool genSubLong(CompilationUnit* cUnit, RegLocation rlDest,
        RegLocation rlSrc1, RegLocation rlSrc2)
{
  rlSrc1 = loadValueWide(cUnit, rlSrc1, kCoreReg);
  rlSrc2 = loadValueWide(cUnit, rlSrc2, kCoreReg);
  RegLocation rlResult = oatEvalLoc(cUnit, rlDest, kCoreReg, true);
  /*
   *  [v1 v0] =  [a1 a0] - [a3 a2];
   *  sltu  t1,a0,a2
   *  subu  v0,a0,a2
   *  subu  v1,a1,a3
   *  subu  v1,v1,t1
   */

  int tReg = oatAllocTemp(cUnit);
  newLIR3(cUnit, kMipsSltu, tReg, rlSrc1.lowReg, rlSrc2.lowReg);
  opRegRegReg(cUnit, kOpSub, rlResult.lowReg, rlSrc1.lowReg, rlSrc2.lowReg);
  opRegRegReg(cUnit, kOpSub, rlResult.highReg, rlSrc1.highReg, rlSrc2.highReg);
  opRegRegReg(cUnit, kOpSub, rlResult.highReg, rlResult.highReg, tReg);
  oatFreeTemp(cUnit, tReg);
  storeValueWide(cUnit, rlDest, rlResult);
  return false;
}

bool genNegLong(CompilationUnit* cUnit, RegLocation rlDest,
                RegLocation rlSrc)
{
  rlSrc = loadValueWide(cUnit, rlSrc, kCoreReg);
  RegLocation rlResult = oatEvalLoc(cUnit, rlDest, kCoreReg, true);
  /*
   *  [v1 v0] =  -[a1 a0]
   *  negu  v0,a0
   *  negu  v1,a1
   *  sltu  t1,r_zero
   *  subu  v1,v1,t1
   */

  opRegReg(cUnit, kOpNeg, rlResult.lowReg, rlSrc.lowReg);
  opRegReg(cUnit, kOpNeg, rlResult.highReg, rlSrc.highReg);
  int tReg = oatAllocTemp(cUnit);
  newLIR3(cUnit, kMipsSltu, tReg, r_ZERO, rlResult.lowReg);
  opRegRegReg(cUnit, kOpSub, rlResult.highReg, rlResult.highReg, tReg);
  oatFreeTemp(cUnit, tReg);
  storeValueWide(cUnit, rlDest, rlResult);
  return false;
}

/*
 * In the Arm code a it is typical to use the link register
 * to hold the target address.  However, for Mips we must
 * ensure that all branch instructions can be restarted if
 * there is a trap in the shadow.  Allocate a temp register.
 */
int loadHelper(CompilationUnit* cUnit, int offset)
{
  loadWordDisp(cUnit, rMIPS_SELF, offset, r_T9);
  return r_T9;
}

void spillCoreRegs(CompilationUnit* cUnit)
{
  if (cUnit->numCoreSpills == 0) {
    return;
  }
  uint32_t mask = cUnit->coreSpillMask;
  int offset = cUnit->numCoreSpills * 4;
  opRegImm(cUnit, kOpSub, rMIPS_SP, offset);
  for (int reg = 0; mask; mask >>= 1, reg++) {
    if (mask & 0x1) {
      offset -= 4;
      storeWordDisp(cUnit, rMIPS_SP, offset, reg);
    }
  }
}

void unSpillCoreRegs(CompilationUnit* cUnit)
{
  if (cUnit->numCoreSpills == 0) {
    return;
  }
  uint32_t mask = cUnit->coreSpillMask;
  int offset = cUnit->frameSize;
  for (int reg = 0; mask; mask >>= 1, reg++) {
    if (mask & 0x1) {
      offset -= 4;
      loadWordDisp(cUnit, rMIPS_SP, offset, reg);
    }
  }
  opRegImm(cUnit, kOpAdd, rMIPS_SP, cUnit->frameSize);
}

void genEntrySequence(CompilationUnit* cUnit, RegLocation* argLocs,
                      RegLocation rlMethod)
{
  int spillCount = cUnit->numCoreSpills + cUnit->numFPSpills;
  /*
   * On entry, rMIPS_ARG0, rMIPS_ARG1, rMIPS_ARG2 & rMIPS_ARG3 are live.  Let the register
   * allocation mechanism know so it doesn't try to use any of them when
   * expanding the frame or flushing.  This leaves the utility
   * code with a single temp: r12.  This should be enough.
   */
  oatLockTemp(cUnit, rMIPS_ARG0);
  oatLockTemp(cUnit, rMIPS_ARG1);
  oatLockTemp(cUnit, rMIPS_ARG2);
  oatLockTemp(cUnit, rMIPS_ARG3);

  /*
   * We can safely skip the stack overflow check if we're
   * a leaf *and* our frame size < fudge factor.
   */
  bool skipOverflowCheck = ((cUnit->attrs & METHOD_IS_LEAF) &&
      ((size_t)cUnit->frameSize < Thread::kStackOverflowReservedBytes));
  newLIR0(cUnit, kPseudoMethodEntry);
  int checkReg = oatAllocTemp(cUnit);
  int newSP = oatAllocTemp(cUnit);
  if (!skipOverflowCheck) {
    /* Load stack limit */
    loadWordDisp(cUnit, rMIPS_SELF, Thread::StackEndOffset().Int32Value(), checkReg);
  }
  /* Spill core callee saves */
  spillCoreRegs(cUnit);
  /* NOTE: promotion of FP regs currently unsupported, thus no FP spill */
  DCHECK_EQ(cUnit->numFPSpills, 0);
  if (!skipOverflowCheck) {
    opRegRegImm(cUnit, kOpSub, newSP, rMIPS_SP, cUnit->frameSize - (spillCount * 4));
    genRegRegCheck(cUnit, kCondCc, newSP, checkReg, kThrowStackOverflow);
    opRegCopy(cUnit, rMIPS_SP, newSP);     // Establish stack
  } else {
    opRegImm(cUnit, kOpSub, rMIPS_SP, cUnit->frameSize - (spillCount * 4));
  }

  flushIns(cUnit, argLocs, rlMethod);

  oatFreeTemp(cUnit, rMIPS_ARG0);
  oatFreeTemp(cUnit, rMIPS_ARG1);
  oatFreeTemp(cUnit, rMIPS_ARG2);
  oatFreeTemp(cUnit, rMIPS_ARG3);
}

void genExitSequence(CompilationUnit* cUnit)
{
  /*
   * In the exit path, rMIPS_RET0/rMIPS_RET1 are live - make sure they aren't
   * allocated by the register utilities as temps.
   */
  oatLockTemp(cUnit, rMIPS_RET0);
  oatLockTemp(cUnit, rMIPS_RET1);

  newLIR0(cUnit, kPseudoMethodExit);
  unSpillCoreRegs(cUnit);
  opReg(cUnit, kOpBx, r_RA);
}

/*
 * Nop any unconditional branches that go to the next instruction.
 * Note: new redundant branches may be inserted later, and we'll
 * use a check in final instruction assembly to nop those out.
 */
void removeRedundantBranches(CompilationUnit* cUnit)
{
  LIR* thisLIR;

  for (thisLIR = (LIR*) cUnit->firstLIRInsn;
     thisLIR != (LIR*) cUnit->lastLIRInsn;
     thisLIR = NEXT_LIR(thisLIR)) {

    /* Branch to the next instruction */
    if (thisLIR->opcode == kMipsB) {
      LIR* nextLIR = thisLIR;

      while (true) {
        nextLIR = NEXT_LIR(nextLIR);

        /*
         * Is the branch target the next instruction?
         */
        if (nextLIR == (LIR*) thisLIR->target) {
          thisLIR->flags.isNop = true;
          break;
        }

        /*
         * Found real useful stuff between the branch and the target.
         * Need to explicitly check the lastLIRInsn here because it
         * might be the last real instruction.
         */
        if (!isPseudoOpcode(nextLIR->opcode) ||
          (nextLIR = (LIR*) cUnit->lastLIRInsn))
          break;
      }
    }
  }
}


/* Common initialization routine for an architecture family */
bool oatArchInit()
{
  int i;

  for (i = 0; i < kMipsLast; i++) {
    if (EncodingMap[i].opcode != i) {
      LOG(FATAL) << "Encoding order for " << EncodingMap[i].name <<
         " is wrong: expecting " << i << ", seeing " <<
         (int)EncodingMap[i].opcode;
    }
  }

  return oatArchVariantInit();
}

bool genAndLong(CompilationUnit* cUnit, RegLocation rlDest,
                RegLocation rlSrc1, RegLocation rlSrc2)
{
  LOG(FATAL) << "Unexpected use of genAndLong for Mips";
  return false;
}

bool genOrLong(CompilationUnit* cUnit, RegLocation rlDest,
               RegLocation rlSrc1, RegLocation rlSrc2)
{
  LOG(FATAL) << "Unexpected use of genOrLong for Mips";
  return false;
}

bool genXorLong(CompilationUnit* cUnit, RegLocation rlDest,
               RegLocation rlSrc1, RegLocation rlSrc2)
{
  LOG(FATAL) << "Unexpected use of genXorLong for Mips";
  return false;
}



}  // namespace art
