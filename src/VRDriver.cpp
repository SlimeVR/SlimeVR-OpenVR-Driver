#include "VRDriver.hpp"
#include "TrackerRole.hpp"
#include "VRPaths_openvr.hpp"
#include <TrackerDevice.hpp>
#include <cmath>
#include <cstring>
#include <google/protobuf/arena.h>
#include <simdjson.h>
#include <unordered_set>

vr::EVRInitError
SlimeVRDriver::VRDriver::Init(vr::IVRDriverContext *pDriverContext) {
  // Perform driver context initialisation
  if (vr::EVRInitError init_error = vr::InitServerDriverContext(pDriverContext);
      init_error != vr::EVRInitError::VRInitError_None) {
    return init_error;
  }

  logger_->Log("Activating SlimeVR Driver...");

  try {
    auto json = simdjson::padded_string::load(
        GetVRPathRegistryFilename()); // load VR Path Registry
    simdjson::ondemand::document doc = json_parser_.iterate(json);
    auto path =
        std::string{doc.get_object()["config"].at(0).get_string().value()};
    default_chap_path_ = GetDefaultChaperoneFromConfigPath(path);
  } catch (simdjson::simdjson_error &e) {
    logger_->Log("Error getting VR Config path, continuing (error code {})",
                 std::to_string(e.error()));
  }

  logger_->Log("SlimeVR Driver Loaded Successfully");

  LoadDriverConfig();

  bridge_ = std::make_shared<BridgeClient>(
      std::static_pointer_cast<Logger>(std::make_shared<VRLogger>("Bridge")),
      std::bind(&SlimeVRDriver::VRDriver::OnBridgeMessage, this,
                std::placeholders::_1));
  bridge_->Start();

  exiting_pose_request_thread_ = false;
  pose_request_thread_ = std::make_unique<std::thread>(
      &SlimeVRDriver::VRDriver::RunPoseRequestThread, this);

  return vr::VRInitError_None;
}

void SlimeVRDriver::VRDriver::Cleanup() {
  exiting_pose_request_thread_ = true;
  pose_request_thread_->join();
  pose_request_thread_.reset();
  bridge_->Stop();
}

