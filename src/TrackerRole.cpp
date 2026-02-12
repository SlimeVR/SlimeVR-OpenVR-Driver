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

std::string GetRoleName(TrackerRole role) {
    switch (role) {
    case NONE:
        return "NONE";
    case WAIST:
        return "WAIST";
    case LEFT_FOOT:
        return "LEFT_FOOT";
    case RIGHT_FOOT:
        return "RIGHT_FOOT";
    case CHEST:
        return "CHEST";
    case LEFT_KNEE:
        return "LEFT_KNEE";
    case RIGHT_KNEE:
        return "RIGHT_KNEE";
    case LEFT_ELBOW:
        return "LEFT_ELBOW";
    case RIGHT_ELBOW:
        return "RIGHT_ELBOW";
    case LEFT_SHOULDER:
        return "LEFT_SHOULDER";
    case RIGHT_SHOULDER:
        return "RIGHT_SHOULDER";
    case LEFT_HAND:
        return "LEFT_HAND";
    case RIGHT_HAND:
        return "RIGHT_HAND";
    case LEFT_CONTROLLER:
        return "LEFT_CONTROLLER";
    case RIGHT_CONTROLLER:
        return "RIGHT_CONTROLLER";
    case HEAD:
        return "HEAD";
    case NECK:
        return "NECK";
    case CAMERA:
        return "CAMERA";
    case KEYBOARD:
        return "KEYBOARD";
    case HMD:
        return "HMD";
    case BEACON:
        return "BEACON";
    case GENERIC_CONTROLLER:
        return "GENERIC_CONTROLLER";
    }
}

std::string GetViveRoleHint(TrackerRole role) {
    switch (role) {
        case LEFT_CONTROLLER:
        case RIGHT_CONTROLLER:
        case GENERIC_CONTROLLER:
        case LEFT_HAND:
        case RIGHT_HAND:
            return "vive_tracker_handed";
        case LEFT_FOOT:
            return "vive_tracker_left_foot";
        case RIGHT_FOOT:
            return "vive_tracker_right_foot";
        case LEFT_SHOULDER:
            return "vive_tracker_left_shoulder";
        case RIGHT_SHOULDER:
            return "vive_tracker_right_shoulder";
        case LEFT_ELBOW:
            return "vive_tracker_left_elbow";
        case RIGHT_ELBOW:
            return "vive_tracker_right_elbow";
        case LEFT_KNEE:
            return "vive_tracker_left_knee";
        case RIGHT_KNEE:
            return "vive_tracker_right_knee";
        case WAIST:
            return "vive_tracker_waist";
        case CHEST:
            return "vive_tracker_chest";
        case CAMERA:
            return "vive_tracker_camera";
        case KEYBOARD:
            return "vive_tracker_keyboard";
    }
    return "";
}

std::string GetViveRole(TrackerRole role) {
    switch (role) {
        case GENERIC_CONTROLLER:
            return "TrackerRole_Handed";
        case LEFT_CONTROLLER:
        case LEFT_HAND:
            return "TrackerRole_Handed,TrackedControllerRole_LeftHand";
        case RIGHT_CONTROLLER:
        case RIGHT_HAND:
            return "TrackerRole_Handed,TrackedControllerRole_RightHand";
        case LEFT_FOOT:
            return "TrackerRole_LeftFoot";
        case RIGHT_FOOT:
            return "TrackerRole_RightFoot";
        case LEFT_SHOULDER:
            return "TrackerRole_LeftShoulder";
        case RIGHT_SHOULDER:
            return "TrackerRole_RightShoulder";
        case LEFT_ELBOW:
            return "TrackerRole_LeftElbow";
        case RIGHT_ELBOW:
            return "TrackerRole_RightElbow";
        case LEFT_KNEE:
            return "TrackerRole_LeftKnee";
        case RIGHT_KNEE:
            return "TrackerRole_RightKnee";
        case WAIST:
            return "TrackerRole_Waist";
        case CHEST:
            return "TrackerRole_Chest";
        case CAMERA:
            return "TrackerRole_Camera";
        case KEYBOARD:
            return "TrackerRole_Keyboard";
    }
    return "";
}

DeviceType GetDeviceType(TrackerRole role) {
    switch (role) {
        case LEFT_CONTROLLER:
        case LEFT_HAND:
        case RIGHT_CONTROLLER:
        case RIGHT_HAND:
        case GENERIC_CONTROLLER:
            return DeviceType::CONTROLLER;
        case HMD:
            return DeviceType::HMD;
        case BEACON:
            return DeviceType::TRACKING_REFERENCE;
    }
    return DeviceType::TRACKER;
}
