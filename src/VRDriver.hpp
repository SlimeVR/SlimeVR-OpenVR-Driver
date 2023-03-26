#pragma once
#define NOMINMAX

#include <vector>
#include <memory>
#include <optional>

#include <openvr_driver.h>

#include <IVRDriver.hpp>
#include <IVRDevice.hpp>

#include <simdjson.h>

#include "bridge/BridgeClient.hpp"
#include "Logger.hpp"

namespace SlimeVRDriver {
    class VRDriver : public IVRDriver {
    public:

        // Inherited via IVRDriver
        virtual std::vector<std::shared_ptr<IVRDevice>> GetDevices() override;
        virtual std::vector<vr::VREvent_t> GetOpenVREvents() override;
        virtual std::chrono::milliseconds GetLastFrameTime() override;
        virtual bool AddDevice(std::shared_ptr<IVRDevice> device) override;
        virtual SettingsValue GetSettingsValue(std::string key) override;
        virtual void Log(std::string message) override {
            logger->Log("%s", message.c_str());
        };

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
        virtual ~VRDriver() = default;

        void OnBridgeMessage(const messages::ProtobufMessage& message);

        virtual std::optional<UniverseTranslation> GetCurrentUniverse() override;

    private:
        std::shared_ptr<VRLogger> logger = std::make_shared<VRLogger>();
        std::mutex devices_mutex_;
        std::vector<std::shared_ptr<IVRDevice>> devices_;
        std::vector<vr::VREvent_t> openvr_events_;
        std::map<int, std::shared_ptr<IVRDevice>> devices_by_id;
        std::map<std::string, std::shared_ptr<IVRDevice>> devices_by_serial;
        std::chrono::milliseconds frame_timing_ = std::chrono::milliseconds(16);
        std::chrono::steady_clock::time_point last_frame_time_ = std::chrono::steady_clock::now();
        std::string settings_key_ = "driver_slimevr";

        vr::HmdQuaternion_t GetRotation(vr::HmdMatrix34_t &matrix);
        vr::HmdVector3_t GetPosition(vr::HmdMatrix34_t &matrix);

        bool sentHmdAddMessage = false;

        simdjson::ondemand::parser json_parser;
        std::optional<std::string> default_chap_path_ = std::nullopt;
        //std::map<int, UniverseTranslation> universes;

        std::optional<std::pair<uint64_t, UniverseTranslation>> current_universe = std::nullopt;

        std::optional<UniverseTranslation> search_universe(std::string path, uint64_t target);
        std::optional<UniverseTranslation> search_universes(uint64_t target);

        std::shared_ptr<BridgeClient> bridge;
    };
};