#pragma once

#include <chrono>
#include <cmath>

#include <linalg.h>

#include <IVRDevice.hpp>
#include <DriverFactory.hpp>

namespace SlimeVRDriver {
    class TrackingReferenceDevice : public IVRDevice {
        public:

            TrackingReferenceDevice(std::string serial);
            ~TrackingReferenceDevice() = default;

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
            virtual void PositionMessage(messages::Position &position) override;

    private:
        vr::TrackedDeviceIndex_t device_index_ = vr::k_unTrackedDeviceIndexInvalid;
        std::string serial_;

        vr::DriverPose_t last_pose_ = IVRDevice::MakeDefaultPose();

        float random_angle_rad_;

    };
};