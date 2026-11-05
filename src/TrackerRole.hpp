// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#pragma once

#include "DeviceType.hpp"
#include <solarxr_protocol/generated/all_generated.h>
#include <string>

std::string GetSerial(solarxr_protocol::datatypes::BodyPart role);

std::string GetViveControllerType(solarxr_protocol::datatypes::BodyPart role);

std::string GetTrackerRole(solarxr_protocol::datatypes::BodyPart role);

DeviceType GetDeviceType(solarxr_protocol::datatypes::BodyPart role);
