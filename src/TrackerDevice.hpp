#pragma once

#include <chrono>
#include <cmath>
#include <atomic>

#include <linalg.h>

#include <IVRDevice.hpp>
#include <DriverFactory.hpp>

#include <thread>
#include <sstream>
#include <iostream>
#include <string>
#include "TrackerRole.hpp"
#include "Logger.hpp"

namespace SlimeVRDriver {
    class TrackerDevice : public IVRDevice {
    public:
        TrackerDevice(std::string serial, int device_id, TrackerRole tracker_role);
        ~TrackerDevice() = default;

        // Inherited via IVRDevice
        virtual std::string GetSerial() override;
        virtual void Update() override;
        virtual vr::TrackedDeviceIndex_t GetDeviceIndex() override;
        virtual DeviceType GetDeviceType() override;
        virtual int GetDeviceId() override;
        virtual void SetDeviceId(int device_id) override;
        virtual void PositionMessage(messages::Position &position) override;
        virtual void ControllerInputMessage(messages::ControllerInput& position) override;
        virtual void StatusMessage(messages::TrackerStatus &status) override;
        virtual void BatteryMessage(messages::Battery &battery) override;

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

        int device_id_;
        TrackerRole tracker_role_;
        bool fingertracking_enabled_;

        vr::DriverPose_t last_pose_ = IVRDevice::MakeDefaultPose();
        std::atomic<vr::DriverPose_t> last_pose_atomic_ = IVRDevice::MakeDefaultPose();

        bool did_vibrate_ = false;
        float vibrate_anim_state_ = 0.f;

        vr::VRInputComponentHandle_t haptic_component_ = 0;
        vr::VRInputComponentHandle_t double_tap_component_ = 0;
        vr::VRInputComponentHandle_t triple_tap_component_ = 0;

        vr::VRInputComponentHandle_t ignored = 0;

        vr::VRInputComponentHandle_t left_trigger_component_ = 0;
        vr::VRInputComponentHandle_t left_grip_value_component_ = 0;
        vr::VRInputComponentHandle_t left_stick_x_component_ = 0;
        vr::VRInputComponentHandle_t left_stick_y_component_ = 0;
        vr::VRInputComponentHandle_t button_x_component_ = 0;
        vr::VRInputComponentHandle_t button_y_component_ = 0;
        vr::VRInputComponentHandle_t left_stick_click_component_ = 0;
        vr::VRInputComponentHandle_t menu_component_ = 0;

        vr::VRInputComponentHandle_t left_trigger_component_touch_ = 0;
        vr::VRInputComponentHandle_t left_grip_value_component_touch_ = 0;
        vr::VRInputComponentHandle_t left_stick_x_component_touch_ = 0;
        vr::VRInputComponentHandle_t left_stick_y_component_touch_ = 0;
        vr::VRInputComponentHandle_t button_x_component_touch_ = 0;
        vr::VRInputComponentHandle_t button_y_component_touch_ = 0;
        vr::VRInputComponentHandle_t left_stick_click_component_touch_ = 0;
        vr::VRInputComponentHandle_t menu_component_touch_ = 0;

        vr::VRInputComponentHandle_t right_trigger_component_ = 0;
        vr::VRInputComponentHandle_t right_grip_value_component_ = 0;
        vr::VRInputComponentHandle_t right_stick_x_component_ = 0;
        vr::VRInputComponentHandle_t right_stick_y_component_ = 0;
        vr::VRInputComponentHandle_t button_a_component_ = 0;
        vr::VRInputComponentHandle_t button_b_component_ = 0;
        vr::VRInputComponentHandle_t right_stick_click_component_ = 0;
        vr::VRInputComponentHandle_t recenter_component_ = 0;

        vr::VRInputComponentHandle_t right_trigger_component_touch_ = 0;
        vr::VRInputComponentHandle_t right_grip_value_component_touch_ = 0;
        vr::VRInputComponentHandle_t right_stick_x_component_touch_ = 0;
        vr::VRInputComponentHandle_t right_stick_y_component_touch_ = 0;
        vr::VRInputComponentHandle_t button_a_component_touch_ = 0;
        vr::VRInputComponentHandle_t button_b_component_touch_ = 0;
        vr::VRInputComponentHandle_t right_stick_click_component_touch_ = 0;
        vr::VRInputComponentHandle_t recenter_component_touch_ = 0;

        vr::VRInputComponentHandle_t haptic_component_ = 0;

        bool is_controller_;
        bool is_left_hand_;
        bool is_right_hand_;

        vr::VRInputComponentHandle_t skeletal_component_handle_;

        const int protobuf_fingers_to_openvr[15] = {
            2,  // THUMB_METACARPAL      → eBone_Thumb1
            3,  // THUMB_PROXIMAL        → eBone_Thumb2
            4,  // THUMB_DISTAL          → eBone_Thumb3
            6,  // INDEX_PROXIMAL        → eBone_IndexFinger1
            7,  // INDEX_INTERMEDIATE    → eBone_IndexFinger2
            8,  // INDEX_DISTAL          → eBone_IndexFinger3
            11, // MIDDLE_PROXIMAL       → eBone_MiddleFinger1
            12, // MIDDLE_INTERMEDIATE   → eBone_MiddleFinger2
            13, // MIDDLE_DISTAL         → eBone_MiddleFinger3
            16, // RING_PROXIMAL         → eBone_RingFinger1
            17, // RING_INTERMEDIATE     → eBone_RingFinger2
            18, // RING_DISTAL           → eBone_RingFinger3
            21, // LITTLE_PROXIMAL       → eBone_PinkyFinger1
            22, // LITTLE_INTERMEDIATE   → eBone_PinkyFinger2
            23  // LITTLE_DISTAL         → eBone_PinkyFinger3
        };
    };
};