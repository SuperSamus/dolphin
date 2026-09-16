// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CPUCoreBase
{
public:
  virtual ~CPUCoreBase() = default;
  virtual void Init() = 0;
  virtual void Shutdown() = 0;
  // If poison is false, then this can be called from inside the JIT.
  // However, because other members necessary for running the JIT are cleared (e.g. trampoline
  // info), it's still a requirement that it returns to dispatcher by itself immediately after.
  virtual void ClearCache(bool poison = true) = 0;
  virtual void Run() = 0;
  virtual void SingleStep() = 0;
  virtual const char* GetName() const = 0;
};
