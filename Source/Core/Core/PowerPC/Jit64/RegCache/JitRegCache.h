// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/VariantUtil.h"
#include "Core/PowerPC/Jit64/RegCache/RCMode.h"

#include "Common/x64Emitter.h"

using preg_t = u8;

enum class IgnoreDiscardedRegisters
{
  No,
  Yes,
};

enum class FlushMode
{
  // All dirty registers get written back, and all registers get removed from the cache.
  Full,
  // All dirty registers get written back, but the state of the cache is untouched.
  // The host registers may get clobbered. This is intended for use when doing a block exit
  // after a conditional branch.
  MaintainState,
  // All dirty registers get written back and get set as no longer dirty.
  // No registers are removed from the cache.
  Undirty,
};

template <typename T>
class RCOpArg;
template <typename T>
class RCHostReg;
template <typename T>
class RegCache;

template <typename T>
class RCOpArg
{
public:
  static RCOpArg Imm32(u32 imm) { return RCOpArg{imm}; }
  static RCOpArg R(T::HostReg hr) { return RCOpArg{hr}; }
  RCOpArg() = default;
  ~RCOpArg() { Unlock(); }
  RCOpArg(RCOpArg&& other) noexcept
      : rc(std::exchange(other.rc, nullptr)),
        contents(std::exchange(other.contents, std::monostate{}))
  {
  }

  RCOpArg& operator=(RCOpArg&& other) noexcept
  {
    Unlock();
    rc = std::exchange(other.rc, nullptr);
    contents = std::exchange(other.contents, std::monostate{});
    return *this;
  }

  RCOpArg(RCHostReg<T>&& other) noexcept
      : rc(std::exchange(other.rc, nullptr)),
        contents(VariantCast(std::exchange(other.contents, std::monostate{})))
  {
  }

  RCOpArg& operator=(RCHostReg<T>&& other) noexcept
  {
    Unlock();
    rc = std::exchange(other.rc, nullptr);
    contents = VariantCast(std::exchange(other.contents, std::monostate{}));
    return *this;
  }

  RCOpArg(const RCOpArg&) = delete;
  RCOpArg& operator=(const RCOpArg&) = delete;

  void Realize()
  {
    if (const preg_t* preg = std::get_if<preg_t>(&contents))
    {
      rc->Realize(*preg);
    }
  }

  T::OpArg Location() const
  {
    if (const preg_t* preg = std::get_if<preg_t>(&contents))
    {
      ASSERT(rc->IsRealized(*preg));
      return rc->R(*preg);
    }
    else if (const typename T::HostReg* hr = std::get_if<typename T::HostReg>(&contents))
    {
      return Gen::R(*hr);
    }
    else if (const u32* imm = std::get_if<u32>(&contents))
    {
      return Gen::Imm32(*imm);
    }
    ASSERT(false);
    return {};
  }

  operator typename T::OpArg() const& { return Location(); }
  operator typename T::OpArg() const&& = delete;
  bool IsSimpleReg() const { return Location().IsSimpleReg(); }
  bool IsSimpleReg(T::HostReg reg) const { return Location().IsSimpleReg(reg); }
  T::HostReg GetSimpleReg() const { return Location().GetSimpleReg(); }

  void Unlock()
  {
    if (const preg_t* preg = std::get_if<preg_t>(&contents))
    {
      ASSERT(rc);
      rc->Unlock(*preg);
    }
    else if (const typename T::HostReg* hr = std::get_if<typename T::HostReg>(&contents))
    {
      // If rc, we got this from an RCHostReg.
      // If !rc, we got this from RCOpArg::R.
      if (rc)
        rc->UnlockH(*hr);
    }
    else
    {
      ASSERT(!rc);
    }

    rc = nullptr;
    contents = std::monostate{};
  }

  bool IsImm() const { return Location().IsImm(); }
  s32 SImm32() const { return Location().SImm32(); }
  u32 Imm32() const { return Location().Imm32(); }
  bool IsZero() const { return IsImm() && Imm32() == 0; }

private:
  friend class RegCache<T>;

