#pragma once
#define NOMINMAX

#include <solarxr_protocol/generated/all_generated.h>

#include <openvr_driver.h>
#include <simdjson.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "IVRDevice.hpp"
#include "Logger.hpp"
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

struct TrackerIdT {
    TrackerIdT(const solarxr_protocol::datatypes::TrackerId* id)
        : tracker_num(id->tracker_num()) {
        if (auto new_id = id->device_id()) {
            device_id.emplace(new_id->id());
        }
    }
    std::optional<solarxr_protocol::datatypes::DeviceId> device_id;
    uint8_t tracker_num;
};

struct DeviceData {
    vr::TrackedDeviceIndex_t index{ vr::k_unTrackedDeviceIndexInvalid };
    solarxr_protocol::datatypes::BodyPart role{ solarxr_protocol::datatypes::BodyPart::NONE };
    bool sent_add_message{ false };
    std::mutex id_mutex;
    std::optional<TrackerIdT> id{};

    solarxr_protocol::datatypes::TrackerStatus status{ solarxr_protocol::datatypes::TrackerStatus::DISCONNECTED };
    float last_battery_percentage{ -1.f };
    std::chrono::steady_clock::time_point battery_sent_at{};
};

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

private:
    std::jthread pose_request_thread_;
    void RunPoseRequestThread(std::stop_token stop);
    void OnBridgeConnect();
    void OnBridgeMessage(std::variant<const solarxr_protocol::data_feed::DataFeedMessageHeader*, const solarxr_protocol::rpc::RpcMessageHeader*>&& message);

    solarxr_protocol::datatypes::BodyPart GetRoleForDevice(vr::TrackedDeviceIndex_t index) const;
    uint64_t body_part_mask_;

    std::shared_ptr<BridgeClient> bridge_ = nullptr;
    std::shared_ptr<VRLogger> logger_ = std::make_shared<VRLogger>();
    std::mutex devices_mutex_;
    std::vector<std::shared_ptr<IVRDevice>> devices_;
    std::vector<vr::VREvent_t> openvr_events_;
    std::map<solarxr_protocol::datatypes::BodyPart, std::shared_ptr<IVRDevice>> devices_by_role_;
    std::chrono::milliseconds frame_timing_ = std::chrono::milliseconds(16);
    std::chrono::steady_clock::time_point last_frame_time_ = std::chrono::steady_clock::now();
    std::string settings_key_ = "driver_slimevr";

    std::array<DeviceData, vr::k_unMaxTrackedDeviceCount> feeder_devices_{};

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
