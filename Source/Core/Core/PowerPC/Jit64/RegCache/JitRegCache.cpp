// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/Jit64/RegCache/JitRegCache.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "Common/Assert.h"
#include "Common/BitSet.h"
#include "Common/CommonTypes.h"
#include "Core/PowerPC/Jit64/Jit.h"

using namespace Gen;
using namespace PowerPC;

bool RegCache::SanityCheck() const
{
  if (m_state.m_guests_in_host_register &
      (m_state.m_guests_is_locked | m_state.m_guests_revertable))
    return false;

  for (const preg_t i : m_state.m_guests_in_host_register)
  {
    Gen::X64Reg xr = m_state.m_guests_host_register[i];
    if (m_state.m_hosts_is_locked[xr])
      return false;
    if (m_state.m_hosts_guest_register[xr] != i)
      return false;
  }
  return true;
}

void RegCache::Discard(BitSetGuest pregs)
{
  ASSERT_MSG(DYNA_REC, !m_state.m_hosts_is_locked, "Someone forgot to unlock a X64 reg");
  const BitSetGuest locked_pregs = pregs & m_state.m_guests_is_locked;
  ASSERT_MSG(DYNA_REC, !locked_pregs, "Someone forgot to unlock the following PPC regs {:b}.",
             locked_pregs.m_val);
  const BitSetGuest revertable_pregs = pregs & m_state.m_guests_revertable;
  ASSERT_MSG(DYNA_REC, !revertable_pregs,
             "Register transaction is in progress for the following PPC regs {:b}.",
             revertable_pregs.m_val);

  for (const preg_t i : (pregs & m_state.m_guests_in_host_register))
  {
    const X64Reg xr = m_state.m_guests_host_register[i];
    m_state.m_hosts_in_guest_register[xr] = false;
  }

  m_state.m_guests_in_ppc_state &= ~pregs;
  m_state.m_guests_in_host_register &= ~pregs;
}

void RegCache::Flush(BitSetGuest pregs, FlushMode mode,
                     IgnoreDiscardedRegisters ignore_discarded_registers)
{
  const BitSetGuest revertable_pregs = pregs & m_state.m_guests_revertable;
  ASSERT_MSG(DYNA_REC, !revertable_pregs,
             "Register transaction is in progress for the following PPC regs {:b}.",
             revertable_pregs.m_val);

  for (const preg_t i : (pregs & ~m_state.m_guests_in_ppc_state))
  {
    StoreRegister(i, GetPPCStateLocation(i), ignore_discarded_registers);
  }

  if (mode == FlushMode::Full)
  {
    const BitSetGuest locked_pregs = pregs & m_state.m_guests_is_locked;
    ASSERT_MSG(DYNA_REC, !locked_pregs, "Someone forgot to unlock the following PPC regs {:b}.",
               locked_pregs.m_val);

    for (const preg_t i : (pregs & m_state.m_guests_in_host_register))
    {
      const X64Reg xr = m_state.m_guests_host_register[i];
      ASSERT_MSG(DYNA_REC, !m_state.m_hosts_is_locked[xr],
                 "Someone forgot to unlock X64 reg {} (PPC reg {}).", std::to_underlying(xr), i);
      m_state.m_hosts_in_guest_register[xr] = false;
    }

    m_state.m_guests_in_host_register &= ~pregs;
  }

  m_state.m_guests_in_ppc_state |= pregs;
}

void RegCache::Reset(BitSetGuest pregs)
{
  const BitSetGuest in_host_register_pregs = pregs & m_state.m_guests_in_host_register;
  ASSERT_MSG(DYNA_REC, !in_host_register_pregs,
             "Attempted to reset the loaded registers {:b} (did you mean to flush them?)",
             in_host_register_pregs.m_val);

  m_state.m_guests_in_ppc_state |= pregs;
}

BitSetGuest RegCache::RegistersRevertable() const
{
  ASSERT(IsAllUnlocked());
  return m_state.m_guests_revertable;
}

void RegCache::Commit()
{
  ASSERT(IsAllUnlocked());
  m_state.m_guests_revertable = {};
}

