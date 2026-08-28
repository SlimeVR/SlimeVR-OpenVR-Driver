#include "VRDriver.hpp"
#include "Consts.hpp"
#include "Paths.hpp"
#include "TrackerDevice.hpp"
#include "TrackerRole.hpp"

#include <cmath>
#include <limits>
#include <mutex>
#include <utility>

#include <linalg.h>
#include <simdjson.h>
#include <solarxr_protocol/generated/all_generated.h>

using namespace solarxr_protocol;
using namespace solarxr_protocol::datatypes;

vr::EVRInitError SlimeVRDriver::VRDriver::Init(vr::IVRDriverContext* pDriverContext) {
    // Perform driver context initialisation
    if (vr::EVRInitError init_error = vr::InitServerDriverContext(pDriverContext); init_error != vr::EVRInitError::VRInitError_None) {
        return init_error;
    }

    logger_->Log("version " GIT_DESC);

    try {
        auto config_path = Paths::GetOpenVRConfigPath().string();
        logger_->Log("Found OpenVR config at {}", config_path);

        auto json = simdjson::padded_string::load(config_path).value();
        simdjson::ondemand::document doc = json_parser_.iterate(json);
        auto path = std::filesystem::path(doc.get_object()["config"].at(0).get_string().value()) / "chaperone_info.vrchap";
        if (std::filesystem::exists(path)) {
            default_chap_path_ = path;
            logger_->Log("Found chaperone info file at {}", path.string());
        } else {
            logger_->Log("Couldn't find chaperone info file");
        }
    } catch (simdjson::simdjson_error& e) {
        logger_->Log("Error getting OpenVR config path: {}", e.what());
    }

    bridge_ = std::make_shared<BridgeClient>(
        std::static_pointer_cast<Logger>(std::make_shared<VRLogger>("Bridge")),
        [this](BridgeTransport::MessageHeader&& v) { OnBridgeMessage(std::move(v)); },
        nullptr,
        [this] { driver_connection_active_.store(false); });
    bridge_->Start();

    pose_request_thread_ = std::jthread([this](std::stop_token stop) { return RunPoseRequestThread(stop); }, stop_source_.get_token());

    return vr::VRInitError_None;
}

void SlimeVRDriver::VRDriver::Cleanup() {
    // Wake up all threads waiting on init, if SteamVR exits before an HMD is connected and/or before a connection is established
    stop_source_.request_stop();
    steamvr_init_guard_.store(true);
    steamvr_init_guard_.notify_all();
    driver_connection_active_.store(true);
    driver_connection_active_.notify_all();

    logger_->Log("Waiting for pose request thread to exit");
    pose_request_thread_ = std::jthread();
    logger_->Log("Stopping bridge");
    bridge_->Stop();
}

const char* const* SlimeVRDriver::VRDriver::GetInterfaceVersions() {
    return vr::k_InterfaceVersions;
}

BodyPart SlimeVRDriver::VRDriver::GetRoleForDevice(vr::TrackedDeviceIndex_t index) const {
    vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer(index);
    auto device_class = vr::VRProperties()->GetInt32Property(container, vr::Prop_DeviceClass_Int32);
    switch (device_class) {
    case vr::TrackedDeviceClass_HMD:
        return BodyPart::HEAD;
    case vr::TrackedDeviceClass_Controller: {
        auto controller_role_hint = vr::VRProperties()->GetInt32Property(container, vr::Prop_ControllerRoleHint_Int32);
        if (controller_role_hint == vr::ETrackedControllerRole::TrackedControllerRole_LeftHand) {
            return BodyPart::LEFT_HAND;
        } else if (controller_role_hint == vr::ETrackedControllerRole::TrackedControllerRole_RightHand) {
            return BodyPart::RIGHT_HAND;
        } else {
            logger_->Log("Unknown controller role hint {} for device {}", controller_role_hint, index);
            return BodyPart::NONE;
        }
    }
    case vr::TrackedDeviceClass_GenericTracker: {
        vr::ETrackedPropertyError error{ vr::TrackedProp_Success };
        auto controller_type = vr::VRProperties()->GetStringProperty(container, vr::Prop_ControllerType_String, &error);
        if (controller_type.empty()) {
            logger_->Log("Unable to get controller type for device {}: {}", index, vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(error));
            return BodyPart::NONE;
        }

        for (auto part : EnumValuesBodyPart()) {
            if (auto role_hint = GetViveControllerType(part); !role_hint.empty() && role_hint == controller_type) {
                return part;
            }
        }

        logger_->Log("Couldn't determine role for device {} (Prop_ControllerType_String='{}')", index, controller_type);
        return BodyPart::NONE;
    }
    default:
        return BodyPart::NONE;
    }
}