  explicit RCOpArg(u32 imm) : rc(nullptr), contents(imm) {}
  explicit RCOpArg(T::HostReg hr) : rc(nullptr), contents(hr) {}
  RCOpArg(RegCache<T>* rc_, preg_t preg) : rc(rc_), contents(preg) { rc->Lock(preg); }

  RegCache<T>* rc = nullptr;
  std::variant<std::monostate, typename T::HostReg, u32, preg_t> contents;
};

template <typename T>
class RCHostReg
{
public:
  RCHostReg() = default;
  ~RCHostReg() { Unlock(); }
  RCHostReg(RCHostReg&& other) noexcept
      : rc(std::exchange(other.rc, nullptr)),
        contents(std::exchange(other.contents, std::monostate{}))
  {
  }

  RCHostReg& operator=(RCHostReg&& other) noexcept
  {
    Unlock();
    rc = std::exchange(other.rc, nullptr);
    contents = std::exchange(other.contents, std::monostate{});
    return *this;
  }

  RCHostReg(const RCHostReg&) = delete;
  RCHostReg& operator=(const RCHostReg&) = delete;

  void Realize()
  {
    if (const preg_t* preg = std::get_if<preg_t>(&contents))
    {
      rc->Realize(*preg);
    }
  }
  operator typename T::OpArg() const& { return Gen::R(operator typename T::HostReg()); }
  operator typename T::HostReg() const&
  {
    if (const preg_t* preg = std::get_if<preg_t>(&contents))
    {
      ASSERT(rc->IsRealized(*preg));
      return rc->RH(*preg);
    }
    else if (const typename T::HostReg* hr = std::get_if<typename T::HostReg>(&contents))
    {
      return *hr;
    }
    ASSERT(false);
    return {};
  }
  operator typename T::OpArg() const&& = delete;
  operator typename T::HostReg() const&& = delete;

  void Unlock()
  {
    if (const preg_t* preg = std::get_if<preg_t>(&contents))
    {
      ASSERT(rc);
      rc->Unlock(*preg);
    }
    else if (const typename T::HostReg* hr = std::get_if<typename T::HostReg>(&contents))
    {
      ASSERT(rc);
      rc->UnlockH(*hr);
    }
    else
    {
      ASSERT(!rc);
    }

    rc = nullptr;
    contents = std::monostate{};
  }

private:
  friend class RegCache<T>;
  friend class RCOpArg<T>;

  RCHostReg(RegCache<T>* rc_, preg_t preg) : rc(rc_), contents(preg) { rc->Lock(preg); }

  RCHostReg(RegCache<T>* rc_, T::HostReg hr) : rc(rc_), contents(hr) { rc->LockH(hr); }

  RegCache<T>* rc = nullptr;
  std::variant<std::monostate, typename T::HostReg, preg_t> contents;
};

class RCConstraint
{
public:
  bool IsRealized() const { return realized != RealizedLoc::Invalid; }
  bool IsActive() const
  {
    return IsRealized() || write || read || kill_imm || kill_mem || revertable;
  }

  bool ShouldLoad() const { return read; }
  bool ShouldDirty() const { return write; }
  bool ShouldBeRevertable() const { return revertable; }
  bool ShouldKillImmediate() const { return kill_imm; }
  bool ShouldKillMemory() const { return kill_mem; }

  enum class RealizedLoc
  {
    Invalid,
    Bound,
    Imm,
    Mem,
  };

  void Realized(RealizedLoc loc)
  {
    realized = loc;
    ASSERT(IsRealized());
  }

  enum class ConstraintLoc
  {
    Bound,
    BoundOrImm,
    BoundOrMem,
    Any,
  };