bool RegCache::IsAllUnlocked() const
{
  return !m_state.m_hosts_is_locked && !m_state.m_guests_is_locked && !IsAnyConstraintActive();
}

void RegCache::PreloadRegisters(BitSetGuest to_preload)
{
  for (const preg_t preg : to_preload & ~m_state.m_guests_in_host_register)
  {
    if (GetFreeRegisters().Count() < 2)
      return;
    if (!IsImm(preg))
      BindToRegister(preg, true, false);
  }
}

BitSetHost RegCache::HostRegistersInUse() const
{
  return m_state.m_hosts_in_guest_register | m_state.m_hosts_is_locked;
}

void RegCache::FlushX(X64Reg reg)
{
  ASSERT(!m_state.m_hosts_is_locked[reg]);
  if (m_state.m_hosts_in_guest_register[reg])
  {
    StoreFromRegister(m_state.m_hosts_guest_register[reg]);
  }
}

void RegCache::DiscardRegister(preg_t preg)
{
  if (m_state.m_guests_in_host_register[preg])
  {
    const X64Reg xr = m_state.m_guests_host_register[preg];
    m_state.m_hosts_in_guest_register[xr] = false;
  }

  m_state.m_guests_in_ppc_state[preg] = false;
  m_state.m_guests_in_host_register[preg] = false;
}

void RegCache::BindToRegister(preg_t i, bool doLoad, bool makeDirty)
{
  if (!m_state.m_guests_in_host_register[i])
  {
    const X64Reg xr = GetFreeXReg();

    ASSERT_MSG(DYNA_REC, !m_state.m_hosts_is_locked[xr], "GetFreeXReg returned locked register");
    ASSERT_MSG(DYNA_REC, !m_state.m_guests_revertable[i], "Invalid transaction state");

    m_state.m_hosts_in_guest_register[xr] = true;
    m_state.m_hosts_guest_register[xr] = i;

    if (doLoad)
      LoadRegister(i, xr);

    ASSERT_MSG(DYNA_REC,
               std::ranges::none_of(
                   m_state.m_guests_in_host_register,
                   [&](const auto& r) { return m_state.m_guests_host_register[r] == xr; }),
               "Xreg {} already bound", std::to_underlying(xr));

    m_state.m_guests_in_host_register[i] = true;
    m_state.m_guests_host_register[i] = xr;
  }
  if (makeDirty)
  {
    m_state.m_guests_in_ppc_state[i] = false;
    DiscardImm(i);
  }

  ASSERT_MSG(DYNA_REC, !m_state.m_hosts_is_locked[RX(i)],
             "WTF, this reg ({} -> {}) should have been flushed", i, std::to_underlying(RX(i)));
}

void RegCache::StoreFromRegister(preg_t i, FlushMode mode,
                                 IgnoreDiscardedRegisters ignore_discarded_registers)
{
  // When a transaction is in progress, allowing the store would overwrite the old value.
  ASSERT_MSG(DYNA_REC, !m_state.m_guests_revertable[i],
             "Register transaction on {} is in progress!", i);

  if (!m_state.m_guests_in_ppc_state[i])
    StoreRegister(i, GetPPCStateLocation(i), ignore_discarded_registers);

  if (mode == FlushMode::Full && m_state.m_guests_in_host_register[i])
  {
    m_state.m_guests_in_host_register[i] = false;
    m_state.m_hosts_in_guest_register[m_state.m_guests_host_register[i]] = false;
  }

  m_state.m_guests_in_ppc_state[i] = true;
}

X64Reg RegCache::GetFreeXReg()
{
  const BitSetHost free_registers = GetFreeRegisters();
  if (free_registers)
  {
    for (const X64Reg xr : GetAllocationOrder())
    {
      if (free_registers[xr])
        return xr;
    }
  }

  // Okay, not found; run the register allocator heuristic and
  // figure out which register we should clobber.
  float min_score = std::numeric_limits<float>::max();
  X64Reg best_xreg = INVALID_REG;
  preg_t best_preg = 0;
  for (const preg_t i : GetAllocatableRegisters() & ~m_state.m_hosts_is_locked)
  {
    X64Reg xreg = (X64Reg)i;
    const preg_t preg = m_state.m_hosts_guest_register[xreg];
    if (m_state.m_guests_is_locked[preg])
      continue;

    const float score = ScoreRegister(xreg);
    if (score < min_score)
    {
      min_score = score;
      best_xreg = xreg;
      best_preg = preg;
    }
  }

  if (best_xreg != INVALID_REG)
  {
    StoreFromRegister(best_preg);
    return best_xreg;
  }

  // Still no dice? Die!
  ASSERT_MSG(DYNA_REC, false, "Regcache ran out of regs");
  return INVALID_REG;
}

