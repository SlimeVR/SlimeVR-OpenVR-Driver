#pragma once

#include <openvr_driver.h>

#include "DeviceType.hpp"
#include "solarxr_protocol/generated/all_generated.h"

namespace SlimeVRDriver {

class IVRDevice : public vr::ITrackedDeviceServerDriver {
public:
    virtual solarxr_protocol::datatypes::BodyPart GetBodyPart() = 0;

    /**
     * Returns the serial string for this device.
     *
     * @return Device serial.
     */
    virtual std::string GetSerial() = 0;

    /**
     * Runs any update logic for this device.
     * Called once per frame.
     */
    virtual void Update() = 0;

    /**
     * Returns the OpenVR device index.
     * This should be 0 for HMDs.
     *
     * @returns OpenVR device index.
     */
    virtual vr::TrackedDeviceIndex_t GetDeviceIndex() = 0;

    /**
     * Returns which type of device this device is.
     *
     * @returns The type of device.
     */
    virtual DeviceType GetDeviceType() = 0;

    /**
     * Makes a default device pose.
     *
     * @returns Default initialised pose.
     */
    static constexpr vr::DriverPose_t MakeDefaultPose(bool connected = false, bool tracking = false) {
        return {
            .poseTimeOffset = 0,
            .qWorldFromDriverRotation = {
                .w = 1.0,
            },
            .qDriverFromHeadRotation = {
                .w = 1.0,
            },
            .qRotation = {
                .w = 1.0,
            },
            .result = tracking ? vr::ETrackingResult::TrackingResult_Running_OK : vr::ETrackingResult::TrackingResult_Running_OutOfRange,
            .poseIsValid = tracking,
            .willDriftInYaw = true,
            .shouldApplyHeadModel = false,
            .deviceIsConnected = connected,
        };
    }

    /**
     * Updates device pose from a received message.
     */
    virtual void UpdatePose(const solarxr_protocol::datatypes::math::Quat* rot, const solarxr_protocol::datatypes::math::Vec3f* pos, solarxr_protocol::datatypes::TrackerStatus status) = 0;

    /**
     * Updates device status from a received message.
     */
    virtual void UpdateStatus(solarxr_protocol::datatypes::TrackerStatus status) = 0;

    /**
     * Updates battery indicator from a received message.
     */
    virtual void UpdateBattery(float battery_percentage, bool charging) = 0;

    // Inherited via ITrackedDeviceServerDriver
    virtual vr::EVRInitError Activate(uint32_t unObjectId) = 0;
    virtual void Deactivate() = 0;
    virtual void EnterStandby() = 0;
    virtual void* GetComponent(const char* pchComponentNameAndVersion) = 0;
    virtual void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) = 0;
    virtual vr::DriverPose_t GetPose() = 0;

    ~IVRDevice() = default;
};

} // namespace SlimeVRDriver
