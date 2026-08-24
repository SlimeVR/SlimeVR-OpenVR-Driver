#pragma once

#include <catch2/catch_test_macros.hpp>

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

void TestBridgeClient();
