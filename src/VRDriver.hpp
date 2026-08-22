#pragma once
#define NOMINMAX

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <openvr_driver.h>

#include <IVRDevice.hpp>

#include <simdjson.h>

#include "Logger.hpp"
#include "TrackerRole.hpp"
#include "bridge/BridgeClient.hpp"

namespace SlimeVRDriver {

class UniverseTranslation {
public:
    // TODO: do we want to store this differently?
    vr::HmdVector3_t translation;
    float yaw;

    static UniverseTranslation parse(simdjson::ondemand::object& obj);
};

typedef std::variant<std::monostate, std::string, int, float, bool> SettingsValue;

class VRDriver : protected vr::IServerTrackedDeviceProvider {
public:
    // Inherited via IServerTrackedDeviceProvider
    virtual vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
    virtual void Cleanup() override;
    virtual const char* const* GetInterfaceVersions() override;
    virtual void RunFrame() override;
    virtual bool ShouldBlockStandbyMode() override;
    virtual void EnterStandby() override;
    virtual void LeaveStandby() override;
    virtual ~VRDriver() = default;

    std::vector<std::shared_ptr<IVRDevice>> GetDevices();
    const std::vector<vr::VREvent_t>& GetOpenVREvents();
    std::chrono::milliseconds GetLastFrameTime();
    bool AddDevice(std::shared_ptr<IVRDevice> device);
    SettingsValue GetSettingsValue(std::string key);

    std::optional<UniverseTranslation> GetCurrentUniverse();

    void OnBridgeConnect();
    void OnBridgeMessage(const messages::ProtobufMessage& message);

private:
    // set to true if initialisation is done, or we're exiting
    // if we're exiting, this will be true and stop tokens will be signaled
    std::atomic<bool> steamvr_init_guard_ = false;

    std::jthread pose_request_thread_;
    void RunPoseRequestThread(std::stop_token stop);

    TrackerRole GetRoleForDevice(vr::TrackedDeviceIndex_t index) const;

    std::shared_ptr<BridgeClient> bridge_ = nullptr;
    google::protobuf::Arena arena_;
    std::shared_ptr<VRLogger> logger_ = std::make_shared<VRLogger>();
    std::mutex devices_mutex_;
    std::vector<std::shared_ptr<IVRDevice>> devices_;
    std::vector<vr::VREvent_t> openvr_events_;
    std::map<int, std::shared_ptr<IVRDevice>> devices_by_id_;
    std::map<std::string, std::shared_ptr<IVRDevice>> devices_by_serial_;
    std::chrono::milliseconds frame_timing_ = std::chrono::milliseconds(16);
    std::chrono::steady_clock::time_point last_frame_time_ = std::chrono::steady_clock::now();
    std::string settings_key_ = "driver_slimevr";

    vr::HmdQuaternion_t GetRotation(vr::HmdMatrix34_t& matrix);
    vr::HmdVector3_t GetPosition(vr::HmdMatrix34_t& matrix);

    bool sent_hmd_add_message_ = false;

    simdjson::ondemand::parser json_parser_;
    std::optional<std::filesystem::path> default_chap_path_ = std::nullopt;
    // std::map<int, UniverseTranslation> universes;

    vr::ETrackedPropertyError last_universe_error_;
    std::optional<std::pair<uint64_t, UniverseTranslation>> current_universe_ = std::nullopt;
    std::optional<UniverseTranslation> SearchUniverse(const simdjson::padded_string& json, uint64_t target);
    std::optional<UniverseTranslation> SearchUniverses(uint64_t target);
};

} // namespace SlimeVRDriver
