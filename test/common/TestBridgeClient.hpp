// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#pragma once

#include <catch2/catch_test_macros.hpp>

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

void TestBridgeClient();