BitSetHost RegCache::GetFreeRegisters() const
{
  return (~m_state.m_hosts_in_guest_register & ~m_state.m_hosts_is_locked &
          GetAllocatableRegisters());
}

// Estimate roughly how bad it would be to de-allocate this register. Higher score
// means more bad.
float RegCache::ScoreRegister(X64Reg xreg) const
{
  const preg_t preg = m_state.m_hosts_guest_register[xreg];
  float score = 0;

  // If it's not dirty, we don't need a store to write it back to the register file, so
  // bias a bit against dirty registers. Testing shows that a bias of 2 seems roughly
  // right: 3 causes too many extra clobbers, while 1 saves very few clobbers relative
  // to the number of extra stores it causes.
  if (!m_state.m_guests_in_ppc_state[preg])
    score += 2;

  // If the register isn't actually needed in a physical register for a later instruction,
  // writing it back to the register file isn't quite as bad.
  if (GetRegUtilization()[preg])
  {
    // Don't look too far ahead; we don't want to have quadratic compilation times for
    // enormous block sizes!
    // This actually improves register allocation a tiny bit; I'm not sure why.
    const u32 lookahead = std::min(m_jit.js.instructionsLeft, 64);
    // Count how many other registers are going to be used before we need this one again.
    const u32 regs_in_count = CountRegsIn(preg, lookahead).Count();
    // Totally ad-hoc heuristic to bias based on how many other registers we'll need
    // before this one gets used again.
    score += 1 + 2 * (5 - log2f(1 + (float)regs_in_count));
  }

  return score;
}

X64Reg RegCache::RX(preg_t preg) const
{
  ASSERT_MSG(DYNA_REC, m_state.m_guests_in_host_register[preg], "Not in host register - {}", preg);
  return m_state.m_guests_host_register[preg];
}

void RegCache::Lock(preg_t preg)
{
  m_state.m_guests_is_locked[preg] = true;
}

void RegCache::Unlock(preg_t preg)
{
  m_state.m_guests_is_locked[preg] = false;
  m_guests_constraints.Reset(preg);
}

void RegCache::LockX(X64Reg xr)
{
  m_state.m_hosts_is_locked[xr] = true;
}

void RegCache::UnlockX(X64Reg xr)
{
  m_state.m_hosts_is_locked[xr] = false;
}

void RegCache::Realize(preg_t preg)
{
  if (m_guests_constraints.IsRealized(preg))
    return;

  const bool load = m_guests_constraints.ShouldLoad(preg);
  const bool dirty = m_guests_constraints.ShouldDirty(preg);
  const bool kill_imm = m_guests_constraints.ShouldKillImmediate(preg);
  const bool kill_mem = m_guests_constraints.ShouldKillMemory(preg);

  const auto do_bind = [&] {
    BindToRegister(preg, load, dirty);
    m_guests_constraints.Realized(preg, RCConstraints::RealizedLoc::Bound);
  };

  if (m_guests_constraints.ShouldBeRevertable(preg))
  {
    StoreFromRegister(preg, FlushMode::Undirty);
    do_bind();
    m_state.m_guests_revertable[preg] = true;
    return;
  }

  if (IsImm(preg))
  {
    if (dirty || kill_imm)
      do_bind();
    else
      m_guests_constraints.Realized(preg, RCConstraints::RealizedLoc::Imm);
  }
  else if (!m_state.m_guests_in_host_register[preg])
  {
    if (kill_mem)
      do_bind();
    else
      m_guests_constraints.Realized(preg, RCConstraints::RealizedLoc::Mem);
  }
  else
  {
    do_bind();
  }
}
