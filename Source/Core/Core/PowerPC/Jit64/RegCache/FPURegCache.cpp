// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/Jit64/RegCache/FPURegCache.h"

#include "Core/PowerPC/Jit64/Jit.h"
#include "Core/PowerPC/Jit64/RegCache/JitRegCache.h"

u32 FPURegCacheImpl::Imm32(preg_t preg)
{
  ASSERT_MSG(DYNA_REC, false, "FPURegCache doesn't support immediates");
  return 0;
}

s32 FPURegCacheImpl::SImm32(preg_t preg)
{
  ASSERT_MSG(DYNA_REC, false, "FPURegCache doesn't support immediates");
  return 0;
}

void FPURegCacheImpl::StoreConstant(preg_t preg, const Gen::OpArg& new_loc)
{
  ASSERT_MSG(DYNA_REC, false, "FPURegCache doesn't support immediates");
}

void FPURegCacheImpl::StoreRegister(Gen::X64Reg xreg, const Gen::OpArg& new_loc)
{
  m_jit->MOVAPD(new_loc, xreg);
}

void FPURegCacheImpl::LoadConstant(preg_t preg, Gen::X64Reg new_loc)
{
  ASSERT_MSG(DYNA_REC, false, "FPURegCache doesn't support immediates");
}

void FPURegCacheImpl::LoadFromPPCState(preg_t preg, Gen::X64Reg new_loc)
{
  m_jit->MOVAPD(new_loc, GetPPCStateLocation(preg));
}

void FPURegCacheImpl::DiscardImm(preg_t preg)
{
  // FPURegCache doesn't support immediates, so no need to do anything
}

FPURegCacheImpl::BitSetGuest FPURegCacheImpl::GetRegUtilization() const
{
  return m_jit->js.op->fprInXmm;
}

int FPURegCacheImpl::JitNumInstructionsLeft() const
{
  return m_jit->js.instructionsLeft;
}

FPURegCacheImpl::BitSetGuest FPURegCacheImpl::CountRegsIn(preg_t preg, u32 lookahead) const
{
  BitSetGuest regs_used;

  for (u32 i = 1; i < lookahead; i++)
  {
    BitSetGuest regs_in = m_jit->js.op[i].fregsIn;
    regs_used |= regs_in;
    if (regs_in[preg])
      return regs_used;
  }

  return regs_used;
}

using FPURegCache = RegCache<FPURegCacheImpl>;