void SlimeVRDriver::VRDriver::RunPoseRequestThread() {
    logger_->Log("Pose request thread started");
    while (!exiting_pose_request_thread_) {
        if (!bridge_->IsConnected()) {
            // If bridge not connected, assume we need to resend hmd tracker add message
            sent_hmd_add_message_ = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        messages::ProtobufMessage* message = google::protobuf::Arena::Create<messages::ProtobufMessage>(&arena_);

        vr::TrackedDevicePose_t hmd_pose;
        vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.0f, &hmd_pose, 1);

        vr::PropertyContainerHandle_t hmd_prop_container =
            vr::VRProperties()->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd);

        if (!sent_hmd_add_message_ && hmd_pose.bDeviceIsConnected) {
            vr::ETrackedPropertyError error{};
            auto serial = vr::VRProperties()->GetStringProperty(hmd_prop_container, vr::Prop_SerialNumber_String, &error);
            if (error != vr::ETrackedPropertyError::TrackedProp_Success) {
                logger_->Log("Failed to get HMD's Prop_SerialNumber_String: {}", vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(error));
            }

            auto name = vr::VRProperties()->GetStringProperty(hmd_prop_container, vr::Prop_ModelNumber_String, &error);
            if (error != vr::ETrackedPropertyError::TrackedProp_Success) {
                logger_->Log("Failed to get HMD's Prop_ModelNumber_String: {}", vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(error));
            }

            auto manufacturer = vr::VRProperties()->GetStringProperty(hmd_prop_container, vr::Prop_ManufacturerName_String, &error);
            if (error != vr::ETrackedPropertyError::TrackedProp_Success) {
                logger_->Log("Failed to get HMD's Prop_ManufacturerName_String: {}", vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(error));
            }

            logger_->Log("HMD props: serial='{}', model='{}', manufacturer='{}'", serial, name, manufacturer);

            // Send add message for HMD
            messages::TrackerAdded* tracker_added = google::protobuf::Arena::Create<messages::TrackerAdded>(&arena_);
            message->set_allocated_tracker_added(tracker_added);
            tracker_added->set_tracker_id(0);
            tracker_added->set_tracker_role(TrackerRole::HMD);
            tracker_added->set_tracker_serial(serial.empty() ? "HMD" : serial);
            tracker_added->set_tracker_name(name.empty() ? "HMD" : name);
            tracker_added->set_manufacturer(manufacturer.empty() ? "OpenVR" : manufacturer);
            bridge_->SendBridgeMessage(*message);

            messages::TrackerStatus* tracker_status = google::protobuf::Arena::Create<messages::TrackerStatus>(&arena_);
            message->set_allocated_tracker_status(tracker_status);
            tracker_status->set_tracker_id(0);
            tracker_status->set_status(messages::TrackerStatus_Status::TrackerStatus_Status_OK);
            bridge_->SendBridgeMessage(*message);

            sent_hmd_add_message_ = true;
            logger_->Log("Sent HMD hello message");
        }

        vr::ETrackedPropertyError universe_error;
        uint64_t universe = vr::VRProperties()->GetUint64Property(hmd_prop_container, vr::Prop_CurrentUniverseId_Uint64, &universe_error);
        if (universe_error == vr::ETrackedPropertyError::TrackedProp_Success) {
            if (!current_universe_.has_value() || current_universe_.value().first != universe) {
                auto result = SearchUniverses(universe);
                if (result.has_value()) {
                    current_universe_.emplace(universe, result.value());
                    logger_->Log("Found current universe");
                }
            }
        } else if (universe_error != last_universe_error_) {
            logger_->Log("Failed to find current universe: Prop_CurrentUniverseId_Uint64 error = {}",
                vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(universe_error)
            );
        }
        last_universe_error_ = universe_error;

        vr::HmdQuaternion_t q = GetRotation(hmd_pose.mDeviceToAbsoluteTracking);
        vr::HmdVector3_t pos = GetPosition(hmd_pose.mDeviceToAbsoluteTracking);

        if (current_universe_.has_value()) {
            auto trans = current_universe_.value().second;
            pos.v[0] += trans.translation.v[0];
            pos.v[1] += trans.translation.v[1];
            pos.v[2] += trans.translation.v[2];

            // rotate by quaternion w = cos(-trans.yaw / 2), x = 0, y = sin(-trans.yaw / 2), z = 0
            auto tmp_w = cos(-trans.yaw / 2);
            auto tmp_y = sin(-trans.yaw / 2);
            auto new_w = tmp_w * q.w - tmp_y * q.y;
            auto new_x = tmp_w * q.x + tmp_y * q.z;
            auto new_y = tmp_w * q.y + tmp_y * q.w;
            auto new_z = tmp_w * q.z - tmp_y * q.x;

            q.w = new_w;
            q.x = new_x;
            q.y = new_y;
            q.z = new_z;

            // rotate point on the xz plane by -trans.yaw radians
            // this is equivilant to the quaternion multiplication, after applying the double angle formula.
            float tmp_sin = sin(-trans.yaw);
            float tmp_cos = cos(-trans.yaw);
            auto pos_x = pos.v[0] * tmp_cos + pos.v[2] * tmp_sin;
            auto pos_z = pos.v[0] * -tmp_sin + pos.v[2] * tmp_cos;

            pos.v[0] = pos_x;
            pos.v[2] = pos_z;
        }

        messages::Position* hmd_position = google::protobuf::Arena::Create<messages::Position>(&arena_);
        message->set_allocated_position(hmd_position);
        hmd_position->set_tracker_id(0);
        hmd_position->set_data_source(messages::Position_DataSource_FULL);
        hmd_position->set_x(pos.v[0]);
        hmd_position->set_y(pos.v[1]);
        hmd_position->set_z(pos.v[2]);
        hmd_position->set_qx((float) q.x);
        hmd_position->set_qy((float) q.y);
        hmd_position->set_qz((float) q.z);
        hmd_position->set_qw((float) q.w);
        bridge_->SendBridgeMessage(*message);

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - battery_sent_at_).count() > 100) {
            vr::ETrackedPropertyError err;
            if (vr::VRProperties()->GetBoolProperty(hmd_prop_container, vr::Prop_DeviceProvidesBatteryStatus_Bool, &err)) {
                messages::Battery* hmdBattery = google::protobuf::Arena::Create<messages::Battery>(&arena_);
                message->set_allocated_battery(hmdBattery);
                hmdBattery->set_tracker_id(0);
                hmdBattery->set_battery_level(vr::VRProperties()->GetFloatProperty(hmd_prop_container, vr::Prop_DeviceBatteryPercentage_Float, &err) * 100);
                hmdBattery->set_is_charging(vr::VRProperties()->GetBoolProperty(hmd_prop_container, vr::Prop_DeviceIsCharging_Bool, &err));
                bridge_->SendBridgeMessage(*message);
            }
            battery_sent_at_ = now;
        }

        arena_.Reset();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    logger_->Log("Pose request thread exited");
}

