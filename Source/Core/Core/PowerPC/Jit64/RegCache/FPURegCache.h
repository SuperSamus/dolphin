// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "Core/PowerPC/Jit64/RegCache/JitRegCache.h"
#include "Core/PowerPC/Jit64/RegCache/x64RegCache.h"
#include "Core/PowerPC/Jit64Common/Jit64PowerPCState.h"

class Jit64;

class FPURegCacheImpl final : public X64RegCacheBase
{
  friend class RegCache<FPURegCacheImpl>;

public:
  FPURegCacheImpl(Jit64* jit) : m_jit{jit} {}

  static constexpr size_t NUM_HOST_REGS = 16;
  using BitSetHost = BitSet16;
  static constexpr size_t NUM_GUEST_REGS = 32;
  using BitSetGuest = BitSet32;

  static bool IsImm(preg_t preg) { return false; }
  static u32 Imm32(preg_t preg);
  static s32 SImm32(preg_t preg);

protected:
  static constexpr Gen::OpArg GetPPCStateLocation(preg_t preg) { return PPCSTATE_PS0(preg); }
  static void StoreConstant(preg_t preg, const Gen::OpArg& new_loc);
  void StoreRegister(Gen::X64Reg xreg, const Gen::OpArg& new_loc);
  static void LoadConstant(preg_t preg, Gen::X64Reg new_loc);
  void LoadFromPPCState(preg_t preg, Gen::X64Reg new_loc);
  void DiscardImm(preg_t preg);
  static constexpr std::array<Gen::X64Reg, 14> FPU_ALLOCATION_ORDER = {
      Gen::XMM6,  Gen::XMM7,  Gen::XMM8,  Gen::XMM9, Gen::XMM10, Gen::XMM11, Gen::XMM12,
      Gen::XMM13, Gen::XMM14, Gen::XMM15, Gen::XMM2, Gen::XMM3,  Gen::XMM4,  Gen::XMM5};
  static constexpr std::span<const Gen::X64Reg> GetAllocationOrder()
  {
    return FPU_ALLOCATION_ORDER;
  }
  BitSetGuest GetRegUtilization() const;
  int JitNumInstructionsLeft() const;
  BitSetGuest CountRegsIn(preg_t preg, u32 lookahead) const;

  Jit64* m_jit;
};

using FPURegCache = RegCache<FPURegCacheImpl>;
using FPURCOpArg = RCOpArg<FPURegCacheImpl>;
using FPURCHostReg = RCHostReg<FPURegCacheImpl>;
