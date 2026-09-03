// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "TrackerDevice.hpp"
#include "DriverFactory.hpp"

#include <algorithm>
#include <cmath>

#include <solarxr_protocol/generated/all_generated.h>

using namespace solarxr_protocol;

SlimeVRDriver::TrackerDevice::TrackerDevice(std::string serial, datatypes::BodyPart body_part)
    : serial_(serial)
    , body_part_(body_part) { }

datatypes::BodyPart SlimeVRDriver::TrackerDevice::GetBodyPart() {
    return body_part_;
}
std::string SlimeVRDriver::TrackerDevice::GetSerial() {
    return serial_;
}

void SlimeVRDriver::TrackerDevice::Update() {
    if (device_index_ == vr::k_unTrackedDeviceIndexInvalid)
        return;

    // Check if this device was asked to be identified
    auto& events = GetDriver()->GetOpenVREvents();
    for (const auto& event : events) {
        // Note here, event.trackedDeviceIndex does not necessarily equal device_index_, not sure why, but the component handle will match so we can just use that instead
        // if (event.trackedDeviceIndex == device_index_) {
        if (event.eventType == vr::EVREventType::VREvent_Input_HapticVibration) {
            if (event.data.hapticVibration.componentHandle == haptic_component_) {
                did_vibrate_ = true;
            }
        }
        //}
    }

    // Check if we need to keep vibrating
    if (did_vibrate_) {
        vibrate_anim_state_ += GetDriver()->GetLastFrameTime().count() / 1000.f;
        if (vibrate_anim_state_ > 1.0f) {
            did_vibrate_ = false;
            vibrate_anim_state_ = 0.0f;
        }
    }
}

