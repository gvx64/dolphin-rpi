// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/Arm64Emitter.h"
#include "Common/CommonTypes.h"

#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/PowerPC/JitArm64/Jit.h"
#include "Core/PowerPC/JitArm64/JitArm64_RegCache.h"
#include "Core/PowerPC/PPCTables.h"
#include "Core/PowerPC/PowerPC.h"

using namespace Arm64Gen;

void JitArm64::sc(UGeckoInstruction inst)
{
  INSTRUCTION_START
  JITDISABLE(bJITBranchOff);

  gpr.Flush(FlushMode::FLUSH_ALL);
  fpr.Flush(FlushMode::FLUSH_ALL);

  ARM64Reg WA = gpr.GetReg();

  LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(Exceptions));
  ORR(WA, WA, 31, 0);  // Same as WA | EXCEPTION_SYSCALL
  STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(Exceptions));

  gpr.Unlock(WA);

  WriteExceptionExit(js.compilerPC + 4);
}

void JitArm64::rfi(UGeckoInstruction inst)
{
  INSTRUCTION_START
  JITDISABLE(bJITBranchOff);

  gpr.Flush(FlushMode::FLUSH_ALL);
  fpr.Flush(FlushMode::FLUSH_ALL);

  // See Interpreter rfi for details
  const u32 mask = 0x87C0FFFF;
  const u32 clearMSR13 = 0xFFFBFFFF;  // Mask used to clear the bit MSR[13]
  // MSR = ((MSR & ~mask) | (SRR1 & mask)) & clearMSR13;
  // R0 = MSR location
  // R1 = MSR contents
  // R2 = Mask
  // R3 = Mask
  ARM64Reg WA = gpr.GetReg();
  ARM64Reg WB = gpr.GetReg();
  ARM64Reg WC = gpr.GetReg();

  LDR(INDEX_UNSIGNED, WC, PPC_REG, PPCSTATE_OFF(msr));

  ANDI2R(WC, WC, (~mask) & clearMSR13, WA);  // rD = Masked MSR

//gvx64  LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_SRR1]));  // rB contains SRR1 here
  LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF_SPR(SPR_SRR1));  // rB contains SRR1 hereK //gvx64

  ANDI2R(WA, WA, mask & clearMSR13, WB);  // rB contains masked SRR1 here
  ORR(WA, WA, WC);                        // rB = Masked MSR OR masked SRR1

  STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(msr));  // STR rB in to rA

//gvx64  LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_SRR0]));
  LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF_SPR(SPR_SRR0)); //gvx64
  gpr.Unlock(WB, WC);

  // WA is unlocked in this function
  WriteExceptionExit(WA);
}

void JitArm64::bx(UGeckoInstruction inst)
{
  INSTRUCTION_START
  JITDISABLE(bJITBranchOff);

  u32 destination;
  if (inst.AA)
    destination = SignExt26(inst.LI << 2);
  else
    destination = js.compilerPC + SignExt26(inst.LI << 2);

  if (inst.LK)
  {
    ARM64Reg WA = gpr.GetReg();
    MOVI2R(WA, js.compilerPC + 4);
//gvx64    STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_LR]));
    STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF_SPR(SPR_LR)); //gvx64
    gpr.Unlock(WA);
  }

  if (!js.isLastInstruction)
  {
    if (inst.LK && !js.op->skipLRStack)
    {
      // We have to fake the stack as the RET instruction was not
      // found in the same block. This is a big overhead, but still
      // better than calling the dispatcher.
      FakeLKExit(js.compilerPC + 4);
    }
    return;
  }

  gpr.Flush(FlushMode::FLUSH_ALL);
  fpr.Flush(FlushMode::FLUSH_ALL);

  if (destination == js.compilerPC)
  {
    // make idle loops go faster
    ARM64Reg WA = gpr.GetReg();
    ARM64Reg XA = EncodeRegTo64(WA);

    MOVP2R(XA, &CoreTiming::Idle);
    BLR(XA);
    gpr.Unlock(WA);

    WriteExceptionExit(js.compilerPC);
    return;
  }

  WriteExit(destination, inst.LK, js.compilerPC + 4);
}

void JitArm64::bcx(UGeckoInstruction inst)
{
  INSTRUCTION_START
  JITDISABLE(bJITBranchOff);

  ARM64Reg WA = gpr.GetReg();
  FixupBranch pCTRDontBranch;
  if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)  // Decrement and test CTR
  {
//gvx64    LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_CTR]));
    LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF_SPR(SPR_CTR)); //gvx64
    SUBS(WA, WA, 1);
