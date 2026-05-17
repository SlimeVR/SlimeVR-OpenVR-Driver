/*
    SlimeVR Code is placed under the MIT license
    Copyright (c) 2021 Eiren Rain

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/
#include "TrackerRole.hpp"

using solarxr_protocol::datatypes::BodyPart;

std::string GetSerial(BodyPart role) {
    switch (role) {
    case BodyPart::UPPER_CHEST:
    case BodyPart::CHEST:
        return "human://CHEST";
    case BodyPart::LEFT_UPPER_ARM:
        return "human://LEFT_ELBOW";
    case BodyPart::RIGHT_UPPER_ARM:
        return "human://RIGHT_ELBOW";
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

std::string GetViveRoleHint(BodyPart role) {
    switch (role) {
    case BodyPart::LEFT_HAND:
    case BodyPart::RIGHT_HAND:
        return "vive_tracker_handed";
    case BodyPart::LEFT_FOOT:
        return "vive_tracker_left_foot";
    case BodyPart::RIGHT_FOOT:
        return "vive_tracker_right_foot";
    case BodyPart::LEFT_SHOULDER:
        return "vive_tracker_left_shoulder";
    case BodyPart::RIGHT_SHOULDER:
        return "vive_tracker_right_shoulder";
    case BodyPart::LEFT_UPPER_ARM:
        return "vive_tracker_left_elbow";
    case BodyPart::RIGHT_UPPER_ARM:
        return "vive_tracker_right_elbow";
    case BodyPart::LEFT_UPPER_LEG:
        return "vive_tracker_left_knee";
    case BodyPart::RIGHT_UPPER_LEG:
        return "vive_tracker_right_knee";
    case BodyPart::WAIST:
    case BodyPart::HIP:
        return "vive_tracker_waist";
    case BodyPart::CHEST:
        return "vive_tracker_chest";
    default:
        break;
    }
    return "";
}

std::string GetViveRole(BodyPart role) {
    switch (role) {
    case BodyPart::LEFT_HAND:
        return "TrackerRole_Handed,TrackedControllerRole_LeftHand";
    case BodyPart::RIGHT_HAND:
        return "TrackerRole_Handed,TrackedControllerRole_RightHand";
    case BodyPart::LEFT_FOOT:
        return "TrackerRole_LeftFoot";
    case BodyPart::RIGHT_FOOT:
        return "TrackerRole_RightFoot";
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
    case BodyPart::LEFT_UPPER_LEG:
        return "TrackerRole_LeftKnee";
    case BodyPart::RIGHT_UPPER_LEG:
        return "TrackerRole_RightKnee";
    case BodyPart::WAIST:
    case BodyPart::HIP:
        return "TrackerRole_Waist";
    case BodyPart::UPPER_CHEST:
    case BodyPart::CHEST:
        return "TrackerRole_Chest";
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
