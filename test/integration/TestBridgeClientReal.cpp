// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include <catch2/catch_test_macros.hpp>

#include "../common/TestBridgeClient.hpp"

TEST_CASE("IO with a real server", "[Bridge]") {
    TestBridgeClient();
}