#pragma once
#define NOMINMAX

#include <memory>
#include <optional>
#include <vector>

#include <openvr_driver.h>

#include <IVRDevice.hpp>
#include <IVRDriver.hpp>

#include <simdjson.h>

#include "Logger.hpp"
#include "bridge/BridgeClient.hpp"

namespace SlimeVRDriver {
    class VRDriver : public IVRDriver {
    public:
        // Inherited via IVRDriver
        virtual std::vector<std::shared_ptr<IVRDevice>> GetDevices() override;
        virtual const std::vector<vr::VREvent_t>& GetOpenVREvents() override;
        virtual std::chrono::milliseconds GetLastFrameTime() override;
        virtual bool AddDevice(std::shared_ptr<IVRDevice> device) override;
        virtual SettingsValue GetSettingsValue(std::string key) override;

  virtual vr::IVRDriverInput *GetInput() override;
  virtual vr::CVRPropertyHelpers *GetProperties() override;
  virtual vr::IVRServerDriverHost *GetDriverHost() override;

  // Inherited via IServerTrackedDeviceProvider
  virtual vr::EVRInitError Init(vr::IVRDriverContext *pDriverContext) override;
  virtual void Cleanup() override;
  virtual void RunFrame() override;
  virtual bool ShouldBlockStandbyMode() override;
  virtual void EnterStandby() override;
  virtual void LeaveStandby() override;
  virtual ~VRDriver() = default;

  virtual std::optional<UniverseTranslation> GetCurrentUniverse() override;

  virtual std::optional<vr::DriverPose_t>
  GetExternalPoseForHand(bool left_hand) override;

  void OnBridgeMessage(const messages::ProtobufMessage &message);
  void RunPoseRequestThread();

private:
  std::unique_ptr<std::thread> pose_request_thread_ = nullptr;
  std::atomic<bool> exiting_pose_request_thread_ = false;

  std::shared_ptr<BridgeClient> bridge_ = nullptr;
  google::protobuf::Arena arena_;
  std::shared_ptr<VRLogger> logger_ = std::make_shared<VRLogger>();
  std::mutex devices_mutex_;
  std::vector<std::shared_ptr<IVRDevice>> devices_;
  std::vector<vr::VREvent_t> openvr_events_;
  std::map<int, std::shared_ptr<IVRDevice>> devices_by_id_;
  std::map<std::string, std::shared_ptr<IVRDevice>> devices_by_serial_;
  std::chrono::milliseconds frame_timing_ = std::chrono::milliseconds(16);
  std::chrono::steady_clock::time_point last_frame_time_ =
      std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point battery_sent_at_ =
      std::chrono::steady_clock::now();
  std::string settings_key_ = "driver_slimevr";

  static vr::HmdQuaternion_t GetRotation(vr::HmdMatrix34_t &matrix);
  static vr::HmdVector3_t GetPosition(vr::HmdMatrix34_t &matrix);

  bool sent_hmd_add_message_ = false;

  simdjson::ondemand::parser json_parser_;
  std::optional<std::string> default_chap_path_ = std::nullopt;
  // std::map<int, UniverseTranslation> universes;

  vr::ETrackedPropertyError last_universe_error_;
  std::optional<std::pair<uint64_t, UniverseTranslation>> current_universe_ =
      std::nullopt;
  std::optional<UniverseTranslation> SearchUniverse(std::string path,
                                                    uint64_t target);
  std::optional<UniverseTranslation> SearchUniverses(uint64_t target);

  std::optional<vr::DriverPose_t> external_left_pose_;
  std::optional<vr::DriverPose_t> external_right_pose_;
  std::optional<vr::DriverPose_t> last_external_left_pose_;
  std::optional<vr::DriverPose_t> last_external_right_pose_;
  int stale_external_left_frames_ = 0;
  int stale_external_right_frames_ = 0;
  static constexpr int kStaleExternalPoseFrames = 30; // ~0.5s at 60 Hz
  void UpdateExternalControllerPoses();
  static vr::DriverPose_t
  DriverPoseFromTrackedDevicePose(const vr::TrackedDevicePose_t &raw);
  static bool ExternalPoseEquals(const vr::DriverPose_t &a,
                                 const vr::DriverPose_t &b);
};
}; // namespace SlimeVRDriver