void SlimeVRDriver::VRDriver::RunPoseRequestThread(std::stop_token stop) {
    using namespace std::chrono_literals;

    flatbuffers::FlatBufferBuilder fbb;
    std::vector<flatbuffers::Offset<driver_protocol::DriverMessageHeader>> driver_msgs{};
    driver_msgs.reserve(64);

    auto notify_status_changed = [this, &fbb, &driver_msgs](DeviceData& device, uint16_t id, TrackerStatus status) {
        if (device.status != status) {
            logger_->Log("Status for device {} changing {}->{}", device.index, std::to_underlying(device.status), std::to_underlying(status));

            auto update_status_msg = driver_protocol::CreateUpdateTrackerStatus(fbb, id, status);
            auto header = driver_protocol::CreateDriverMessageHeader(fbb, 0, 0, driver_protocol::DriverMessage::UpdateTrackerStatus, update_status_msg.Union());

            driver_msgs.push_back(header);
            device.status = status;
        }
    };

    logger_->Log("Pose request thread started");
    steamvr_init_guard_.wait(false);
    // If SteamVR exited before initialisation completed, we'll just
    // skip past the loop body on the first iteration anyway

    logger_->Log("Entering pose request loop");
    while (!stop.stop_requested()) {
        if (!bridge_->IsConnected() || !driver_connection_active_) {
            // If bridge not connected, assume we need to resend device add messages
            for (auto& device : feeder_devices_) {
                device.sent_add_message = false;
                device.tracker_id.store(0);
                device.status = TrackerStatus::DISCONNECTED;
            }
            driver_connection_active_.wait(false);
            continue;
        }

        vr::PropertyContainerHandle_t hmd_prop_container = vr::VRProperties()->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd);
        std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
        vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.0f, poses.data(), poses.size());

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
                         vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(universe_error));
        }
        last_universe_error_ = universe_error;

        for (uint32_t index = 0; index < vr::k_unMaxTrackedDeviceCount; index++) {
            DeviceData& device = feeder_devices_[index];
            device.index = index;
            vr::TrackedDevicePose_t& pose = poses[index];
            vr::PropertyContainerHandle_t prop_container = vr::VRProperties()->TrackedDeviceToPropertyContainer(index);

            {
                vr::ETrackedPropertyError error{};

                // Don't feed data about our own trackers and Standable's fake ones
                auto driver_name = vr::VRProperties()->GetStringProperty(prop_container, vr::Prop_TrackingSystemName_String, &error);
                if (error != vr::TrackedProp_Success) {
                    if (error != vr::TrackedProp_InvalidDevice && error != vr::TrackedProp_UnknownProperty)
                        logger_->Log("Failed to get Prop_TrackingSystemName_String for device {}: {}", index, vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(error));

                    continue;
                }
                if (driver_name == "slimevr" || driver_name == "standable")
                    continue;

                auto device_class = (vr::ETrackedDeviceClass)vr::VRProperties()->GetInt32Property(prop_container, vr::Prop_DeviceClass_Int32, &error);
                if (error != vr::TrackedProp_Success) {
                    logger_->Log("Failed to get Prop_DeviceClass_Int32 for device {}: {}", index, vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(error));
                    continue;
                }

                // Ignore devices that aren't HMD, controllers, or generic trackers
                if (device_class == vr::TrackedDeviceClass_Invalid || device_class >= vr::TrackedDeviceClass_TrackingReference) {
                    continue;
                }
            }

            if (!device.sent_add_message) {
                vr::ETrackedPropertyError error{};
                auto serial = vr::VRProperties()->GetStringProperty(prop_container, vr::Prop_SerialNumber_String, &error);
                if (error != vr::ETrackedPropertyError::TrackedProp_Success) {
                    logger_->Log("Failed to get device {}'s Prop_SerialNumber_String: {}", index, vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(error));
                }
                if (serial.empty())
                    serial = std::format("Device {}", index);

                auto name = vr::VRProperties()->GetStringProperty(prop_container, vr::Prop_ModelNumber_String, &error);
                if (error != vr::ETrackedPropertyError::TrackedProp_Success) {
                    logger_->Log("Failed to get device {}'s Prop_ModelNumber_String: {}", index, vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(error));
                }
                if (name.empty())
                    name = std::format("Device {}", index);

                auto manufacturer = vr::VRProperties()->GetStringProperty(prop_container, vr::Prop_ManufacturerName_String, &error);
                if (error != vr::ETrackedPropertyError::TrackedProp_Success) {
                    logger_->Log("Failed to get device {}'s Prop_ManufacturerName_String: {}", index, vr::VRPropertiesRaw()->GetPropErrorNameFromEnum(error));
                }
                if (manufacturer.empty())
                    manufacturer = "OpenVR";

                BodyPart role = GetRoleForDevice(index);

                // Send add message for device
                auto add_tracker_msg = driver_protocol::CreateAddTrackerRequest(fbb, fbb.CreateString(serial), fbb.CreateString(name), fbb.CreateString(manufacturer), role);
                auto msg_header = driver_protocol::CreateDriverMessageHeader(fbb, index, 0, driver_protocol::DriverMessage::AddTrackerRequest, add_tracker_msg.Union());

                driver_msgs.push_back(msg_header);
                device.sent_add_message = true;
                logger_->Log("Sent add message for device {}: serial={}, model={}, manufacturer={}, role=BodyPart::{}", index, serial, name, manufacturer, EnumNameBodyPart(role));
            }

            uint16_t tracker_id = device.tracker_id.load();
            // Didn't get tracker ID yet...
            if (tracker_id == 0)
                continue;

            if (device.sent_add_message && !pose.bDeviceIsConnected) {
                notify_status_changed(device, tracker_id, TrackerStatus::DISCONNECTED);
                continue;
            } else if (!pose.bDeviceIsConnected) {
                // ignore device as it's not connected
                continue;
            }

            if (pose.bPoseIsValid || pose.eTrackingResult == vr::TrackingResult_Fallback_RotationOnly) {
                auto status = pose.eTrackingResult == vr::TrackingResult_Fallback_RotationOnly
                    ? TrackerStatus::OCCLUDED
                    : TrackerStatus::OK;
                notify_status_changed(device, tracker_id, status);

                vr::HmdQuaternion_t q = GetRotation(pose.mDeviceToAbsoluteTracking);
                vr::HmdVector3_t pos = GetPosition(pose.mDeviceToAbsoluteTracking);

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

                auto quat_fbs = math::Quat(q.x, q.y, q.z, q.w);
                auto position_fbs = math::Vec3f(pos.v[0], pos.v[1], pos.v[2]);
                auto update_pos_msg = driver_protocol::CreateUpdateTrackerPosition(fbb, tracker_id, &quat_fbs, &position_fbs);
                auto msg_header = driver_protocol::CreateDriverMessageHeader(fbb, 0, 0, driver_protocol::DriverMessage::UpdateTrackerPosition, update_pos_msg.Union());

                driver_msgs.push_back(msg_header);
            } else {
                notify_status_changed(
                    device,
                    tracker_id,
                    pose.eTrackingResult == vr::TrackingResult_Calibrating_OutOfRange
                        ? TrackerStatus::OCCLUDED
                        : TrackerStatus::DISCONNECTED);
            }

            auto now = std::chrono::steady_clock::now();
            if (now - device.battery_sent_at > 100ms) {
                if (vr::VRProperties()->GetBoolProperty(prop_container, vr::Prop_DeviceProvidesBatteryStatus_Bool)) {
                    float battery_percentage = vr::VRProperties()->GetFloatProperty(prop_container, vr::Prop_DeviceBatteryPercentage_Float);
                    if (std::fabs(device.last_battery_percentage - battery_percentage) > std::numeric_limits<float>::epsilon()) {
                        uint8_t battery_level = uint8_t(battery_percentage * 100.f);
                        bool charging = vr::VRProperties()->GetBoolProperty(prop_container, vr::Prop_DeviceIsCharging_Bool);

                        auto update_battery_msg = driver_protocol::CreateUpdateTrackerBattery(fbb, tracker_id, battery_level, charging);
                        auto msg_header = driver_protocol::CreateDriverMessageHeader(fbb, 0, 0, driver_protocol::DriverMessage::UpdateTrackerBattery, update_battery_msg.Union());

                        driver_msgs.push_back(msg_header);
                        device.last_battery_percentage = battery_percentage;
                    }
                }
                device.battery_sent_at = now;
            }
        }

        if (!driver_msgs.empty()) {
            auto driver_msgs_fbs = fbb.CreateVector(driver_msgs);
            auto bundle = CreateMessageBundle(fbb, 0, 0, driver_msgs_fbs);
            fbb.Finish(bundle);
            bridge_->SendMessage(fbb);

            driver_msgs.clear();
            fbb.Clear();
        }

        std::this_thread::sleep_for(2ms);
    }
    logger_->Log("Pose request thread exiting");
}

