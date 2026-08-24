#include <catch2/catch_test_macros.hpp>

#include "../common/TestBridgeClient.hpp"

TEST_CASE("IO with a real server", "[Bridge]") {
    TestBridgeClient();
}