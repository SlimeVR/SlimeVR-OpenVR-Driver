#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <linalg.h>

#include "IVRDevice.hpp"
#include <openvr_driver.h>

#include "Logger.hpp"
#include <solarxr_protocol/generated/all_generated.h>

namespace SlimeVRDriver {

class TrackerDevice : public IVRDevice {
public:
    TrackerDevice(std::string serial, solarxr_protocol::datatypes::BodyPart body_part);
    ~TrackerDevice() = default;

    // Inherited via IVRDevice
    virtual solarxr_protocol::datatypes::BodyPart GetBodyPart() override;
    virtual std::string GetSerial() override;
    virtual void Update() override;
    virtual vr::TrackedDeviceIndex_t GetDeviceIndex() override;
    virtual DeviceType GetDeviceType() override;
    virtual void UpdatePose(linalg::vec<float, 4>&& rot, linalg::vec<float, 3>&& pos) override;
    virtual void UpdateStatus(solarxr_protocol::datatypes::TrackerStatus status) override;
    virtual void UpdateBattery(float battery_percentage, bool charging) override;

    // Inherited via ITrackedDeviceServerDriver
    virtual vr::EVRInitError Activate(uint32_t unObjectId) override;
    virtual void Deactivate() override;
    virtual void EnterStandby() override;
    virtual void* GetComponent(const char* pchComponentNameAndVersion) override;
    virtual void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;
    virtual vr::DriverPose_t GetPose() override;

private:
    std::shared_ptr<VRLogger> logger_ = std::make_shared<VRLogger>();

    std::atomic<vr::TrackedDeviceIndex_t> device_index_ = vr::k_unTrackedDeviceIndexInvalid;
    std::string serial_;

    solarxr_protocol::datatypes::BodyPart body_part_;

    vr::DriverPose_t last_pose_ = IVRDevice::MakeDefaultPose();

    bool did_vibrate_ = false;
    float vibrate_anim_state_ = 0.f;

    vr::VRInputComponentHandle_t haptic_component_ = 0;
    vr::VRInputComponentHandle_t system_click_component_ = 0;
    vr::VRInputComponentHandle_t system_touch_component_ = 0;
};

} // namespace SlimeVRDriver