void SlimeVRDriver::VRDriver::RunFrame() {
    // Collect events
    vr::VREvent_t event;
    std::vector<vr::VREvent_t> events;
    auto* properties = vr::VRProperties();

    while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event))) {
        events.push_back(event);

        if (steamvr_init_guard_) {
            // We already signaled init was done.
            continue;
        }

        auto hmd_device_class = properties->GetInt32Property(properties->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd), vr::Prop_DeviceClass_Int32);
        bool signal_steamvr_init_done = false;
        // this is the signal SteamVR uses to start up the systemui as of 2.17.1
        switch (event.eventType) {
        case vr::VREvent_TrackedDeviceActivated: {
            if (event.trackedDeviceIndex != vr::k_unTrackedDeviceIndex_Hmd)
                break;
            if (hmd_device_class != vr::TrackedDeviceClass_Invalid) {
                logger_->Log("Received TrackedDeviceActivated for HMD and its device class is not Invalid");
                signal_steamvr_init_done = true;
            }
            break;
        }
        default:
            signal_steamvr_init_done = hmd_device_class != vr::TrackedDeviceClass_Invalid;
            if (signal_steamvr_init_done) {
                logger_->Log("Received an event and device class for HMD is not Invalid");
            }
            break;
        }
        if (signal_steamvr_init_done) {
            logger_->Log("Signaling that SteamVR is done initialising");
            steamvr_init_guard_.store(true);
            steamvr_init_guard_.notify_all();
        }
    }
    openvr_events_ = std::move(events);

    // Update frame timing
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    frame_timing_ = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time_);
    last_frame_time_ = now;

    // Update devices
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        for (auto& device : devices_) {
            device->Update();
        }
    }
}