void SlimeVRDriver::TrackerDevice::UpdatePose(const solarxr_protocol::datatypes::math::Quat* orientation,
                                              const solarxr_protocol::datatypes::math::Vec3f* position,
                                              const solarxr_protocol::datatypes::math::Vec3f* linear_velocity,
                                              const solarxr_protocol::datatypes::math::Vec3f* angular_velocity) {
    if (device_index_ == vr::k_unTrackedDeviceIndexInvalid)
        return;

#ifndef NDEBUG
#define CHECK_CLASSIFICATION(D)                                                                    \
    if (auto classification = std::fpclassify((D)); classification == FP_NAN) {                    \
        logger_->Log("Uh oh! fpclassify(" #D ") returned FP_NAN for {}: {}, zeroing", D, serial_); \
        D = 0.0;                                                                                   \
    }
#else
#define CHECK_CLASSIFICATION(D)                                               \
    if (auto classification = std::fpclassify((D)); classification == FP_NAN) \
        D = 0.0;
#endif

    // Setup pose for this frame
    vr::DriverPose_t pose = last_pose_;
    // send the new position and rotation from the pipe to the tracker object
    if (position != nullptr) {
        pose.vecPosition[0] = position->x();
        pose.vecPosition[1] = position->y();
        pose.vecPosition[2] = position->z();
        CHECK_CLASSIFICATION(pose.vecPosition[0]);
        CHECK_CLASSIFICATION(pose.vecPosition[1]);
        CHECK_CLASSIFICATION(pose.vecPosition[2]);
    }

    if (orientation != nullptr) {
        pose.qRotation.w = orientation->w();
        pose.qRotation.x = orientation->x();
        pose.qRotation.y = orientation->y();
        pose.qRotation.z = orientation->z();
        CHECK_CLASSIFICATION(pose.qRotation.w);
        CHECK_CLASSIFICATION(pose.qRotation.x);
        CHECK_CLASSIFICATION(pose.qRotation.y);
        CHECK_CLASSIFICATION(pose.qRotation.z);
    }

    if (linear_velocity != nullptr) {
        pose.vecVelocity[0] = linear_velocity->x();
        pose.vecVelocity[1] = linear_velocity->y();
        pose.vecVelocity[2] = linear_velocity->z();
        CHECK_CLASSIFICATION(pose.vecVelocity[0]);
        CHECK_CLASSIFICATION(pose.vecVelocity[1]);
        CHECK_CLASSIFICATION(pose.vecVelocity[2]);
    } else {
        std::ranges::fill(pose.vecVelocity, 0.0);
    }

    if (angular_velocity != nullptr) {
        pose.vecAngularVelocity[0] = angular_velocity->x();
        pose.vecAngularVelocity[1] = angular_velocity->y();
        pose.vecAngularVelocity[2] = angular_velocity->z();
        CHECK_CLASSIFICATION(pose.vecAngularVelocity[0]);
        CHECK_CLASSIFICATION(pose.vecAngularVelocity[1]);
        CHECK_CLASSIFICATION(pose.vecAngularVelocity[2]);
    } else {
        std::ranges::fill(pose.vecAngularVelocity, 0.0);
    }

    auto current_universe = GetDriver()->GetCurrentUniverse();
    if (current_universe.has_value()) {
        auto trans = current_universe.value();

        // TODO: set this once, somewhere?
        pose.vecWorldFromDriverTranslation[0] = -trans.translation.v[0];
        pose.vecWorldFromDriverTranslation[1] = -trans.translation.v[1];
        pose.vecWorldFromDriverTranslation[2] = -trans.translation.v[2];
        CHECK_CLASSIFICATION(pose.vecWorldFromDriverTranslation[0]);
        CHECK_CLASSIFICATION(pose.vecWorldFromDriverTranslation[1]);
        CHECK_CLASSIFICATION(pose.vecWorldFromDriverTranslation[2]);

        pose.qWorldFromDriverRotation.w = cos(trans.yaw / 2);
        pose.qWorldFromDriverRotation.x = 0.0;
        pose.qWorldFromDriverRotation.y = sin(trans.yaw / 2);
        pose.qWorldFromDriverRotation.z = 0.0;
        CHECK_CLASSIFICATION(pose.qWorldFromDriverRotation.w);
        CHECK_CLASSIFICATION(pose.qWorldFromDriverRotation.y);
    }

    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(device_index_, pose, sizeof(vr::DriverPose_t));
    last_pose_ = pose;
}

void SlimeVRDriver::TrackerDevice::UpdateBattery(float battery_percentage, bool charging) {
    if (this->device_index_ == vr::k_unTrackedDeviceIndexInvalid)
        return;

    // Get the properties handle
    auto props = vr::VRProperties()->TrackedDeviceToPropertyContainer(this->device_index_);

    // Set that the tracker reports battery level in case it has not already been set to true
    // It's a given that the tracker supports reporting battery life because otherwise a BatteryMessage would not be received
    if (!vr::VRProperties()->GetBoolProperty(props, vr::Prop_DeviceProvidesBatteryStatus_Bool)) {
        vr::VRProperties()->SetBoolProperty(props, vr::Prop_DeviceProvidesBatteryStatus_Bool, true);
    }

    vr::VRProperties()->SetBoolProperty(props, vr::Prop_DeviceIsCharging_Bool, charging);

    // Set the battery Level; 0 = 0%, 1 = 100%
    vr::VRProperties()->SetFloatProperty(props, vr::Prop_DeviceBatteryPercentage_Float, battery_percentage);
}

void SlimeVRDriver::TrackerDevice::UpdateStatus(datatypes::TrackerStatus status) {
    if (device_index_ == vr::k_unTrackedDeviceIndexInvalid || status_ == status)
        return;

    status_ = status;

    auto& pose = last_pose_;
    switch (status_) {
    case datatypes::TrackerStatus::OK:
        pose.deviceIsConnected = true;
        pose.poseIsValid = true;
        pose.result = vr::ETrackingResult::TrackingResult_Running_OK;
        break;

    case datatypes::TrackerStatus::DISCONNECTED:
        pose.deviceIsConnected = false;
        pose.poseIsValid = false;
        pose.result = vr::ETrackingResult::TrackingResult_Uninitialized;
        break;
    case datatypes::TrackerStatus::BUSY:
    case datatypes::TrackerStatus::OCCLUDED:
        pose.deviceIsConnected = true;
        pose.poseIsValid = true;
        pose.result = vr::ETrackingResult::TrackingResult_Calibrating_OutOfRange;
        break;
    case datatypes::TrackerStatus::ERROR:
    default:
        pose.deviceIsConnected = true;
        pose.poseIsValid = false;
        pose.result = vr::ETrackingResult::TrackingResult_Uninitialized;
        break;
    }

    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(device_index_, pose, sizeof(vr::DriverPose_t));
}

DeviceType SlimeVRDriver::TrackerDevice::GetDeviceType() {
    return DeviceType::TRACKER;
}

vr::TrackedDeviceIndex_t SlimeVRDriver::TrackerDevice::GetDeviceIndex() {
    return device_index_;
}

vr::EVRInitError SlimeVRDriver::TrackerDevice::Activate(uint32_t unObjectId) {
    device_index_ = unObjectId;

    logger_->Log("Activating tracker {}", serial_);

    auto props = vr::VRProperties()->TrackedDeviceToPropertyContainer(device_index_);

    vr::VRProperties()->SetStringProperty(props, vr::Prop_ManufacturerName_String, "SlimeVR");
    vr::VRProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "SlimeVR Virtual Tracker");

    vr::VRProperties()->SetStringProperty(props, vr::Prop_RenderModelName_String, "{htc}/rendermodels/vr_tracker_vive_1_0");

    // Some device properties will be derived at runtime by SteamVR
    // using the profile, such as the device class and controller type
    bool emulate_vives = vr::VRSettings()->GetBool("driver_slimevr", "emulateVives");
    std::string input_profile_path = emulate_vives ? "{htc}/input/vive_tracker_profile.json" : "{slimevr}/input/slimevr_tracker_profile.json";
    vr::VRProperties()->SetStringProperty(props, vr::Prop_InputProfilePath_String, input_profile_path.c_str());

    return vr::EVRInitError::VRInitError_None;
}

void SlimeVRDriver::TrackerDevice::Deactivate() {
    device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

void SlimeVRDriver::TrackerDevice::EnterStandby() {
}

void* SlimeVRDriver::TrackerDevice::GetComponent(const char* pchComponentNameAndVersion) {
    return nullptr;
}

void SlimeVRDriver::TrackerDevice::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) {
    if (unResponseBufferSize >= 1) {
        pchResponseBuffer[0] = 0;
    }
}