  void AddUse(RCMode mode) { AddConstraint(mode, ConstraintLoc::Any, false); }
  void AddUseNoImm(RCMode mode) { AddConstraint(mode, ConstraintLoc::BoundOrMem, false); }
  void AddBindOrImm(RCMode mode) { AddConstraint(mode, ConstraintLoc::BoundOrImm, false); }
  void AddBind(RCMode mode) { AddConstraint(mode, ConstraintLoc::Bound, false); }
  void AddRevertableBind(RCMode mode) { AddConstraint(mode, ConstraintLoc::Bound, true); }

private:
  void AddConstraint(RCMode mode, ConstraintLoc loc, bool should_revertable)
  {
    if (IsRealized())
    {
      ASSERT(IsCompatible(mode, loc, should_revertable));
      return;
    }

    if (should_revertable)
      revertable = true;

    switch (loc)
    {
    case ConstraintLoc::Bound:
      kill_imm = true;
      kill_mem = true;
      break;
    case ConstraintLoc::BoundOrImm:
      kill_mem = true;
      break;
    case ConstraintLoc::BoundOrMem:
      kill_imm = true;
      break;
    case ConstraintLoc::Any:
      break;
    }

    switch (mode)
    {
    case RCMode::Read:
      read = true;
      break;
    case RCMode::Write:
      write = true;
      break;
    case RCMode::ReadWrite:
      read = true;
      write = true;
      break;
    }
  }

  bool IsCompatible(RCMode mode, ConstraintLoc loc, bool should_revertable) const
  {
    if (should_revertable && !revertable)
    {
      return false;
    }

    const bool is_loc_compatible = [&] {
      switch (loc)
      {
      case ConstraintLoc::Bound:
        return realized == RealizedLoc::Bound;
      case ConstraintLoc::BoundOrImm:
        return realized == RealizedLoc::Bound || realized == RealizedLoc::Imm;
      case ConstraintLoc::BoundOrMem:
        return realized == RealizedLoc::Bound || realized == RealizedLoc::Mem;
      case ConstraintLoc::Any:
        return true;
      }
      ASSERT(false);
      return false;
    }();

    const bool is_mode_compatible = [&] {
      switch (mode)
      {
      case RCMode::Read:
        return read;
      case RCMode::Write:
        return write;
      case RCMode::ReadWrite:
        return read && write;
      }
      ASSERT(false);
      return false;
    }();

    return is_loc_compatible && is_mode_compatible;
  }

  RealizedLoc realized = RealizedLoc::Invalid;
  bool write = false;
  bool read = false;
  bool kill_imm = false;
  bool kill_mem = false;
  bool revertable = false;
};

template <typename T>
class RegCache
{
public:
  RegCache(T impl) : rc_impl(impl) {}

  void Start()
  {
    m_hosts_guest_reg = {};
    m_hosts_free = T::BitSetHost::AllTrue();
    m_hosts_locked = {};

    m_guests_host_register = {};
    m_guests_in_ppc_state = T::BitSetGuest::AllTrue();
    m_guests_in_host_register = {};
    m_guests_revertable = {};
    m_guests_locked = {};
    m_guests_is_locked = {};
    m_guests_constraints = {};
  }

  bool IsImm(preg_t preg) const { return rc_impl.IsImm(preg); }

  u32 Imm32(preg_t preg) const { return rc_impl.Imm32(preg); }

  s32 SImm32(preg_t preg) const { return rc_impl.SImm32(preg); }

  bool SanityCheck() const
  {
    if (m_guests_in_host_register & (m_guests_is_locked | m_guests_revertable))
      return false;

    for (const preg_t i : m_guests_in_host_register)
    {
      typename T::HostReg hr = m_guests_host_register[i];
      if (m_hosts_is_locked[hr])
        return false;
      if (m_hosts_guest_reg[hr] != i)
        return false;
    }
    return true;
  }

  template <typename... Ts>
  static void Realize(Ts&... rc)
  {
    static_assert(((std::is_same<Ts, RCOpArg<T>>() || std::is_same<Ts, RCHostReg<T>>()) && ...));
    (rc.Realize(), ...);
  }

  template <typename... Ts>
  static void Unlock(Ts&... rc)
  {
    static_assert(((std::is_same<Ts, RCOpArg<T>>() || std::is_same<Ts, RCHostReg<T>>()) && ...));
    (rc.Unlock(), ...);
  }

  template <typename... Args>
  bool IsImm(Args... pregs) const
  {
    static_assert(sizeof...(pregs) > 0);
    return (IsImm(preg_t(pregs)) && ...);
  }

  bool IsBound(preg_t preg) const { return m_guests_in_host_register[preg]; }