void SlimeVRDriver::VRDriver::OnBridgeMessage(const data_feed::DataFeedMessageHeader*) {
    // Ignored
}
void SlimeVRDriver::VRDriver::OnBridgeMessage(const rpc::RpcMessageHeader* msg) {
    using solarxr_protocol::rpc::RpcMessage;
    switch (msg->message_type()) {
    case RpcMessage::BoneRoutingSettingsResponse: {
        auto resp = msg->message_as<rpc::BoneRoutingSettingsResponse>();
        auto routes = resp->routes();
        if (!routes) {
            logger_->Log("Got BoneRoutingSettingsResponse without routes");
            body_part_mask_ = 0;
            break;
        }

        body_part_mask_ = 0;
        for (auto route : *routes) {
            auto outputs = route->outputs();
            if (!outputs) {
                logger_->Log("Got route with no outputs");
                continue;
            }
            for (auto output : *outputs) {
                if (output == rpc::RoutingOutput::DRIVER) {
                    datatypes::BodyPart body_part = route->bone();
                    logger_->Log("Bone {} is enabled", EnumNameBodyPart(body_part));
                    body_part_mask_ |= static_cast<uint64_t>(1) << std::to_underlying(body_part);
                }
            }
        }

        logger_->Log("Body part mask changed to {:#b}", body_part_mask_);

        break;
    }
    default:
        break;
    }
}

