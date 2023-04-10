#pragma once

#include <chrono>
#include <cmath>

#include <linalg.h>

#include <IVRDevice.hpp>
#include <DriverFactory.hpp>

#include <thread>
#include <sstream>
#include <iostream>
#include <string>
#include "bridge/bridge.hpp"
#include "TrackerRole.hpp"

namespace SlimeVRDriver {
    class TrackerDevice : public IVRDevice {
        public:

            TrackerDevice(std::string serial, int deviceId, TrackerRole trackerRole);
            ~TrackerDevice() = default;

            virtual void BatteryMessage(messages::Battery &battery);

            // Inherited via IVRDevice
            virtual std::string GetSerial() override;
            virtual void Update() override;
            virtual vr::TrackedDeviceIndex_t GetDeviceIndex() override;
            virtual DeviceType GetDeviceType() override;

            virtual vr::EVRInitError Activate(uint32_t unObjectId) override;
            virtual void Deactivate() override;
            virtual void EnterStandby() override;
            virtual void* GetComponent(const char* pchComponentNameAndVersion) override;
            virtual void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;
            virtual vr::DriverPose_t GetPose() override;
            virtual int getDeviceId() override;
            virtual void PositionMessage(messages::Position &position) override;
            virtual void StatusMessage(messages::TrackerStatus &status) override;
    private:
        vr::TrackedDeviceIndex_t device_index_ = vr::k_unTrackedDeviceIndexInvalid;
        std::string serial_;
        bool isSetup;

        char buffer[1024];
        uint32_t dwWritten;
        uint32_t dwRead;
		int deviceId_;
        TrackerRole trackerRole;

        vr::DriverPose_t last_pose_ = IVRDevice::MakeDefaultPose();

        bool did_vibrate_ = false;
        float vibrate_anim_state_ = 0.f;

        vr::VRInputComponentHandle_t haptic_component_ = 0;

        vr::VRInputComponentHandle_t system_click_component_ = 0;
        vr::VRInputComponentHandle_t system_touch_component_ = 0;

    };
};