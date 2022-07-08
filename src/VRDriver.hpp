#pragma once

#define NOMINMAX

#include <vector>
#include <memory>
#include <map>

#include <openvr_driver.h>

#include <IVRDriver.hpp>
#include <IVRDevice.hpp>

namespace SlimeVRDriver {
    class Bridge;
    class VRDriver : public IVRDriver {
    public:
        VRDriver();
        virtual ~VRDriver();

        // Inherited via IVRDriver
        virtual std::vector<std::shared_ptr<IVRDevice>> GetDevices() override;
        virtual std::vector<vr::VREvent_t> GetOpenVREvents() override;
        virtual std::chrono::milliseconds GetLastFrameTime() override;
        virtual bool AddDevice(std::shared_ptr<IVRDevice> device) override;
        virtual SettingsValue GetSettingsValue(std::string key) override;
        virtual void Log(std::string message) override;

        virtual vr::IVRDriverInput* GetInput() override;
        virtual vr::CVRPropertyHelpers* GetProperties() override;
        virtual vr::IVRServerDriverHost* GetDriverHost() override;

        // Inherited via IServerTrackedDeviceProvider
        virtual vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
        virtual void Cleanup() override;
        virtual void RunFrame() override;
        virtual bool ShouldBlockStandbyMode() override;
        virtual void EnterStandby() override;
        virtual void LeaveStandby() override;

    private:
        std::vector<std::shared_ptr<IVRDevice>> devices_;
        std::vector<vr::VREvent_t> openvr_events_;
        std::map<int, std::shared_ptr<IVRDevice>> devices_by_id;
        std::map<std::string, std::shared_ptr<IVRDevice>> devices_by_serial;
        std::chrono::milliseconds frame_timing_ = std::chrono::milliseconds(16);
        std::chrono::system_clock::time_point last_frame_time_ = std::chrono::system_clock::now();
        std::string settings_key_ = "driver_slimevr";

        vr::HmdQuaternion_t GetRotation(vr::HmdMatrix34_t &matrix);
        vr::HmdVector3_t GetPosition(vr::HmdMatrix34_t &matrix);

        bool sentHmdAddMessage = false;
        Bridge *m_pBridge;
    };
};