void SlimeVRDriver::VRDriver::OnBridgeMessage(const driver_protocol::DriverMessageHeader* msg) {
    using solarxr_protocol::driver_protocol::DriverMessage;
    switch (msg->message_type()) {
    case DriverMessage::HandshakeAvailable: {
        logger_->Log("Got HandshakeAvailable, firing off thread");
        std::thread t([this] {
            steamvr_init_guard_.wait(false);
            if (stop_source_.stop_requested()) {
                // Cleanup was called
                return;
            }

            logger_->Log("Sending HandshakeRequest");
            flatbuffers::FlatBufferBuilder fbb(256);

            auto handshake_msg = driver_protocol::CreateHandshakeRequest(fbb, fbb.CreateString("SteamVR"), datatypes::CreateBoneMask(fbb, true, true, false, true, true));
            auto msg_header = driver_protocol::CreateDriverMessageHeader(fbb, 0, 0, driver_protocol::DriverMessage::HandshakeRequest, handshake_msg.Union());
            auto msgs = fbb.CreateVector({ msg_header });
            auto bundle = CreateMessageBundle(fbb, 0, 0, msgs);
            fbb.Finish(bundle);
            bridge_->SendMessage(fbb);
        });
        t.detach();

        break;
    }
    case DriverMessage::HandshakeResponse: {
        auto resp = msg->message_as<driver_protocol::HandshakeResponse>();
        auto status = resp->status();
        logger_->Log("Got HandshakeResponse with status={}", driver_protocol::EnumNameHandshakeStatus(status));
        if (status == driver_protocol::HandshakeStatus::ACCEPTED) {
            flatbuffers::FlatBufferBuilder fbb(256);

            auto bone_routing_req = rpc::CreateBoneRoutingSettingsRequest(fbb);
            auto rpc_msg_header = rpc::CreateRpcMessageHeader(fbb, 0, 0, rpc::RpcMessage::BoneRoutingSettingsRequest, bone_routing_req.Union());
            auto msgs = fbb.CreateVector({ rpc_msg_header });
            auto bundle = CreateMessageBundle(fbb, 0, msgs, 0);
            fbb.Finish(bundle);
            bridge_->SendMessage(fbb);

            driver_connection_active_.store(true);
            driver_connection_active_.notify_all();
        } else {
            for (auto& [_, device] : devices_by_role_) {
                if (!device)
                    continue; // ??

                device->UpdateStatus(TrackerStatus::DISCONNECTED);
            }
            driver_connection_active_.store(false);
        }

        break;
    }
    case DriverMessage::AddTrackerResponse: {
        auto resp = msg->message_as<driver_protocol::AddTrackerResponse>();
        uint32_t reply_to = msg->reply_to();
        auto status = resp->status();
        if (status == driver_protocol::AddTrackerStatus::ERROR) {
            logger_->Log("Got AddTrackerResponse with status=ERROR reply_to={}", reply_to);
            break;
        }

        uint16_t tracker_id = resp->tracker_id();
        logger_->Log("Got AddTrackerResponse with reply_to={} tracker_id={}", reply_to, tracker_id);
        auto& device = feeder_devices_[reply_to];

        device.tracker_id.store(tracker_id);
        break;
    }
    case DriverMessage::SkeletonUpdate: {
        std::lock_guard lock(devices_mutex_);

        auto update = msg->message_as<driver_protocol::SkeletonUpdate>();
        auto bones = update->bones();
        for (auto bone : *bones) {
            auto body_part = bone->body_part();
            if (body_part == BodyPart::NONE || body_part == BodyPart::HEAD)
                continue;

            bool tracker_enabled = body_part_mask_ & (static_cast<uint64_t>(1) << std::to_underlying(body_part));
            std::shared_ptr<IVRDevice> device = devices_by_role_.contains(body_part) ? devices_by_role_.at(body_part) : nullptr;
            if (!tracker_enabled) {
                if (device) {
                    device->UpdateStatus(TrackerStatus::DISCONNECTED);
                }
                continue;
            }

            if (!device) {
                device = std::make_shared<TrackerDevice>(GetSerial(body_part), body_part);
                if (!AddDevice(device)) {
                    continue;
                }

                if (const auto battery = queued_bone_battery_.extract(body_part)) {
                    const BatteryInfo& info = battery.mapped();
                    device->UpdateBattery(static_cast<float>(info.level) / 100.f, info.charging);
                }
            }
            device->UpdateStatus(TrackerStatus::OK);

            const datatypes::math::Vec3f* head_position_fbs = bone->head_position_g();
            const datatypes::math::Quat* orientation_quat_fbs = bone->orientation_g();
            if (head_position_fbs == nullptr) {
                logger_->Log("Got BodyPart::{} with head_position_g=null, continuing", EnumNameBodyPart(body_part));
                continue;
            }
            if (orientation_quat_fbs == nullptr) {
                logger_->Log("Got BodyPart::{} with orientation_g=null, continuing", EnumNameBodyPart(body_part));
                continue;
            }

            // Compute tail position
            linalg::vec<float, 3> head_position(head_position_fbs->x(), head_position_fbs->y(), head_position_fbs->z());
            linalg::vec<float, 4> orientation(orientation_quat_fbs->x(), orientation_quat_fbs->y(), orientation_quat_fbs->z(), orientation_quat_fbs->w());
            linalg::vec<float, 3> towards_tail(0.f, -bone->bone_length(), 0.f);

            linalg::vec<float, 3> tail_offset = linalg::qrot(orientation, towards_tail);
            linalg::vec<float, 3> tail_position = head_position + tail_offset;
            device->UpdatePose(std::move(orientation), std::move(tail_position));
        }

        break;
    }
    case DriverMessage::BoneBatteryUpdate: {
        auto update = msg->message_as<driver_protocol::BoneBatteryUpdate>();
        datatypes::BodyPart body_part = update->bone();
        uint8_t battery_level = update->battery_level();
        bool charging = update->charging();

        std::shared_ptr<IVRDevice> device = devices_by_role_.contains(body_part) ? devices_by_role_.at(body_part) : nullptr;
        if (!device) {
            logger_->Log("Got BoneBatteryUpdate(bone=BodyPart::{} battery_level={} charging={}) with no device", EnumNameBodyPart(body_part), battery_level, charging);
            queued_bone_battery_.try_emplace(body_part, battery_level, charging);
            break;
        }

        logger_->Log("Got BoneBatteryUpdate(bone=BodyPart::{} battery_level={} charging={})", EnumNameBodyPart(body_part), battery_level, charging);
        device->UpdateBattery(static_cast<float>(battery_level) / 100.f, charging);
        break;
    }
    default:
        break;
    }
}