void SlimeVRDriver::VRDriver::RunFrame() {
  // Collect events
  vr::VREvent_t event;
  std::vector<vr::VREvent_t> events;
  while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event))) {
    events.push_back(event);

    if (event.eventType == vr::EVREventType::VREvent_ButtonPress ||
        event.eventType == vr::EVREventType::VREvent_ButtonUnpress) {
      uint64_t mask = 1ULL << event.data.controller.button;
      if (event.trackedDeviceIndex != vr::k_unTrackedDeviceIndexInvalid) {
        if (event.trackedDeviceIndex == external_left_index_) {
          if (event.eventType == vr::EVREventType::VREvent_ButtonPress) external_left_buttons_ |= mask;
          else external_left_buttons_ &= ~mask;
        } else if (event.trackedDeviceIndex == external_right_index_) {
          if (event.eventType == vr::EVREventType::VREvent_ButtonPress) external_right_buttons_ |= mask;
          else external_right_buttons_ &= ~mask;
        }
      }
    }
  }
  openvr_events_ = std::move(events);

  // Update frame timing
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  frame_timing_ = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - last_frame_time_);
  last_frame_time_ = now;

  // Update external controller poses (e.g. Virtual Desktop / Steam Link on Quest) so we can prefer their hand position when in view and fall back to SlimeVR when not.
  {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    UpdateExternalControllerPoses();
  }

  // Update devices
  {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    for (auto &device : devices_) {
      device->Update();
    }
  }
}

void SlimeVRDriver::VRDriver::OnBridgeMessage(
    const messages::ProtobufMessage &message) {
  std::lock_guard<std::mutex> lock(devices_mutex_);
  if (message.has_tracker_added()) {
    messages::TrackerAdded ta = message.tracker_added();
    AddDevice(std::make_shared<TrackerDevice>(
        ta.tracker_serial(), ta.tracker_id(),
        static_cast<TrackerRole>(ta.tracker_role())));
  } else if (message.has_position()) {
    messages::Position pos = message.position();
    auto device = devices_by_id_.find(pos.tracker_id());
    if (device != devices_by_id_.end()) {
      device->second->PositionMessage(pos);
    }
  } else if (message.has_controller_input()) {
    messages::ControllerInput controllerInput = message.controller_input();
    auto device = devices_by_id_.find(controllerInput.tracker_id());
    if (device != devices_by_id_.end()) {
      device->second->ControllerInputMessage(controllerInput);
    }
  } else if (message.has_tracker_status()) {
    messages::TrackerStatus status = message.tracker_status();
    auto device = devices_by_id_.find(status.tracker_id());
    if (device != devices_by_id_.end()) {
      device->second->StatusMessage(status);
      static const std::unordered_map<messages::TrackerStatus_Status,
                                      std::string>
          status_map = {
              {messages::TrackerStatus_Status_OK, "OK"},
              {messages::TrackerStatus_Status_DISCONNECTED, "DISCONNECTED"},
              {messages::TrackerStatus_Status_ERROR, "ERROR"},
              {messages::TrackerStatus_Status_BUSY, "BUSY"},
          };
      if (status_map.count(status.status())) {
        logger_->Log("Tracker status id {} status {}", status.tracker_id(),
                     status_map.at(status.status()));
      }
    }
  } else if (message.has_battery()) {
    messages::Battery bat = message.battery();
    auto device = this->devices_by_id_.find(bat.tracker_id());
    if (device != this->devices_by_id_.end()) {
      device->second->BatteryMessage(bat);
    }
  }
}

