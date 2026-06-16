// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/Crypto/SHA1.h"

static constexpr std::string_view ACHIEVEMENT_APPROVED_LIST_FILENAME = "ApprovedInis.json";
// After building tests, find the new hash with:
// ./Binaries/Tests/tests --gtest_filter=PatchAllowlist.VerifyHashes
static const inline Common::SHA1::Digest ACHIEVEMENT_APPROVED_LIST_HASH = {
    0xE2, 0x8D, 0x04, 0xE3, 0x26, 0x94, 0x0A, 0x78, 0x3A, 0xDE,
    0x40, 0xD5, 0x7E, 0xD1, 0x2E, 0x34, 0x51, 0x8D, 0xD0, 0x65};
