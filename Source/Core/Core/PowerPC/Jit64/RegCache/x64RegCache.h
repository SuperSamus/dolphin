// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/x64Emitter.h"

class X64RegCacheBase
{
public:
  using OpArg = Gen::OpArg;
  using HostReg = Gen::X64Reg;
};