bool SlimeVRDriver::VRDriver::ShouldBlockStandbyMode() { return false; }

void SlimeVRDriver::VRDriver::EnterStandby() {}

void SlimeVRDriver::VRDriver::LeaveStandby() {}

std::vector<std::shared_ptr<SlimeVRDriver::IVRDevice>>
SlimeVRDriver::VRDriver::GetDevices() {
  std::lock_guard<std::mutex> lock(devices_mutex_);
  std::vector<std::shared_ptr<SlimeVRDriver::IVRDevice>> devices;
  devices.assign(devices.begin(), devices.end());
  return devices;
}

const std::vector<vr::VREvent_t>& SlimeVRDriver::VRDriver::GetOpenVREvents() {
    return openvr_events_;
}

std::chrono::milliseconds SlimeVRDriver::VRDriver::GetLastFrameTime() {
  return frame_timing_;
}

bool SlimeVRDriver::VRDriver::AddDevice(std::shared_ptr<IVRDevice> device) {
  vr::ETrackedDeviceClass openvr_device_class;
  // Remember to update this switch when new device types are added
  switch (device->GetDeviceType()) {
  case DeviceType::CONTROLLER:
    openvr_device_class =
        vr::ETrackedDeviceClass::TrackedDeviceClass_Controller;
    break;
  case DeviceType::HMD:
    openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_HMD;
    break;
  case DeviceType::TRACKER:
    openvr_device_class =
        vr::ETrackedDeviceClass::TrackedDeviceClass_GenericTracker;
    break;
  case DeviceType::TRACKING_REFERENCE:
    openvr_device_class =
        vr::ETrackedDeviceClass::TrackedDeviceClass_TrackingReference;
    break;
  default:
    return false;
  }
  if (!devices_by_serial_.count(device->GetSerial())) {
    bool result = vr::VRServerDriverHost()->TrackedDeviceAdded(
        device->GetSerial().c_str(), openvr_device_class, device.get());
    if (result) {
      devices_.push_back(device);
      devices_by_id_[device->GetDeviceId()] = device;
      devices_by_serial_[device->GetSerial()] = device;
      logger_->Log("New tracker device added {} (id {})", device->GetSerial(),
                   device->GetDeviceId());
    } else {
      logger_->Log("Failed to add tracker device {} (id {})",
                   device->GetSerial(), device->GetDeviceId());
      return false;
    }
  } else {
    std::shared_ptr<IVRDevice> oldDevice =
        devices_by_serial_[device->GetSerial()];
    if (oldDevice->GetDeviceId() != device->GetDeviceId()) {
      devices_by_id_[device->GetDeviceId()] = oldDevice;
      oldDevice->SetDeviceId(device->GetDeviceId());
      logger_->Log("Device overridden from id {} to {} for serial {}",
                   oldDevice->GetDeviceId(), device->GetDeviceId(),
                   device->GetSerial());
    } else {
      logger_->Log("Device readded id {}, serial {}", device->GetDeviceId(),
                   device->GetSerial());
    }
  }
  return true;
}

void SlimeVRDriver::VRDriver::LoadDriverConfig() {
  vr::IVRResources *res = vr::VRResources();
  if (!res) return;
  char path[1024];
  uint32_t len = res->GetResourceFullPath(
      "slimevr_driver_config.json", "", path, sizeof(path));
  if (len == 0 || len >= sizeof(path)) return;
  path[len] = '\0';
  try {
    auto json = simdjson::padded_string::load(path);
    simdjson::ondemand::document doc = json_parser_.iterate(json);
    auto obj = doc.get_object();
    if (auto v = obj["external_hand_max_radius_m"]; !v.error()) config_external_hand_max_radius_m_ = static_cast<float>(v.get_double());
    if (auto v = obj["stale_external_pose_frames"]; !v.error()) config_stale_external_pose_frames_ = static_cast<int>(v.get_int64());
    if (auto v = obj["pose_lerp_speed"]; !v.error()) config_pose_lerp_speed_ = static_cast<float>(v.get_double());
    if (auto v = obj["pose_lerp_speed_on_swap"]; !v.error()) config_pose_lerp_speed_on_swap_ = static_cast<float>(v.get_double());
    if (auto v = obj["frozen_pose_position_epsilon_m"]; !v.error()) config_frozen_pose_position_epsilon_m_ = static_cast<float>(v.get_double());
    if (auto v = obj["controller_priority"]; !v.error()) config_controller_priority_ = static_cast<int>(v.get_int64());
	if (auto v = obj["input_passthrough"]; !v.error()) {
		bool val = false;
		if (v.get_bool().get(val) == simdjson::SUCCESS) {
			config_input_passthrough_ = val;
		}
	}
    logger_->Log("Loaded driver config from {}", path);
  } catch (const simdjson::simdjson_error &) {
    // Use defaults; config file missing or invalid
  }
}