  RCOpArg<T> Use(preg_t preg, RCMode mode)
  {
    m_guests_constraints[preg].AddUse(mode);
    return RCOpArg{this, preg};
  }

  RCOpArg<T> UseNoImm(preg_t preg, RCMode mode)
  {
    m_guests_constraints[preg].AddUseNoImm(mode);
    return RCOpArg{this, preg};
  }

  RCOpArg<T> BindOrImm(preg_t preg, RCMode mode)
  {
    m_guests_constraints[preg].AddBindOrImm(mode);
    return RCOpArg{this, preg};
  }

  RCHostReg<T> Bind(preg_t preg, RCMode mode)
  {
    m_guests_constraints[preg].AddBind(mode);
    return RCHostReg{this, preg};
  }

  RCHostReg<T> RevertableBind(preg_t preg, RCMode mode)
  {
    m_guests_constraints[preg].AddRevertableBind(mode);
    return RCHostReg{this, preg};
  }

  RCHostReg<T> Scratch() { return Scratch(GetFreeHostReg()); }

  RCHostReg<T> Scratch(T::HostReg hr)
  {
    FlushX(hr);
    return RCHostReg{this, hr};
  }

  void Discard(T::BitSetGuest pregs)
  {
    ASSERT_MSG(DYNA_REC, !m_hosts_is_locked, "Someone forgot to unlock a host reg");
    typename T::BitSetGuest locked_pregs = pregs & m_guests_is_locked;
    ASSERT_MSG(DYNA_REC, !locked_pregs, "Someone forgot to unlock the following PPC regs {:b}.",
               locked_pregs.m_val);
    typename T::BitSetGuest revertable_pregs = pregs & m_guests_revertable;
    ASSERT_MSG(DYNA_REC, !revertable_pregs,
               "Register transaction is in progress for the following PPC regs {:b}.",
               revertable_pregs.m_val);

    for (const preg_t i : (pregs & m_guests_in_host_register))
    {
      typename T::HostReg hr = m_guests_host_register[i];
      m_hosts_free[hr] = true;
    }

    m_guests_in_ppc_state &= ~pregs;
    m_guests_in_host_register &= ~pregs;
  }

  void Flush(BitSet32 pregs = T::BitSetGuest::AllTrue(), FlushMode mode = FlushMode::Full,
             IgnoreDiscardedRegisters ignore_discarded_registers = IgnoreDiscardedRegisters::No)
  {
    ASSERT_MSG(DYNA_REC, !m_hosts_is_locked, "Someone forgot to unlock a X64 reg");
    typename T::BitSetGuest locked_pregs = pregs & m_guests_is_locked;
    ASSERT_MSG(DYNA_REC, !locked_pregs, "Someone forgot to unlock the following PPC regs {:b}.",
               locked_pregs.m_val);
    typename T::BitSetGuest revertable_pregs = pregs & m_guests_revertable;
    ASSERT_MSG(DYNA_REC, !revertable_pregs,
               "Register transaction is in progress for the following PPC regs {:b}.",
               revertable_pregs.m_val);

    for (const preg_t i : (pregs & ~m_guests_in_ppc_state))
    {
      StoreRegister(i, rc_impl.GetPPCStateLocation(i), ignore_discarded_registers);
    }

    if (mode == FlushMode::Full)
    {
      for (const preg_t i : (pregs & m_guests_in_host_register))
      {
        typename T::HostReg hr = m_guests_host_register[i];
        m_hosts_free[hr] = true;
      }
    }

    if (mode == FlushMode::Full)
      m_guests_in_host_register &= ~pregs;

    if (mode != FlushMode::MaintainState)
      m_guests_in_ppc_state |= pregs;
  }
  void Flush(FlushMode mode,
             IgnoreDiscardedRegisters ignore_discarded_registers = IgnoreDiscardedRegisters::No)
  {
    Flush(T::BitSetGuest::AllTrue(), mode, ignore_discarded_registers);
  }