void SlimeVRDriver::VRDriver::OnBridgeMessage(BridgeTransport::MessageHeader&& message) {
    std::visit([this](const auto* msg) {
        OnBridgeMessage(msg);
    },
               message);
}

bool SlimeVRDriver::VRDriver::ShouldBlockStandbyMode() {
    return false;
}

void SlimeVRDriver::VRDriver::EnterStandby() {
}

void SlimeVRDriver::VRDriver::LeaveStandby() {
}

std::vector<std::shared_ptr<SlimeVRDriver::IVRDevice>> SlimeVRDriver::VRDriver::GetDevices() {
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
        openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_Controller;
        break;
    case DeviceType::HMD:
        openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_HMD;
        break;
    case DeviceType::TRACKER:
        openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_GenericTracker;
        break;
    case DeviceType::TRACKING_REFERENCE:
        openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_TrackingReference;
        break;
    default:
        return false;
    }

    auto body_part = device->GetBodyPart();
    if (devices_by_role_.contains(body_part)) {
        logger_->Log("Tried to re-add device with role BodyPart::{}", EnumNameBodyPart(body_part));
        return false;
    }

    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(device->GetSerial().c_str(), openvr_device_class, device.get())) {
        logger_->Log("Failed to add device for BodyPart::{} (\"{}\")", EnumNameBodyPart(body_part), device->GetSerial());
        return false;
    }

    devices_.push_back(device);
    devices_by_role_[body_part] = device;
    logger_->Log("Added device {} for BodyPart::{}", device->GetSerial(), EnumNameBodyPart(body_part));
    return true;
}