//gvx64    STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_CTR]));
    STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF_SPR(SPR_CTR)); //gvx64

    if (inst.BO & BO_BRANCH_IF_CTR_0)
      pCTRDontBranch = B(CC_NEQ);
    else
      pCTRDontBranch = B(CC_EQ);
  }

  FixupBranch pConditionDontBranch;

  if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)  // Test a CR bit
  {
    pConditionDontBranch =
        JumpIfCRFieldBit(inst.BI >> 2, 3 - (inst.BI & 3), !(inst.BO_2 & BO_BRANCH_IF_TRUE));
  }

  FixupBranch far = B();
  SwitchToFarCode();
  SetJumpTarget(far);

  if (inst.LK)
  {
    MOVI2R(WA, js.compilerPC + 4);
    STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_LR]));
  }
  gpr.Unlock(WA);

  u32 destination;
  if (inst.AA)
    destination = SignExt16(inst.BD << 2);
  else
    destination = js.compilerPC + SignExt16(inst.BD << 2);

  gpr.Flush(FlushMode::FLUSH_MAINTAIN_STATE);
  fpr.Flush(FlushMode::FLUSH_MAINTAIN_STATE);

  WriteExit(destination, inst.LK, js.compilerPC + 4);

  SwitchToNearCode();

  if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)
    SetJumpTarget(pConditionDontBranch);
  if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)
    SetJumpTarget(pCTRDontBranch);

  if (!analyzer.HasOption(PPCAnalyst::PPCAnalyzer::OPTION_CONDITIONAL_CONTINUE))
  {
    gpr.Flush(FlushMode::FLUSH_ALL);
    fpr.Flush(FlushMode::FLUSH_ALL);
    WriteExit(js.compilerPC + 4);
  }
}

void JitArm64::bcctrx(UGeckoInstruction inst)
{
  INSTRUCTION_START
  JITDISABLE(bJITBranchOff);

  // Rare condition seen in (just some versions of?) Nintendo's NES Emulator
  // BO_2 == 001zy -> b if false
  // BO_2 == 011zy -> b if true
  FALLBACK_IF(!(inst.BO_2 & BO_DONT_CHECK_CONDITION));

  // bcctrx doesn't decrement and/or test CTR
  _assert_msg_(DYNA_REC, inst.BO_2 & BO_DONT_DECREMENT_FLAG,
               "bcctrx with decrement and test CTR option is invalid!");

  // BO_2 == 1z1zz -> b always

  // NPC = CTR & 0xfffffffc;
  gpr.Flush(FlushMode::FLUSH_ALL);
  fpr.Flush(FlushMode::FLUSH_ALL);

  if (inst.LK_3)
  {
    ARM64Reg WB = gpr.GetReg();
    MOVI2R(WB, js.compilerPC + 4);
//gvx64    STR(INDEX_UNSIGNED, WB, PPC_REG, PPCSTATE_OFF(spr[SPR_LR]));
    STR(INDEX_UNSIGNED, WB, PPC_REG, PPCSTATE_OFF_SPR(SPR_LR)); //gvx64
    gpr.Unlock(WB);
  }

  ARM64Reg WA = gpr.GetReg();

//gvx64  LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_CTR]));
  LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_CTR])); //gvx64
  AND(WA, WA, 30, 29);  // Wipe the bottom 2 bits.

  WriteExit(WA, inst.LK_3, js.compilerPC + 4);
}

void JitArm64::bclrx(UGeckoInstruction inst)
{
  INSTRUCTION_START
  JITDISABLE(bJITBranchOff);

  bool conditional =
      (inst.BO & BO_DONT_DECREMENT_FLAG) == 0 || (inst.BO & BO_DONT_CHECK_CONDITION) == 0;

  ARM64Reg WA = gpr.GetReg();
  ARM64Reg WB = inst.LK ? gpr.GetReg() : INVALID_REG;

  FixupBranch pCTRDontBranch;
  if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)  // Decrement and test CTR
  {
//gvx64    LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_CTR]));
    LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF_SPR(SPR_CTR)); //gvx64
    SUBS(WA, WA, 1);
//gvx64    STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_CTR]));
    STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF_SPR(SPR_CTR)); //gvx64

    if (inst.BO & BO_BRANCH_IF_CTR_0)
      pCTRDontBranch = B(CC_NEQ);
    else
      pCTRDontBranch = B(CC_EQ);
  }

  FixupBranch pConditionDontBranch;
  if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)  // Test a CR bit
  {
    pConditionDontBranch =
        JumpIfCRFieldBit(inst.BI >> 2, 3 - (inst.BI & 3), !(inst.BO_2 & BO_BRANCH_IF_TRUE));
  }

  if (conditional)
  {
    FixupBranch far = B();
    SwitchToFarCode();
    SetJumpTarget(far);
  }

//gvx64  LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(spr[SPR_LR]));
  LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF_SPR(SPR_LR)); //gvx64
  AND(WA, WA, 30, 29);  // Wipe the bottom 2 bits.

  if (inst.LK)
  {
    MOVI2R(WB, js.compilerPC + 4);
//gvx64    STR(INDEX_UNSIGNED, WB, PPC_REG, PPCSTATE_OFF(spr[SPR_LR]));
    STR(INDEX_UNSIGNED, WB, PPC_REG, PPCSTATE_OFF_SPR(SPR_LR)); //gvx64

    gpr.Unlock(WB);
  }

  gpr.Flush(conditional ? FlushMode::FLUSH_MAINTAIN_STATE : FlushMode::FLUSH_ALL);
  fpr.Flush(conditional ? FlushMode::FLUSH_MAINTAIN_STATE : FlushMode::FLUSH_ALL);

  WriteBLRExit(WA);

  if (conditional)
    SwitchToNearCode();

  if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)
    SetJumpTarget(pConditionDontBranch);
  if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)
    SetJumpTarget(pCTRDontBranch);

  if (!analyzer.HasOption(PPCAnalyst::PPCAnalyzer::OPTION_CONDITIONAL_CONTINUE))
  {
    gpr.Flush(FlushMode::FLUSH_ALL);
    fpr.Flush(FlushMode::FLUSH_ALL);
    WriteExit(js.compilerPC + 4);
  }
}