  void Reset(T::BitSetGuest pregs)
  {
    typename T::BitSetGuest in_host_register_pregs = pregs & m_guests_in_host_register;
    ASSERT_MSG(DYNA_REC, !in_host_register_pregs,
               "Attempted to reset the loaded registers {:b} (did you mean to flush them?)",
               in_host_register_pregs.m_val);

    m_guests_in_ppc_state |= pregs;
  }

  T::BitSetGuest RegistersRevertable() const
  {
    ASSERT(IsAllUnlocked());
    return m_guests_revertable;
  }

  void Commit()
  {
    ASSERT(IsAllUnlocked());
    m_guests_revertable = {};
  }

  bool IsAllUnlocked() const
  {
    return !m_hosts_is_locked && !m_guests_is_locked && !IsAnyConstraintActive();
  }

  void PreloadRegisters(T::BitSetGuest to_preload)
  {
    for (const preg_t preg : to_preload & ~m_guests_in_host_register)
    {
      if (NumFreeRegisters() < 2)
        return;
      if (!IsImm(preg))
        BindToRegister(preg, true, false);
    }
  }

  T::BitSetHost RegistersInUse() const { return ~m_hosts_free | m_hosts_is_locked; }

protected:
  friend class RCOpArg<T>;
  friend class RCHostReg<T>;

  void FlushX(T::HostReg reg)
  {
    ASSERT(!m_hosts_is_locked[reg]);
    if (!m_hosts_free[reg])
    {
      StoreFromRegister(m_hosts_guest_reg[reg]);
    }
  }

  void DiscardRegister(preg_t preg)
  {
    if (m_guests_in_host_register[preg])
    {
      typename T::HostReg hr = m_guests_host_register[preg];
      m_hosts_free[hr] = true;
    }

    m_guests_in_ppc_state[preg] = false;
    m_guests_in_host_register[preg] = false;
  }

  void LoadRegister(preg_t preg, T::HostReg new_loc)
  {
    if (rc_impl.IsImm(preg))
    {
      rc_impl.LoadConstant(preg, new_loc);
    }
    else
    {
      ASSERT_MSG(DYNA_REC, m_guests_in_ppc_state[preg], "Register {} not in PPCState", preg);
      rc_impl.LoadFromPPCState(preg, new_loc);
    }
  }

  void StoreRegister(preg_t preg, const T::OpArg& new_loc,
                     IgnoreDiscardedRegisters ignore_discarded_registers)
  {
    if (m_guests_in_host_register[preg])
    {
      rc_impl.StoreRegister(m_guests_host_register[preg], new_loc);
    }
    else if (rc_impl.IsImm(preg))
    {
      rc_impl.StoreConstant(preg, new_loc);
    }
    else
    {
      ASSERT_MSG(DYNA_REC, ignore_discarded_registers != IgnoreDiscardedRegisters::No,
                 "Register {} not in host register or constant propagation", preg);
    }
  }

  void BindToRegister(preg_t i, bool doLoad, bool makeDirty)
  {
    if (!m_guests_in_host_register[i])
    {
      typename T::HostReg hr = GetFreeHostReg();

      ASSERT_MSG(DYNA_REC, !m_hosts_is_locked[hr], "GetFreeHostReg returned locked register");
      ASSERT_MSG(DYNA_REC, !m_guests_revertable[i], "Invalid transaction state");

      m_hosts_free[hr] = false;
      m_hosts_guest_reg[hr] = i;

      if (doLoad)
        LoadRegister(i, hr);

      ASSERT_MSG(
          DYNA_REC,
          std::ranges::none_of(m_guests_in_host_register,
                               [&](const auto& r) { return m_guests_host_register[r] == hr; }),
          "Host reg {} already bound", std::to_underlying(hr));

      m_guests_in_host_register[i] = true;
      m_guests_host_register[i] = hr;
    }
    if (makeDirty)
    {
      m_guests_in_ppc_state[i] = false;
      rc_impl.DiscardImm(i);
    }

    ASSERT_MSG(DYNA_REC, !m_hosts_is_locked[RH(i)],
               "WTF, this reg ({} -> {}) should have been flushed", i, std::to_underlying(RH(i)));
  }

