// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <optional>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/x64Emitter.h"
#include "Core/PowerPC/Jit64/RegCache/RCMode.h"

using preg_t = size_t;

// A struct representing the state of a PPC register
class PPCCachedReg
{
public:
  PPCCachedReg() = default;

  explicit PPCCachedReg(Gen::OpArg default_location) : m_default_location(default_location) {}

  // Get where the register is stored in memory.
  Gen::OpArg GetDefaultLocation() const { return m_default_location; }

  // Get the host register in which this PPC register is bound to.
  std::optional<Gen::X64Reg> GetHostRegister() const { return m_host_register; }

  // Does the value stored in memory correspond the real value of the register?
  bool IsInDefaultLocation() const { return m_in_default_location; }
  // Is this register bound to a host register?
  bool IsInHostRegister() const { return m_host_register.has_value(); }

  // Claim that this register has been flushed to memory.
  void SetFlushed(bool maintain_host_register)
  {
    // When a transaction is in progress, allowing the store would overwrite the old value.
    ASSERT(!m_revertable);
    if (!maintain_host_register)
    {
      ASSERT(!IsLocked());
      m_host_register = std::nullopt;
    }
    m_in_default_location = true;
  }

  // Bind this register to a host register.
  void SetInHostRegister(Gen::X64Reg xreg, bool dirty)
  {
    ASSERT(!IsInHostRegister());
    ASSERT(!m_revertable);
    if (dirty)
      m_in_default_location = false;
    m_host_register = xreg;
  }

  // Claim that the value in memory now isn't accurate, but the value of the host register is.
  void SetDirty()
  {
    ASSERT(IsInHostRegister());
    m_in_default_location = false;
  }

  // Unbind the host register, despite its value not being flushed to memory.
  // Do this when it's known that the register will be written to before being read.
  void SetDiscarded()
  {
    ASSERT(!IsLocked());
    ASSERT(!m_revertable);
    m_in_default_location = false;
    m_host_register = std::nullopt;
  }

  // Is the value of the register staged to be reverted in case of a load error?
  bool IsRevertable() const { return m_revertable; }

  // In case of a load exception, stage the register to potentially be reverted.
  void SetRevertable()
  {
    ASSERT(m_host_register.has_value());
    m_revertable = true;
  }
  // There has been an exception in loading a value: set the correct value to the one stored in
  // memory, and unbind the host register.
  void SetRevert()
  {
    ASSERT(!IsLocked());
    ASSERT(m_revertable);
    m_revertable = false;
    SetFlushed(false);
  }
  // Loading a value has been successful, continue normally.
  void SetCommit()
  {
    ASSERT(!IsLocked());
    ASSERT(m_revertable);
    m_revertable = false;
  }

  bool IsLocked() const { return m_locked > 0; }
  void Lock() { m_locked++; }
  void Unlock()
  {
    ASSERT(IsLocked());
    m_locked--;
  }

private:
  Gen::OpArg m_default_location{};
  std::optional<Gen::X64Reg> m_host_register{};
  bool m_in_default_location = true;
  bool m_revertable = false;
  size_t m_locked = 0;
};

class X64CachedReg
{
public:
  std::optional<preg_t> Contents() const { return ppcReg; }

  void SetBoundTo(preg_t ppcReg_)
  {
    ASSERT(!ppcReg.has_value());
    ppcReg = ppcReg_;
  }

  void Unbind()
  {
    ASSERT(!IsLocked() && ppcReg.has_value());
    ppcReg = std::nullopt;
  }

  bool IsFree() const { return !ppcReg.has_value() && !locked; }

  bool IsLocked() const { return locked > 0; }
  void Lock() { locked++; }
  void Unlock()
  {
    ASSERT(IsLocked());
    locked--;
  }

private:
  std::optional<preg_t> ppcReg = std::nullopt;
  size_t locked = 0;
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
