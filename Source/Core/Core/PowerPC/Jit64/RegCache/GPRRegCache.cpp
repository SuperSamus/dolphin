// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/Jit64/RegCache/GPRRegCache.h"

#include "Core/PowerPC/Jit64/Jit.h"
#include "Core/PowerPC/Jit64/RegCache/JitRegCache.h"

bool GPRRegCacheImpl::IsImm(preg_t preg) const
{
  return m_jit->GetConstantPropagation().HasGPR(preg);
}

u32 GPRRegCacheImpl::Imm32(preg_t preg) const
{
  ASSERT(m_jit->GetConstantPropagation().HasGPR(preg));
  return m_jit->GetConstantPropagation().GetGPR(preg);
}

s32 GPRRegCacheImpl::SImm32(preg_t preg) const
{
  ASSERT(m_jit->GetConstantPropagation().HasGPR(preg));
  return m_jit->GetConstantPropagation().GetGPR(preg);
}

void GPRRegCacheImpl::StoreConstant(preg_t preg, const Gen::OpArg& new_loc)
{
  m_jit->MOV(32, new_loc, ::Gen::Imm32(m_jit->GetConstantPropagation().GetGPR(preg)));
}

void GPRRegCacheImpl::StoreRegister(Gen::X64Reg xreg, const Gen::OpArg& new_loc)
{
  m_jit->MOV(32, new_loc, ::Gen::R(xreg));
}

void GPRRegCacheImpl::LoadConstant(preg_t preg, Gen::X64Reg new_loc)
{
  m_jit->MOV(32, ::Gen::R(new_loc), ::Gen::Imm32(m_jit->GetConstantPropagation().GetGPR(preg)));
}

void GPRRegCacheImpl::LoadFromPPCState(preg_t preg, Gen::X64Reg new_loc)
{
  m_jit->MOV(32, ::Gen::R(new_loc), GetPPCStateLocation(preg));
}

void GPRRegCacheImpl::DiscardImm(preg_t preg)
{
  m_jit->GetConstantPropagation().ClearGPR(preg);
}

GPRRegCacheImpl::BitSetGuest GPRRegCacheImpl::GetRegUtilization() const
{
  return m_jit->js.op->gprWillBeRead | m_jit->js.op->gprWillBeWritten;
}

int GPRRegCacheImpl::JitNumInstructionsLeft() const
{
  return m_jit->js.instructionsLeft;
}

GPRRegCacheImpl::BitSetGuest GPRRegCacheImpl::CountRegsIn(preg_t preg, u32 lookahead) const
{
  BitSetGuest regs_used;

  for (u32 i = 1; i < lookahead; i++)
  {
    BitSetGuest regs_in = m_jit->js.op[i].regsIn;
    regs_used |= regs_in;
    if (regs_in[preg])
      return regs_used;
  }

  return regs_used;
}

void GPRRegCache::SetImmediate32(preg_t preg, u32 imm_value, bool dirty)
{
  // "dirty" can be false to avoid redundantly flushing an immediate when
  // processing speculative constants.
  if (dirty)
    DiscardRegister(preg);
  rc_impl.m_jit->GetConstantPropagation().SetGPR(preg, imm_value);
}
