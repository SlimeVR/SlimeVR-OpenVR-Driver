#pragma once

#include <catch2/catch_test_macros.hpp>

#include "DriverFactory.hpp"
#include "bridge/BridgeClient.hpp"
#include "TrackerRole.hpp"

void testLogTrackerAdded(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message);
void testLogTrackerStatus(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message);
void testBridgeClient();
