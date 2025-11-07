// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/Crypto/SHA1.h"

static constexpr std::string_view ACHIEVEMENT_APPROVED_LIST_FILENAME = "ApprovedInis.json";
// After building tests, find the new hash with:
// ./Binaries/Tests/tests --gtest_filter=PatchAllowlist.VerifyHashes
static const inline Common::SHA1::Digest ACHIEVEMENT_APPROVED_LIST_HASH = {
    0x76, 0x6B, 0x45, 0x2B, 0xEF, 0x60, 0x64, 0xA8, 0xBF, 0x08,
    0x2D, 0x04, 0x76, 0x80, 0xDC, 0x09, 0x52, 0xDF, 0x43, 0xEC};
