// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "TrackerRole.hpp"

using solarxr_protocol::datatypes::BodyPart;

std::string GetSerial(BodyPart role) {
    switch (role) {
    case BodyPart::UPPER_CHEST:
    case BodyPart::CHEST:
        return "human://CHEST";
    case BodyPart::LEFT_SHOULDER:
        return "human://LEFT_SHOULDER";
    case BodyPart::RIGHT_SHOULDER:
        return "human://RIGHT_SHOULDER";
    case BodyPart::LEFT_UPPER_ARM:
        return "human://LEFT_ELBOW";
    case BodyPart::RIGHT_UPPER_ARM:
        return "human://RIGHT_ELBOW";
    case BodyPart::LEFT_LOWER_ARM:
        return "human://LEFT_WRIST";
    case BodyPart::RIGHT_LOWER_ARM:
        return "human://RIGHT_WRIST";
    case BodyPart::LEFT_HAND:
        return "human://LEFT_HAND";
    case BodyPart::RIGHT_HAND:
        return "human://RIGHT_HAND";
    case BodyPart::WAIST:
    case BodyPart::HIP:
        return "human://WAIST";
    case BodyPart::LEFT_UPPER_LEG:
        return "human://LEFT_KNEE";
    case BodyPart::RIGHT_UPPER_LEG:
        return "human://RIGHT_KNEE";
    case BodyPart::LEFT_FOOT:
        return "human://LEFT_FOOT";
    case BodyPart::RIGHT_FOOT:
        return "human://RIGHT_FOOT";
    default:
        break;
    }
    return "";
}

std::string GetViveControllerType(BodyPart role) {
    switch (role) {
    case BodyPart::UPPER_CHEST:
    case BodyPart::CHEST:
        return "vive_tracker_chest";
    case BodyPart::LEFT_SHOULDER:
        return "vive_tracker_left_shoulder";
    case BodyPart::RIGHT_SHOULDER:
        return "vive_tracker_right_shoulder";
    case BodyPart::LEFT_UPPER_ARM:
        return "vive_tracker_left_elbow";
    case BodyPart::RIGHT_UPPER_ARM:
        return "vive_tracker_right_elbow";
    case BodyPart::LEFT_LOWER_ARM:
        return "vive_tracker_left_wrist";
    case BodyPart::RIGHT_LOWER_ARM:
        return "vive_tracker_right_wrist";
    case BodyPart::LEFT_HAND:
    case BodyPart::RIGHT_HAND:
        return "vive_tracker_handed";
    case BodyPart::WAIST:
    case BodyPart::HIP:
        return "vive_tracker_waist";
    case BodyPart::LEFT_UPPER_LEG:
        return "vive_tracker_left_knee";
    case BodyPart::RIGHT_UPPER_LEG:
        return "vive_tracker_right_knee";
    case BodyPart::LEFT_FOOT:
        return "vive_tracker_left_foot";
    case BodyPart::RIGHT_FOOT:
        return "vive_tracker_right_foot";
    default:
        break;
    }
    return "";
}

std::string GetTrackerRole(BodyPart role) {
    switch (role) {
    case BodyPart::UPPER_CHEST:
    case BodyPart::CHEST:
        return "TrackerRole_Chest";
    case BodyPart::LEFT_SHOULDER:
        return "TrackerRole_LeftShoulder";
    case BodyPart::RIGHT_SHOULDER:
        return "TrackerRole_RightShoulder";
    case BodyPart::LEFT_UPPER_ARM:
        return "TrackerRole_LeftElbow";
    case BodyPart::RIGHT_UPPER_ARM:
        return "TrackerRole_RightElbow";
    case BodyPart::LEFT_LOWER_ARM:
        return "TrackerRole_LeftWrist";
    case BodyPart::RIGHT_LOWER_ARM:
        return "TrackerRole_RightWrist";
    case BodyPart::LEFT_HAND:
        return "TrackerRole_Handed,TrackedControllerRole_LeftHand";
    case BodyPart::RIGHT_HAND:
        return "TrackerRole_Handed,TrackedControllerRole_RightHand";
    case BodyPart::WAIST:
    case BodyPart::HIP:
        return "TrackerRole_Waist";
    case BodyPart::LEFT_UPPER_LEG:
        return "TrackerRole_LeftKnee";
    case BodyPart::RIGHT_UPPER_LEG:
        return "TrackerRole_RightKnee";
    case BodyPart::LEFT_FOOT:
        return "TrackerRole_LeftFoot";
    case BodyPart::RIGHT_FOOT:
        return "TrackerRole_RightFoot";
    default:
        break;
    }
    return "";
}

DeviceType GetDeviceType(BodyPart role) {
    switch (role) {
    case BodyPart::LEFT_HAND:
    case BodyPart::RIGHT_HAND:
        return DeviceType::CONTROLLER;
    case BodyPart::HEAD:
        return DeviceType::HMD;
    default:
        break;
    }
    return DeviceType::TRACKER;
}
