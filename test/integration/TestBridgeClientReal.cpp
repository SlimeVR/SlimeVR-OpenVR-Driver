#include <catch2/catch_test_macros.hpp>

#include "../common/TestBridgeClient.hpp"
#include "DriverFactory.hpp"
#include "TrackerRole.hpp"

TEST_CASE("IO with a real server", "[Bridge]") {
    TestBridgeClient();
}