  void StoreFromRegister(
      preg_t i, FlushMode mode = FlushMode::Full,
      IgnoreDiscardedRegisters ignore_discarded_registers = IgnoreDiscardedRegisters::No)
  {
    // When a transaction is in progress, allowing the store would overwrite the old value.
    ASSERT_MSG(DYNA_REC, !m_guests_revertable[i], "Register transaction on {} is in progress!", i);

    if (!m_guests_in_ppc_state[i])
      StoreRegister(i, rc_impl.GetPPCStateLocation(i), ignore_discarded_registers);

    if (mode == FlushMode::Full && m_guests_in_host_register[i])
    {
      m_guests_in_host_register[i] = false;
      m_hosts_free[m_guests_host_register[i]] = true;
    }

    if (mode != FlushMode::MaintainState)
      m_guests_in_ppc_state[i] = true;
  }

  T::HostReg FirstFreeRegister(const T::BitSetHost free_registers) const
  {
    // Force loop unrolling
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      typename T::HostReg result = Gen::INVALID_REG;
      auto allocation_order = T::GetAllocationOrder();
      // Each iteration is forced to be checked in order
      ((free_registers[allocation_order[Is]] ? (result = allocation_order[Is], true) : false) ||
       ...);
      return result;
    }(std::make_index_sequence<T::GetAllocationOrder().size()>{});
  }

  T::HostReg GetFreeHostReg()
  {
    constexpr typename T::BitSetHost allocatable_registers = GetAllocatableRegisters();
    typename T::BitSetHost free_registers =
        m_hosts_free & ~m_hosts_is_locked & allocatable_registers;
    if (free_registers)
      return FirstFreeRegister(free_registers);

    // Okay, not found; run the register allocator heuristic and
    // figure out which register we should clobber.
    float min_score = std::numeric_limits<float>::max();
    typename T::HostReg best_hreg = Gen::INVALID_REG;
    preg_t best_preg = 0;
    for (const preg_t i : allocatable_registers & ~m_hosts_is_locked)
    {
      auto hreg = (typename T::HostReg)i;
      const preg_t preg = m_hosts_guest_reg[hreg];
      if (m_guests_is_locked[preg])
        continue;

      const float score = ScoreRegister(hreg);
      if (score < min_score)
      {
        min_score = score;
        best_hreg = hreg;
        best_preg = preg;
      }
    }

    if (best_hreg != -1)
    {
      StoreFromRegister(best_preg);
      return best_hreg;
    }

    // Still no dice? Die!
    ASSERT_MSG(DYNA_REC, false, "Regcache ran out of regs");
    return -1;
  }

  static consteval T::BitSetHost GetAllocatableRegisters()
  {
    typename T::BitSetHost result;
    for (const preg_t i : T::GetAllocationOrder())
      result[i] = true;
    return result;
  }

  unsigned int NumFreeRegisters() const
  {
    return (m_hosts_free & ~m_hosts_is_locked & GetAllocatableRegisters()).Count();
  }

  // Estimate roughly how bad it would be to de-allocate this register. Higher score
  // means more bad.
  float ScoreRegister(T::HostReg hreg) const
  {
    preg_t preg = m_hosts_guest_reg[hreg];
    float score = 0;

    // If it's not dirty, we don't need a store to write it back to the register file, so
    // bias a bit against dirty registers. Testing shows that a bias of 2 seems roughly
    // right: 3 causes too many extra clobbers, while 1 saves very few clobbers relative
    // to the number of extra stores it causes.
    if (!m_guests_in_ppc_state[preg])
      score += 2;

    // If the register isn't actually needed in a physical register for a later instruction,
    // writing it back to the register file isn't quite as bad.
    if (rc_impl.GetRegUtilization()[preg])
    {
      // Don't look too far ahead; we don't want to have quadratic compilation times for
      // enormous block sizes!
      // This actually improves register allocation a tiny bit; I'm not sure why.
      u32 lookahead = std::min(rc_impl.JitNumInstructionsLeft(), 64);
      // Count how many other registers are going to be used before we need this one again.
      u32 regs_in_count = rc_impl.CountRegsIn(preg, lookahead).Count();
      // Totally ad-hoc heuristic to bias based on how many other registers we'll need
      // before this one gets used again.
      score += 1 + 2 * (5 - log2f(1 + (float)regs_in_count));
    }

    return score;
  }

  T::OpArg R(preg_t preg) const
  {
    if (m_guests_in_host_register[preg])
    {
      return Gen::R(m_guests_host_register[preg]);
    }
    else if (rc_impl.IsImm(preg))
    {
      return Gen::Imm32(rc_impl.Imm32(preg));
    }
    else
    {
      ASSERT_MSG(DYNA_REC, m_guests_in_ppc_state[preg], "Register {} missing!", preg);
      return rc_impl.GetPPCStateLocation(preg);
    }
  }

  T::HostReg RH(preg_t preg) const
  {
    ASSERT_MSG(DYNA_REC, m_guests_in_host_register[preg], "Not in host register - {}", preg);
    return m_guests_host_register[preg];
  }

  void Lock(preg_t preg)
  {
    ++m_guests_locked[preg];
    m_guests_is_locked[preg] = true;
  }

  void Unlock(preg_t preg)
  {
    --m_guests_locked[preg];
    if (m_guests_locked[preg] == 0)
    {
      m_guests_is_locked[preg] = false;
      // Fully unlocked, reset realization state.
      m_guests_constraints[preg] = {};
    }
  }

  void LockH(T::HostReg hr)
  {
    ++m_hosts_locked[hr];
    m_hosts_is_locked[hr] = true;
  }

  void UnlockH(T::HostReg hr)
  {
    ASSERT(m_hosts_locked[hr] > 0 && m_hosts_is_locked[hr]);
    --m_hosts_locked[hr];
    m_hosts_is_locked[hr] = m_hosts_locked[hr] > 0;
  }

  bool IsRealized(preg_t preg) const { return m_guests_constraints[preg].IsRealized(); }

  void Realize(preg_t preg)
  {
    if (m_guests_constraints[preg].IsRealized())
      return;

    const bool load = m_guests_constraints[preg].ShouldLoad();
    const bool dirty = m_guests_constraints[preg].ShouldDirty();
    const bool kill_imm = m_guests_constraints[preg].ShouldKillImmediate();
    const bool kill_mem = m_guests_constraints[preg].ShouldKillMemory();

    const auto do_bind = [&] {
      BindToRegister(preg, load, dirty);
      m_guests_constraints[preg].Realized(RCConstraint::RealizedLoc::Bound);
    };

    if (m_guests_constraints[preg].ShouldBeRevertable())
    {
      StoreFromRegister(preg, FlushMode::Undirty);
      do_bind();
      m_guests_revertable[preg] = true;
      return;
    }

    if (IsImm(preg))
    {
      if (dirty || kill_imm)
        do_bind();
      else
        m_guests_constraints[preg].Realized(RCConstraint::RealizedLoc::Imm);
    }
    else if (!m_guests_in_host_register[preg])
    {
      if (kill_mem)
        do_bind();
      else
        m_guests_constraints[preg].Realized(RCConstraint::RealizedLoc::Mem);
    }
    else
    {
      do_bind();
    }
  }

  bool IsAnyConstraintActive() const
  {
    return std::ranges::any_of(m_guests_constraints, &RCConstraint::IsActive);
  }

  T rc_impl;

  std::array<u8, T::NUM_HOST_REGS> m_hosts_guest_reg = {};
  T::BitSetHost m_hosts_free = T::BitSetHost::AllTrue();
  std::array<u8, T::NUM_HOST_REGS> m_hosts_locked = {};
  T::BitSetHost m_hosts_is_locked;

  T::BitSetGuest m_guests_in_ppc_state = T::BitSetGuest::AllTrue();
  std::array<typename T::HostReg, T::NUM_GUEST_REGS> m_guests_host_register{};
  T::BitSetGuest m_guests_in_host_register;
  T::BitSetGuest m_guests_revertable;
  std::array<u8, T::NUM_GUEST_REGS> m_guests_locked = {};
  T::BitSetGuest m_guests_is_locked;
  std::array<RCConstraint, T::NUM_GUEST_REGS> m_guests_constraints;
};