float SlimeVRDriver::VRDriver::GetPoseLerpSpeed() {
  return config_pose_lerp_speed_;
}

float SlimeVRDriver::VRDriver::GetPoseLerpSpeedOnSwap() {
  return config_pose_lerp_speed_on_swap_;
}

int SlimeVRDriver::VRDriver::GetControllerPriority() {
  return config_controller_priority_;
}

bool SlimeVRDriver::VRDriver::GetInputPassthrough() {
  return config_input_passthrough_;
}

SlimeVRDriver::SettingsValue
SlimeVRDriver::VRDriver::GetSettingsValue(std::string key) {
  vr::EVRSettingsError err = vr::EVRSettingsError::VRSettingsError_None;
  int int_value =
      vr::VRSettings()->GetInt32(settings_key_.c_str(), key.c_str(), &err);
  if (err == vr::EVRSettingsError::VRSettingsError_None) {
    return int_value;
  }
  err = vr::EVRSettingsError::VRSettingsError_None;
  float float_value =
      vr::VRSettings()->GetFloat(settings_key_.c_str(), key.c_str(), &err);
  if (err == vr::EVRSettingsError::VRSettingsError_None) {
    return float_value;
  }
  err = vr::EVRSettingsError::VRSettingsError_None;
  bool bool_value =
      vr::VRSettings()->GetBool(settings_key_.c_str(), key.c_str(), &err);
  if (err == vr::EVRSettingsError::VRSettingsError_None) {
    return bool_value;
  }
  std::string str_value;
  str_value.reserve(1024);
  vr::VRSettings()->GetString(settings_key_.c_str(), key.c_str(),
                              str_value.data(), 1024, &err);
  if (err == vr::EVRSettingsError::VRSettingsError_None) {
    return str_value;
  }
  err = vr::EVRSettingsError::VRSettingsError_None;

  return SettingsValue();
}

vr::IVRDriverInput *SlimeVRDriver::VRDriver::GetInput() {
  return vr::VRDriverInput();
}

vr::CVRPropertyHelpers *SlimeVRDriver::VRDriver::GetProperties() {
  return vr::VRProperties();
}

vr::IVRServerDriverHost *SlimeVRDriver::VRDriver::GetDriverHost() {
  return vr::VRServerDriverHost();
}

//-----------------------------------------------------------------------------
// Purpose: Calculates quaternion (qw,qx,qy,qz) representing the rotation. From: https://github.com/Omnifinity/OpenVR-Tracking-Example/blob/master/HTC%20Lighthouse%20Tracking%20Example/LighthouseTracking.cpp
//-----------------------------------------------------------------------------

vr::HmdQuaternion_t
SlimeVRDriver::VRDriver::GetRotation(vr::HmdMatrix34_t &matrix) {
  vr::HmdQuaternion_t q;

  q.w = sqrt(fmax(0, 1 + matrix.m[0][0] + matrix.m[1][1] + matrix.m[2][2])) / 2;
  q.x = sqrt(fmax(0, 1 + matrix.m[0][0] - matrix.m[1][1] - matrix.m[2][2])) / 2;
  q.y = sqrt(fmax(0, 1 - matrix.m[0][0] + matrix.m[1][1] - matrix.m[2][2])) / 2;
  q.z = sqrt(fmax(0, 1 - matrix.m[0][0] - matrix.m[1][1] + matrix.m[2][2])) / 2;
  q.x = copysign(q.x, matrix.m[2][1] - matrix.m[1][2]);
  q.y = copysign(q.y, matrix.m[0][2] - matrix.m[2][0]);
  q.z = copysign(q.z, matrix.m[1][0] - matrix.m[0][1]);
  return q;
}
//-----------------------------------------------------------------------------
// Purpose: Extracts position (x,y,z).
// from:
// https://github.com/Omnifinity/OpenVR-Tracking-Example/blob/master/HTC%20Lighthouse%20Tracking%20Example/LighthouseTracking.cpp
//-----------------------------------------------------------------------------

