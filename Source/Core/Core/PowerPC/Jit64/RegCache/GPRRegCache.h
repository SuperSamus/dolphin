// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "Core/PowerPC/Jit64/RegCache/JitRegCache.h"
#include "Core/PowerPC/Jit64Common/Jit64PowerPCState.h"

class Jit64;

class GPRRegCacheImpl final
{
  friend class RegCache<GPRRegCacheImpl>;
  friend class GPRRegCache;

public:
  GPRRegCacheImpl(Jit64* jit) : m_jit{jit} {}

  static constexpr size_t NUM_HOST_REGS = 16;
  using BitSetHost = BitSet16;
  static constexpr size_t NUM_GUEST_REGS = 32;
  using BitSetGuest = BitSet32;

  bool IsImm(preg_t preg) const;
  u32 Imm32(preg_t preg) const;
  s32 SImm32(preg_t preg) const;

protected:
  static constexpr Gen::OpArg GetPPCStateLocation(preg_t preg) { return PPCSTATE_GPR(preg); }
  void StoreConstant(preg_t preg, const Gen::OpArg& new_loc);
  void StoreRegister(Gen::X64Reg xreg, const Gen::OpArg& new_loc);
  void LoadConstant(preg_t preg, Gen::X64Reg new_loc);
  void LoadFromPPCState(preg_t preg, Gen::X64Reg new_loc);
  void DiscardImm(preg_t preg);
  static constexpr std::array<Gen::X64Reg, 11> GPR_ALLOCATION_ORDER = {
#ifdef _WIN32
      Gen::RSI, Gen::RDI, Gen::R13, Gen::R14, Gen::R15, Gen::R8,
      Gen::R9,  Gen::R10, Gen::R11, Gen::R12, Gen::RCX
#else
      Gen::R12, Gen::R13, Gen::R14, Gen::R15, Gen::RSI, Gen::RDI,
      Gen::R8,  Gen::R9,  Gen::R10, Gen::R11, Gen::RCX
#endif
  };
  static constexpr std::span<const Gen::X64Reg> GetAllocationOrder()
  {
    return GPR_ALLOCATION_ORDER;
  }
  BitSetGuest GetRegUtilization() const;
  int JitNumInstructionsLeft() const;
  BitSetGuest CountRegsIn(preg_t preg, u32 lookahead) const;

  Jit64* m_jit;
};

class GPRRegCache : public RegCache<GPRRegCacheImpl>
{
public:
  void SetImmediate32(preg_t preg, u32 imm_value, bool dirty = true);
};

using GPRRCOpArg = RCOpArg<GPRRegCacheImpl>;
using GPRRCX64Reg = RCX64Reg<GPRRegCacheImpl>;
