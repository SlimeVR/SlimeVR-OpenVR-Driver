#pragma once
#define NOMINMAX

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <openvr_driver.h>

#include <IVRDevice.hpp>

#include <simdjson.h>
#include <solarxr_protocol/generated/all_generated.h>

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

struct DeviceData {
    vr::TrackedDeviceIndex_t index{ vr::k_unTrackedDeviceIndexInvalid };
    solarxr_protocol::datatypes::BodyPart role{ solarxr_protocol::datatypes::BodyPart::NONE };
    bool sent_add_message{ false };
    std::atomic_uint16_t tracker_id{};

    solarxr_protocol::datatypes::TrackerStatus status{ solarxr_protocol::datatypes::TrackerStatus::DISCONNECTED };
    float last_battery_percentage{ -1.f };
    std::chrono::steady_clock::time_point battery_sent_at{};
};

struct BatteryInfo {
    uint8_t level;
    bool charging;
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
    // set to true if initialisation is done, or we're exiting
    // if we're exiting, this will be true and stop tokens will be signaled
    std::atomic<bool> steamvr_init_guard_ = false;
    // set to true when we have a valid driver connection (handshake was successful), or we're exiting, see above
    std::atomic<bool> driver_connection_active_ = false;
    std::stop_source stop_source_;

    std::jthread pose_request_thread_;
    void RunPoseRequestThread(std::stop_token stop);

    uint64_t body_part_mask_ = 0;
    void OnBridgeMessage(const solarxr_protocol::data_feed::DataFeedMessageHeader* msg);
    void OnBridgeMessage(const solarxr_protocol::rpc::RpcMessageHeader* msg);
    void OnBridgeMessage(const solarxr_protocol::driver_protocol::DriverMessageHeader* msg);
    void OnBridgeMessage(BridgeTransport::MessageHeader&& message);

    solarxr_protocol::datatypes::BodyPart GetRoleForDevice(vr::TrackedDeviceIndex_t index) const;

    std::shared_ptr<BridgeClient> bridge_ = nullptr;
    std::shared_ptr<VRLogger> logger_ = std::make_shared<VRLogger>();
    std::mutex devices_mutex_;
    std::vector<std::shared_ptr<IVRDevice>> devices_;
    std::vector<vr::VREvent_t> openvr_events_;
    std::unordered_map<solarxr_protocol::datatypes::BodyPart, std::shared_ptr<IVRDevice>> devices_by_role_;
    std::chrono::milliseconds frame_timing_ = std::chrono::milliseconds(16);
    std::chrono::steady_clock::time_point last_frame_time_ = std::chrono::steady_clock::now();
    std::string settings_key_ = "driver_slimevr";

    // Server may send us BoneBatteryUpdate before we create a device, we store them in this map for later when adding devices.
    std::unordered_map<solarxr_protocol::datatypes::BodyPart, BatteryInfo> queued_bone_battery_{};
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