vr::HmdVector3_t
SlimeVRDriver::VRDriver::GetPosition(vr::HmdMatrix34_t &matrix) {
  vr::HmdVector3_t vector;

  vector.v[0] = matrix.m[0][3];
  vector.v[1] = matrix.m[1][3];
  vector.v[2] = matrix.m[2][3];

  return vector;
}

SlimeVRDriver::UniverseTranslation
SlimeVRDriver::UniverseTranslation::parse(simdjson::ondemand::object &obj) {
  SlimeVRDriver::UniverseTranslation res;
  int iii = 0;
  for (auto component : obj["translation"]) {
    if (iii > 2) {
      break; // TODO: 4 components in a translation vector? should this be an error?
    }
    res.translation.v[iii] = static_cast<float>(component.get_double());
    iii += 1;
  }
  res.yaw = static_cast<float>(obj["yaw"].get_double());

  return res;
}

std::optional<SlimeVRDriver::UniverseTranslation> SlimeVRDriver::VRDriver::SearchUniverse(std::string path_or_json, uint64_t target) {
    try {
        simdjson::padded_string json;
        if (path_or_json.find('{') != std::string::npos) {
            json = simdjson::padded_string(path_or_json);
        } else {
            json = simdjson::padded_string::load(path_or_json);
        }
        simdjson::ondemand::document doc = json_parser_.iterate(json);

        for (simdjson::ondemand::object uni : doc["universes"]) {
            // TODO: universeID comes after the translation, would it be faster to unconditionally parse the translation?
            auto elem = uni["universeID"];
            uint64_t parsed_universe;

            auto is_integer = elem.is_integer();
            if (!is_integer.error() && is_integer.value_unsafe()) {
                parsed_universe = elem.get_uint64();
            } else {
                parsed_universe = elem.get_uint64_in_string();
            }

            if (parsed_universe == target) {
                auto standing_uni = uni["standing"].get_object();
                return SlimeVRDriver::UniverseTranslation::parse(standing_uni.value());
            }
        }
    } catch (simdjson::simdjson_error &e) {
        logger_->Log("Error getting universes from {}: {}", path_or_json, e.what());
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<SlimeVRDriver::UniverseTranslation> SlimeVRDriver::VRDriver::SearchUniverses(uint64_t target) {
    vr::PropertyContainerHandle_t hmd_prop_container = vr::VRProperties()->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd);
    auto driver_chap_json = vr::VRProperties()->GetStringProperty(hmd_prop_container, vr::Prop_DriverProvidedChaperoneJson_String);
    if (driver_chap_json != "") {
        try {
            auto driver_res = SearchUniverse(driver_chap_json, target);
            if (driver_res.has_value()) {
                return driver_res.value();
            }
        }
        catch (simdjson::simdjson_error &e) {
            logger_->Log("Error loading driver-provided chaperone JSON: {}", e.what());
        }
    }

    auto driver_chap_path = vr::VRProperties()->GetStringProperty(hmd_prop_container, vr::Prop_DriverProvidedChaperonePath_String);
    if (driver_chap_path != "") {
        try {
            auto driver_res = SearchUniverse(driver_chap_path, target);
            if (driver_res.has_value()) {
                return driver_res.value();
            }
        }
        catch (simdjson::simdjson_error &e) {
            logger_->Log("Error loading chaperone from driver-provided path {}: {}", driver_chap_path, e.what());
        }
    }

    if (default_chap_path_.has_value() && std::filesystem::exists(default_chap_path_.value())) {
        try {
            return SearchUniverse(default_chap_path_.value(), target);
        }
        catch (simdjson::simdjson_error &e) {
            logger_->Log("Error loading chaperone from default path {}: {}", default_chap_path_.value(), e.what());
        }
    }
    
    return std::nullopt;
}

std::optional<SlimeVRDriver::UniverseTranslation>
SlimeVRDriver::VRDriver::GetCurrentUniverse() {
  if (current_universe_.has_value()) {
    return current_universe_.value().second;
  }

  return std::nullopt;
}

std::optional<vr::DriverPose_t>
SlimeVRDriver::VRDriver::GetExternalPoseForHand(bool left_hand) {
  if (left_hand && external_left_pose_.has_value()) {
    return external_left_pose_;
  }
  if (!left_hand && external_right_pose_.has_value()) {
    return external_right_pose_;
  }
  return std::nullopt;
}

vr::DriverPose_t SlimeVRDriver::VRDriver::DriverPoseFromTrackedDevicePose(
    const vr::TrackedDevicePose_t &raw) {
  vr::DriverPose_t pose =
      IVRDevice::MakeDefaultPose(raw.bDeviceIsConnected, raw.bPoseIsValid);
  pose.poseTimeOffset = 0.0;
  pose.result = raw.eTrackingResult;

  vr::HmdMatrix34_t m = raw.mDeviceToAbsoluteTracking;
  pose.qRotation = GetRotation(m);
  vr::HmdVector3_t pos = GetPosition(m);
  pose.vecPosition[0] = pos.v[0];
  pose.vecPosition[1] = pos.v[1];
  pose.vecPosition[2] = pos.v[2];

  pose.vecVelocity[0] = raw.vVelocity.v[0];
  pose.vecVelocity[1] = raw.vVelocity.v[1];
  pose.vecVelocity[2] = raw.vVelocity.v[2];
  pose.vecAngularVelocity[0] = raw.vAngularVelocity.v[0];
  pose.vecAngularVelocity[1] = raw.vAngularVelocity.v[1];
  pose.vecAngularVelocity[2] = raw.vAngularVelocity.v[2];

  // External poses are already in tracking space; use identity
  // world-from-driver
  pose.qWorldFromDriverRotation.w = 1.0;
  pose.qWorldFromDriverRotation.x = 0.0;
  pose.qWorldFromDriverRotation.y = 0.0;
  pose.qWorldFromDriverRotation.z = 0.0;
  pose.vecWorldFromDriverTranslation[0] = 0.0;
  pose.vecWorldFromDriverTranslation[1] = 0.0;
  pose.vecWorldFromDriverTranslation[2] = 0.0;
  pose.qDriverFromHeadRotation.w = 1.0;
  pose.qDriverFromHeadRotation.x = 0.0;
  pose.qDriverFromHeadRotation.y = 0.0;
  pose.qDriverFromHeadRotation.z = 0.0;
  pose.vecDriverFromHeadTranslation[0] = 0.0;
  pose.vecDriverFromHeadTranslation[1] = 0.0;
  pose.vecDriverFromHeadTranslation[2] = 0.0;

  return pose;
}

bool SlimeVRDriver::VRDriver::ExternalPoseEquals(const vr::DriverPose_t &a,
                                                 const vr::DriverPose_t &b) const {
  float pos_eps = config_frozen_pose_position_epsilon_m_;
  for (int i = 0; i < 3; i++) {
    if (std::fabs(a.vecPosition[i] - b.vecPosition[i]) > pos_eps)
      return false;
  }
  return true;
}

bool SlimeVRDriver::VRDriver::ExternalHandInFrontAndInRadius(
    const double hand_pos[3], const vr::TrackedDevicePose_t &hmd_pose) const {
  if (!hmd_pose.bPoseIsValid)
    return false;
  double r = static_cast<double>(config_external_hand_max_radius_m_);
  const auto &m = hmd_pose.mDeviceToAbsoluteTracking.m;
  double hx = m[0][3], hy = m[1][3], hz = m[2][3];
  double dx = hand_pos[0] - hx, dy = hand_pos[1] - hy, dz = hand_pos[2] - hz;
  double dist_sq = dx * dx + dy * dy + dz * dz;
  if (dist_sq > r * r)
    return false;
  // Forward = -Z in OpenVR (third column of rotation)
  double fx = -m[0][2], fy = -m[1][2], fz = -m[2][2];
  if (dx * fx + dy * fy + dz * fz < 0.0)
    return false; // behind HMD
  return true;
}

void SlimeVRDriver::VRDriver::UpdateExternalControllerPoses() {
  external_left_index_ = vr::k_unTrackedDeviceIndexInvalid;
  external_right_index_ = vr::k_unTrackedDeviceIndexInvalid;

  vr::TrackedDevicePose_t raw_poses[vr::k_unMaxTrackedDeviceCount];
  vr::VRServerDriverHost()->GetRawTrackedDevicePoses(
      0.0f, raw_poses, vr::k_unMaxTrackedDeviceCount);

  std::unordered_set<vr::TrackedDeviceIndex_t> our_indices;
  for (const auto &device : devices_) {
    vr::TrackedDeviceIndex_t idx = device->GetDeviceIndex();
    if (idx != vr::k_unTrackedDeviceIndexInvalid) {
      our_indices.insert(idx);
    }
  }

  const vr::TrackedDevicePose_t &hmd_pose = raw_poses[0];
  auto *props = GetProperties();
  for (uint32_t i = 1; i < vr::k_unMaxTrackedDeviceCount; i++) {
    if (our_indices.count(i))
      continue;
    const vr::TrackedDevicePose_t &p = raw_poses[i];

    vr::PropertyContainerHandle_t container =
        props->TrackedDeviceToPropertyContainer(i);
    vr::ETrackedPropertyError err =
        vr::ETrackedPropertyError::TrackedProp_Success;
    int32_t device_class =
        props->GetInt32Property(container, vr::Prop_DeviceClass_Int32, &err);
    if (err != vr::ETrackedPropertyError::TrackedProp_Success ||
        device_class != vr::TrackedDeviceClass_Controller) {
      continue;
    }
    int32_t role = props->GetInt32Property(
        container, vr::Prop_ControllerRoleHint_Int32, &err);
    if (err != vr::ETrackedPropertyError::TrackedProp_Success)
      continue;

    // Never use our own driver's controllers as "external".
    char manufacturer[256] = {};
    props->GetStringProperty(container, vr::Prop_ManufacturerName_String,
                             manufacturer, sizeof(manufacturer), &err);
    if (err == vr::ETrackedPropertyError::TrackedProp_Success &&
        std::strcmp(manufacturer, "SlimeVR") == 0) {
      continue;
    }

    vr::DriverPose_t driver_pose = DriverPoseFromTrackedDevicePose(p);
    // Only use external hand when it's in front of HMD and within 170 cm radius; else SlimeVR.
    if (!ExternalHandInFrontAndInRadius(driver_pose.vecPosition, hmd_pose)) {
      if (role == vr::TrackedControllerRole_LeftHand)
        external_left_pose_ = std::nullopt;
      else if (role == vr::TrackedControllerRole_RightHand)
        external_right_pose_ = std::nullopt;
      // Don't update last_external_* so next frame we don't compare against a pose we rejected
      continue;
    }
    if (role == vr::TrackedControllerRole_LeftHand) {
      external_left_index_ = i;
      if (last_external_left_pose_.has_value() &&
          ExternalPoseEquals(driver_pose, *last_external_left_pose_)) {
        stale_external_left_frames_++;
        if (stale_external_left_frames_ >= config_stale_external_pose_frames_)
          external_left_pose_ = std::nullopt; // swap to SlimeVR
        else
          external_left_pose_ = driver_pose;
      } else {
        stale_external_left_frames_ = 0;
        external_left_pose_ = driver_pose;
      }
      last_external_left_pose_ = driver_pose;
    } else if (role == vr::TrackedControllerRole_RightHand) {
      external_right_index_ = i;
      if (last_external_right_pose_.has_value() &&
          ExternalPoseEquals(driver_pose, *last_external_right_pose_)) {
        stale_external_right_frames_++;
        if (stale_external_right_frames_ >= config_stale_external_pose_frames_)
          external_right_pose_ = std::nullopt; // swap to SlimeVR
        else
          external_right_pose_ = driver_pose;
      } else {
        stale_external_right_frames_ = 0;
        external_right_pose_ = driver_pose;
      }
      last_external_right_pose_ = driver_pose;
    }
  }
}

uint64_t SlimeVRDriver::VRDriver::GetExternalButtonsForHand(bool left_hand) {
  return left_hand ? external_left_buttons_ : external_right_buttons_;
}