SlimeVRDriver::SettingsValue SlimeVRDriver::VRDriver::GetSettingsValue(std::string key) {
    vr::EVRSettingsError err = vr::EVRSettingsError::VRSettingsError_None;
    int int_value = vr::VRSettings()->GetInt32(settings_key_.c_str(), key.c_str(), &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return int_value;
    }
    err = vr::EVRSettingsError::VRSettingsError_None;
    float float_value = vr::VRSettings()->GetFloat(settings_key_.c_str(), key.c_str(), &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return float_value;
    }
    err = vr::EVRSettingsError::VRSettingsError_None;
    bool bool_value = vr::VRSettings()->GetBool(settings_key_.c_str(), key.c_str(), &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return bool_value;
    }
    std::string str_value;
    str_value.reserve(1024);
    vr::VRSettings()->GetString(settings_key_.c_str(), key.c_str(), str_value.data(), 1024, &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return str_value;
    }
    err = vr::EVRSettingsError::VRSettingsError_None;

    return SettingsValue();
}

//-----------------------------------------------------------------------------
// Purpose: Calculates quaternion (qw,qx,qy,qz) representing the rotation
// from: https://github.com/Omnifinity/OpenVR-Tracking-Example/blob/master/HTC%20Lighthouse%20Tracking%20Example/LighthouseTracking.cpp
//-----------------------------------------------------------------------------

vr::HmdQuaternion_t SlimeVRDriver::VRDriver::GetRotation(vr::HmdMatrix34_t& matrix) {
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
// from: https://github.com/Omnifinity/OpenVR-Tracking-Example/blob/master/HTC%20Lighthouse%20Tracking%20Example/LighthouseTracking.cpp
//-----------------------------------------------------------------------------

vr::HmdVector3_t SlimeVRDriver::VRDriver::GetPosition(vr::HmdMatrix34_t& matrix) {
    vr::HmdVector3_t vector;

    vector.v[0] = matrix.m[0][3];
    vector.v[1] = matrix.m[1][3];
    vector.v[2] = matrix.m[2][3];

    return vector;
}

SlimeVRDriver::UniverseTranslation SlimeVRDriver::UniverseTranslation::parse(simdjson::ondemand::object& obj) {
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

std::optional<SlimeVRDriver::UniverseTranslation> SlimeVRDriver::VRDriver::SearchUniverse(const simdjson::padded_string& json, uint64_t target) {
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
        } catch (simdjson::simdjson_error& e) {
            logger_->Log("Error loading driver-provided chaperone JSON: {}", e.what());
        }
    }

    auto driver_chap_path = vr::VRProperties()->GetStringProperty(hmd_prop_container, vr::Prop_DriverProvidedChaperonePath_String);
    if (driver_chap_path != "") {
        try {
            auto driver_res = SearchUniverse(simdjson::padded_string::load(driver_chap_path).take_value(), target);
            if (driver_res.has_value()) {
                return driver_res.value();
            }
        } catch (simdjson::simdjson_error& e) {
            logger_->Log("Error loading chaperone from driver-provided path {}: {}", driver_chap_path, e.what());
        }
    }

    if (default_chap_path_.has_value() && std::filesystem::exists(default_chap_path_.value())) {
        try {
            return SearchUniverse(simdjson::padded_string::load(default_chap_path_.value().string()).take_value(), target);
        } catch (simdjson::simdjson_error& e) {
            logger_->Log("Error loading chaperone from default path: {}", e.what());
        }
    }

    return std::nullopt;
}

std::optional<SlimeVRDriver::UniverseTranslation> SlimeVRDriver::VRDriver::GetCurrentUniverse() {
    if (current_universe_.has_value()) {
        return current_universe_.value().second;
    }

    return std::nullopt;
}
