#pragma once

#include <catch2/catch_test_macros.hpp>

#include "DriverFactory.hpp"
#include "bridge/BridgeClient.hpp"
#include "TrackerRole.hpp"

void TestLogTrackerAdded(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message);
void TestLogTrackerStatus(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message);
void TestLogVersion(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message);
void TestBridgeClient();
