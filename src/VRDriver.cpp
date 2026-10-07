// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "VRDriver.hpp"
#include "Consts.hpp"
#include "Paths.hpp"
#include "PreciseSleeper.hpp"
#include "Threading.hpp"
#include "TrackerDevice.hpp"
#include "TrackerRole.hpp"

#include <filesystem>
#include <utility>

#include <linalg.h>
#include <simdjson.h>
#include <vrmath/vrmath.h>

#include <solarxr_protocol/generated/all_generated.h>

using namespace solarxr_protocol;
using namespace solarxr_protocol::datatypes;
namespace fs = std::filesystem;

namespace SlimeVRDriver {

vr::EVRInitError VRDriver::Init(vr::IVRDriverContext* pDriverContext) {
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    // The logger can only retrieve the log level from VRSettings after we've
    // initialised the driver context, but we construct the logger before then.
    logger_->UpdateLogLevel();

    logger_->Info("version " GIT_DESC);

    try {
        auto config_path = Paths::GetOpenVRConfigPath().string();
        logger_->Info("Found OpenVR config at {}", config_path);

        auto json = simdjson::padded_string::load(config_path).value();
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc = parser.iterate(json);
        auto path = std::filesystem::path(doc.get_object()["config"].at(0).get_string().value()) / "chaperone_info.vrchap";

        std::error_code ec; // so exists doesn't throw
        if (std::filesystem::exists(path, ec)) {
            default_chap_path_ = path;
            logger_->Info("Found chaperone info file at {}", path.string());
        } else {
            logger_->Error("Couldn't find chaperone info file");
        }
    } catch (simdjson::simdjson_error& e) {
        logger_->Error("Error getting OpenVR config path: {}", e.what());
    }

    bridge_ = std::make_shared<BridgeClient>(
        logger_,
        [this](BridgeTransport::MessageHeader&& v) { OnBridgeMessage(std::move(v)); },
        nullptr,
        [this] {
            body_part_mask_ = 0;
            for (auto device : devices_) {
                device->UpdateStatus(TrackerStatus::DISCONNECTED);
            }
            driver_connection_active_.clear();
        });
    bridge_->Start();

    pose_request_thread_ = std::jthread([this](std::stop_token stop) { return RunPoseRequestThread(stop); }, stop_source_.get_token());

    return vr::VRInitError_None;
}

void VRDriver::Cleanup() {
    // Wake up all threads waiting on init, if SteamVR exits before an HMD is connected and/or before a connection is established
    stop_source_.request_stop();
    steamvr_init_guard_.test_and_set();
    steamvr_init_guard_.notify_all();
    driver_connection_active_.test_and_set();
    driver_connection_active_.notify_all();

    logger_->Info("Waiting for pose request thread to exit");
    pose_request_thread_ = std::jthread();
    bridge_->Stop();
    logger_->Info("Bridge is stopped");

    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* VRDriver::GetInterfaceVersions() {
    return vr::k_InterfaceVersions;
}

BodyPart VRDriver::DetermineDeviceRole(vr::TrackedDeviceIndex_t index) const {
    auto* properties = vr::VRProperties();
    auto* properties_raw = vr::VRPropertiesRaw();

    vr::PropertyContainerHandle_t container = properties->TrackedDeviceToPropertyContainer(index);
    auto device_class = properties->GetInt32Property(container, vr::Prop_DeviceClass_Int32);
    switch (device_class) {
    case vr::TrackedDeviceClass_HMD:
        return BodyPart::HEAD;
    case vr::TrackedDeviceClass_Controller: {
        vr::ETrackedPropertyError error;
        auto controller_role_hint = properties->GetInt32Property(container, vr::Prop_ControllerRoleHint_Int32, &error);
        if (error != vr::TrackedProp_Success) {
            logger_->Warn("Failed to get device {}'s Prop_ControllerRoleHint_Int32: {}", index, properties_raw->GetPropErrorNameFromEnum(error));
            break;
        }

        if (controller_role_hint == vr::ETrackedControllerRole::TrackedControllerRole_LeftHand) {
            return BodyPart::LEFT_HAND;
        } else if (controller_role_hint == vr::ETrackedControllerRole::TrackedControllerRole_RightHand) {
            return BodyPart::RIGHT_HAND;
        } else {
            logger_->Error("Unknown controller role hint {} for device {}", controller_role_hint, index);
            break;
        }
    }
    case vr::TrackedDeviceClass_GenericTracker: {
        vr::ETrackedPropertyError error;
        auto controller_type = properties->GetStringProperty(container, vr::Prop_ControllerType_String, &error);
        if (error != vr::TrackedProp_Success) {
            logger_->Warn("Failed to get device {}'s Prop_ControllerType_String: {}", index, properties_raw->GetPropErrorNameFromEnum(error));
            break;
        }

        for (auto part : EnumValuesBodyPart()) {
            if (auto role_hint = GetViveControllerType(part); !role_hint.empty() && role_hint == controller_type) {
                return part;
            }
        }

        logger_->Warn("Couldn't determine role for device {} (Prop_ControllerType_String='{}')", index, controller_type);
        break;
    }
    default:
        break;
    }

    return BodyPart::NONE;
}

static vr::HmdQuaternion_t EulerYZXToQuaternion(double x, double y, double z) {
    double cX = std::cos(x / 2.f);
    double cY = std::cos(y / 2.f);
    double cZ = std::cos(z / 2.f);
    double sX = std::sin(x / 2.f);
    double sY = std::sin(y / 2.f);
    double sZ = std::sin(z / 2.f);

    return {
        .w = cX * cY * cZ - sX * sY * sZ,
        .x = cY * cZ * sX + cX * sY * sZ,
        .y = cX * cZ * sY + cY * sX * sZ,
        .z = cX * cY * sZ - cZ * sX * sY,
    };
}

PoseTransformation VRDriver::DetermineDevicePalmTransformation(vr::TrackedDeviceIndex_t index) const {
    auto* properties = vr::VRProperties();
    auto* properties_raw = vr::VRPropertiesRaw();
    vr::ETrackedPropertyError err;

    vr::PropertyContainerHandle_t prop_container = properties->TrackedDeviceToPropertyContainer(index);
    if (prop_container == vr::k_ulInvalidPropertyContainer) {
        throw std::runtime_error("Property container invalid");
    }

    std::string render_model_name_prefixed = properties->GetStringProperty(prop_container, vr::Prop_RenderModelName_String, &err);
    if (err != vr::TrackedProp_Success) {
        throw std::runtime_error(std::format("GetStringProperty(Prop_RenderModelName_String) returned {}", properties_raw->GetPropErrorNameFromEnum(err)));
    }

    uint32_t len = vr::VRResources()->GetResourceFullPath(render_model_name_prefixed.c_str(), "rendermodels", nullptr, 0);
    std::string path_str(len - 1, '\0');
    vr::VRResources()->GetResourceFullPath(render_model_name_prefixed.c_str(), "rendermodels", path_str.data(), len);

    fs::path path(path_str);
    auto render_model_name = path.filename().string();
    logger_->Info("Trying to load render model JSON description from path {} (name {})", path_str, render_model_name);
    auto json_path = path / (render_model_name + ".json");

    auto json = simdjson::padded_string::load(json_path.native()).value();
    simdjson::dom::parser parser;
    auto doc = parser.parse(json);

    auto palm_offset = doc["components"]["openxr_handmodel"]["component_local"].get_object();
    if (palm_offset.error()) {
        throw std::runtime_error("Couldn't find hand model transform");
    }

    auto origin_arr = palm_offset["origin"].get_array();
    auto rotate_xyz_arr = palm_offset["rotate_xyz"].get_array();

    // coordinates in the JSON description are in right-handed Y-forward Z-up coordinate system, whereas OpenXR, OpenVR, and SlimeVR are Y-up Z-back
    return PoseTransformation{
        .translation = {
            .v = {
                static_cast<float>(origin_arr.at(0).get_double()),
                static_cast<float>(-origin_arr.at(2).get_double()),
                static_cast<float>(origin_arr.at(1).get_double()),
            },
        },
        .orientation = EulerYZXToQuaternion(               //
            DEG_TO_RAD(rotate_xyz_arr.at(0).get_double()), //
            DEG_TO_RAD(rotate_xyz_arr.at(1).get_double()), //
            DEG_TO_RAD(rotate_xyz_arr.at(2).get_double())),
    };
}

void VRDriver::RunPoseRequestThread(std::stop_token stop) {
    using namespace std::chrono_literals;

    Threading::SetThisThreadName("Pose request");

    flatbuffers::FlatBufferBuilder fbb;
    std::vector<flatbuffers::Offset<driver_protocol::DriverMessageHeader>> driver_msgs{};
    driver_msgs.reserve(64);

    auto notify_status_changed = [this, &fbb, &driver_msgs](DeviceData& device, vr::TrackedDeviceIndex_t index, uint16_t tracker_id, TrackerStatus status) {
        if (device.status != status) {
            logger_->Info("Status for tracker {} (device {}) changing {}->{}", tracker_id, index, EnumNameTrackerStatus(device.status), EnumNameTrackerStatus(status));

            auto update_status_msg = driver_protocol::CreateUpdateTrackerStatus(fbb, tracker_id, status);
            auto header = driver_protocol::CreateDriverMessageHeader(fbb, 0, 0, driver_protocol::DriverMessage::UpdateTrackerStatus, update_status_msg.Union());

            driver_msgs.push_back(header);
            device.status = status;
        }
    };

    logger_->Info("Pose request thread started");
    steamvr_init_guard_.wait(false);
    // If SteamVR exited before initialisation completed, we'll just
    // skip past the loop body on the first iteration anyway

    PreciseSleeper sleeper;
    logger_->Info("Entering pose request loop");
    while (!stop.stop_requested()) {
        if (!bridge_->IsConnected() || !driver_connection_active_.test()) {
            // If bridge not connected, assume we need to resend device add messages
            for (auto& device : feeder_devices_) {
                device.sent_add_message = false;
                device.tracker_id.store(0);
                device.status = TrackerStatus::DISCONNECTED;
            }
            driver_connection_active_.wait(false);
            continue;
        }

        auto tick_start_time = std::chrono::steady_clock::now();

        auto* properties = vr::VRProperties();
        auto* properties_raw = vr::VRPropertiesRaw();

        vr::PropertyContainerHandle_t hmd_prop_container = properties->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd);
        std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
        vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.0f, poses.data(), poses.size());

        vr::ETrackedPropertyError universe_error;
        uint64_t universe = properties->GetUint64Property(hmd_prop_container, vr::Prop_CurrentUniverseId_Uint64, &universe_error);
        if (universe_error == vr::ETrackedPropertyError::TrackedProp_Success) {
            if (!current_universe_.has_value() || current_universe_.value().first != universe) {
                auto result = SearchUniverses(universe);
                if (result.has_value()) {
                    current_universe_.emplace(universe, result.value());
                    logger_->Info("Found current universe");
                }
            }
        } else if (universe_error != last_universe_error_) {
            logger_->Warn("Failed to find current universe: Prop_CurrentUniverseId_Uint64 error = {}",
                          properties_raw->GetPropErrorNameFromEnum(universe_error));
        }
        last_universe_error_ = universe_error;

        for (uint32_t index = 0; index < vr::k_unMaxTrackedDeviceCount; index++) {
            DeviceData& device = feeder_devices_[index];
            if (device.blacklisted)
                continue;

            vr::TrackedDevicePose_t& pose = poses[index];
            vr::PropertyContainerHandle_t prop_container = properties->TrackedDeviceToPropertyContainer(index);
            if (prop_container == vr::k_ulInvalidPropertyContainer)
                continue;

            if (!device.sent_add_message) {
                if (!pose.bDeviceIsConnected)
                    continue;

                vr::ETrackedPropertyError error;

                std::string driver_name = properties->GetStringProperty(prop_container, vr::Prop_TrackingSystemName_String, &error);
                if (error != vr::TrackedProp_Success) {
                    logger_->Warn("Failed to get device {}'s Prop_TrackingSystemName_String: {}", index, properties_raw->GetPropErrorNameFromEnum(error));
                    continue;
                }
                vr::ETrackedDeviceClass device_class = static_cast<vr::ETrackedDeviceClass>(properties->GetInt32Property(prop_container, vr::Prop_DeviceClass_Int32, &error));
                if (error != vr::TrackedProp_Success) {
                    logger_->Warn("Failed to get device {}'s Prop_DeviceClass_Int32: {}", index, properties_raw->GetPropErrorNameFromEnum(error));
                    continue;
                }

                // Ignore devices that aren't an HMD, controller, or tracker
                const bool class_blacklisted = device_class == vr::TrackedDeviceClass_Invalid || device_class > vr::TrackedDeviceClass_GenericTracker;
                // Ignore trackers from us or Standable
                const bool driver_blacklisted = driver_name == "slimevr" || driver_name == "standable";

                device.blacklisted = class_blacklisted || driver_blacklisted;
                if (device.blacklisted)
                    continue;

                std::string serial = properties->GetStringProperty(prop_container, vr::Prop_SerialNumber_String, &error);
                if (error != vr::TrackedProp_Success) {
                    logger_->Warn("Failed to get device {}'s Prop_SerialNumber_String: {}", index, properties_raw->GetPropErrorNameFromEnum(error));
                    continue;
                }
                std::string name = properties->GetStringProperty(prop_container, vr::Prop_ModelNumber_String, &error);
                if (error != vr::TrackedProp_Success) {
                    logger_->Warn("Failed to get device {}'s Prop_ModelNumber_String: {}", index, properties_raw->GetPropErrorNameFromEnum(error));
                    name = std::format("Device {}", index);
                }
                std::string manufacturer = properties->GetStringProperty(prop_container, vr::Prop_ManufacturerName_String, &error);
                if (error != vr::TrackedProp_Success) {
                    logger_->Warn("Failed to get device {}'s Prop_ManufacturerName_String: {}", index, properties_raw->GetPropErrorNameFromEnum(error));
                    manufacturer = "OpenVR";
                }

                BodyPart role = DetermineDeviceRole(index);
                if (role == BodyPart::LEFT_HAND || role == BodyPart::RIGHT_HAND) {
                    try {
                        device.raw_to_palm_transformation = DetermineDevicePalmTransformation(index);
                    } catch (std::exception& e) {
                        logger_->Warn("Couldn't determine palm transformation for device {}: {}", index, e.what());
                        device.raw_to_palm_transformation = std::nullopt;
                    }
                }

                // Send add message for device
                auto add_tracker_msg = driver_protocol::CreateAddTrackerRequest(fbb, fbb.CreateString(serial), fbb.CreateString(name), fbb.CreateString(manufacturer), role);
                auto msg_header = driver_protocol::CreateDriverMessageHeader(fbb, index, 0, driver_protocol::DriverMessage::AddTrackerRequest, add_tracker_msg.Union());

                driver_msgs.push_back(msg_header);
                device.sent_add_message = true;
                logger_->Info("Sent add message for device {}: serial={}, model={}, manufacturer={}, role=BodyPart::{}", index, serial, name, manufacturer, EnumNameBodyPart(role));
            }

            uint16_t tracker_id = device.tracker_id.load();
            // Didn't get tracker ID yet...
            if (tracker_id == 0)
                continue;

            if (pose.bDeviceIsConnected && (pose.bPoseIsValid || pose.eTrackingResult == vr::TrackingResult_Fallback_RotationOnly)) {
                auto status = pose.eTrackingResult == vr::TrackingResult_Fallback_RotationOnly
                    ? TrackerStatus::OCCLUDED
                    : TrackerStatus::OK;
                notify_status_changed(device, index, tracker_id, status);

                vr::HmdQuaternion_t q = GetRotation(pose.mDeviceToAbsoluteTracking);
                vr::HmdVector3_t pos = GetPosition(pose.mDeviceToAbsoluteTracking);

                if (device.raw_to_palm_transformation) {
                    q = q * device.raw_to_palm_transformation->orientation;
                    pos = pos + device.raw_to_palm_transformation->translation;
                }

                if (current_universe_.has_value()) {
                    current_universe_->second.apply(pos, q);
                }

                math::Quat quat_fbs(q.x, q.y, q.z, q.w);
                math::Vec3f position_fbs(pos.v[0], pos.v[1], pos.v[2]);
                math::Vec3f angular_velocity_fbs(pose.vAngularVelocity.v[0], pose.vAngularVelocity.v[1], pose.vAngularVelocity.v[2]);
                math::Vec3f linear_velocity_fbs(pose.vVelocity.v[0], pose.vVelocity.v[1], pose.vVelocity.v[2]);
                auto update_pos_msg = driver_protocol::CreateUpdateTrackerPosition(fbb, tracker_id, &quat_fbs, &position_fbs, &angular_velocity_fbs, &linear_velocity_fbs);
                auto msg_header = driver_protocol::CreateDriverMessageHeader(fbb, 0, 0, driver_protocol::DriverMessage::UpdateTrackerPosition, update_pos_msg.Union());

                driver_msgs.push_back(msg_header);
            } else if (pose.bDeviceIsConnected) {
                notify_status_changed(
                    device,
                    index,
                    tracker_id,
                    pose.eTrackingResult == vr::TrackingResult_Calibrating_OutOfRange
                        ? TrackerStatus::OCCLUDED
                        : TrackerStatus::DISCONNECTED);
            } else {
                notify_status_changed(device, index, tracker_id, TrackerStatus::DISCONNECTED);
            }

            auto now = std::chrono::steady_clock::now();
            if (now - device.battery_sent_at > 100ms) {
                if (properties->GetBoolProperty(prop_container, vr::Prop_DeviceProvidesBatteryStatus_Bool)) {
                    float battery_percentage = properties->GetFloatProperty(prop_container, vr::Prop_DeviceBatteryPercentage_Float);
                    if (std::fabs(device.last_battery_percentage - battery_percentage) > std::numeric_limits<float>::epsilon()) {
                        uint8_t battery_level = uint8_t(battery_percentage * 100.f);
                        bool charging = properties->GetBoolProperty(prop_container, vr::Prop_DeviceIsCharging_Bool);

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

        auto tick_end_time = std::chrono::steady_clock::now();
        auto elapsed = tick_end_time - tick_start_time;
        if (elapsed < 2ms) {
            sleeper.SleepFor(2ms - elapsed);
        }
    }

    logger_->Info("Pose request thread exiting");
    simdjson::ondemand::parser::release_parser();
}

void VRDriver::RunFrame() {
    // Collect events
    vr::VREvent_t event;
    std::vector<vr::VREvent_t> events;
    auto* properties = vr::VRProperties();

    while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event))) {
        logger_->Debug("Received VREvent {}", event.eventType);
        events.push_back(event);

        if (steamvr_init_guard_.test()) {
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
                logger_->Info("Received TrackedDeviceActivated for HMD and its device class is not Invalid");
                signal_steamvr_init_done = true;
            }
            break;
        }
        default:
            signal_steamvr_init_done = hmd_device_class != vr::TrackedDeviceClass_Invalid;
            if (signal_steamvr_init_done) {
                logger_->Info("Received an event and device class for HMD is not Invalid");
            }
            break;
        }
        if (signal_steamvr_init_done) {
            logger_->Info("Signaling that SteamVR is done initialising");
            steamvr_init_guard_.test_and_set();
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

void VRDriver::OnBridgeMessage(const data_feed::DataFeedMessageHeader*) {
    // Ignored
}
void VRDriver::OnBridgeMessage(const rpc::RpcMessageHeader* msg) {
    using solarxr_protocol::rpc::RpcMessage;
    switch (msg->message_type()) {
    case RpcMessage::BoneRoutingSettingsResponse: {
        auto resp = msg->message_as<rpc::BoneRoutingSettingsResponse>();

        body_part_mask_ = 0;

        auto routes = resp->routes();
        if (!routes) {
            logger_->Info("Got BoneRoutingSettingsResponse without routes");
            for (auto device : devices_) {
                device->UpdateStatus(TrackerStatus::DISCONNECTED);
            }
            break;
        }

        for (auto route : *routes) {
            datatypes::BodyPart body_part = route->bone();
            std::shared_ptr<IVRDevice> device = devices_by_role_.contains(body_part) ? devices_by_role_.at(body_part) : nullptr;

            auto outputs = route->outputs();
            if (!outputs) {
                logger_->Info("Got route for bone {} with no outputs", EnumNameBodyPart(body_part));
                if (device)
                    device->UpdateStatus(TrackerStatus::DISCONNECTED);
                continue;
            }

            bool enabled = false;

            for (auto output : *outputs) {
                if (output == rpc::RoutingOutput::DRIVER) {
                    enabled = true;
                    break;
                }
            }

            if (enabled) {
                logger_->Info("Bone {} is enabled", EnumNameBodyPart(body_part));
                body_part_mask_ |= static_cast<uint64_t>(1) << std::to_underlying(body_part);

                if (device)
                    device->UpdateStatus(TrackerStatus::OK);
            } else {
                if (device)
                    device->UpdateStatus(TrackerStatus::DISCONNECTED);
            }
        }

        logger_->Debug("Body part mask changed to {:#b}", body_part_mask_);

        break;
    }
    default:
        break;
    }
}

void VRDriver::OnBridgeMessage(const driver_protocol::DriverMessageHeader* msg) {
    using solarxr_protocol::driver_protocol::DriverMessage;
    switch (msg->message_type()) {
    case DriverMessage::HandshakeAvailable: {
        logger_->Info("Got HandshakeAvailable, firing off thread");
        std::thread t([this] {
            Threading::SetThisThreadName("Handshake request");

            steamvr_init_guard_.wait(false);
            if (stop_source_.stop_requested()) {
                // Cleanup was called
                return;
            }

            logger_->Info("Sending HandshakeRequest");
            flatbuffers::FlatBufferBuilder fbb(256);

            auto handshake_msg = driver_protocol::CreateHandshakeRequest(fbb,
                                                                         fbb.CreateString("SteamVR"),
                                                                         datatypes::CreateBoneMask(fbb, true, false, false, true, false, true, true, true));
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
        logger_->Info("Got HandshakeResponse with status={}", driver_protocol::EnumNameHandshakeStatus(status));
        if (status == driver_protocol::HandshakeStatus::ACCEPTED) {
            flatbuffers::FlatBufferBuilder fbb(256);

            auto bone_routing_req = rpc::CreateBoneRoutingSettingsRequest(fbb);
            auto rpc_msg_header = rpc::CreateRpcMessageHeader(fbb, 0, 0, rpc::RpcMessage::BoneRoutingSettingsRequest, bone_routing_req.Union());
            auto msgs = fbb.CreateVector({ rpc_msg_header });
            auto bundle = CreateMessageBundle(fbb, 0, msgs, 0);
            fbb.Finish(bundle);
            bridge_->SendMessage(fbb);

            driver_connection_active_.test_and_set();
            driver_connection_active_.notify_all();
        } else {
            for (auto& [_, device] : devices_by_role_) {
                if (!device)
                    continue; // ??

                device->UpdateStatus(TrackerStatus::DISCONNECTED);
            }
            driver_connection_active_.clear();
        }

        break;
    }
    case DriverMessage::AddTrackerResponse: {
        auto resp = msg->message_as<driver_protocol::AddTrackerResponse>();
        uint32_t reply_to = msg->reply_to();
        auto status = resp->status();
        if (status == driver_protocol::AddTrackerStatus::ERROR) {
            logger_->Warn("Got AddTrackerResponse with status=ERROR reply_to={}", reply_to);
            break;
        }

        uint16_t tracker_id = resp->tracker_id();
        logger_->Debug("Got AddTrackerResponse with reply_to={} tracker_id={}", reply_to, tracker_id);
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
            if (!tracker_enabled)
                continue;

            std::shared_ptr<IVRDevice> device = devices_by_role_.contains(body_part) ? devices_by_role_.at(body_part) : nullptr;
            if (!device) {
                device = std::make_shared<TrackerDevice>(logger_, GetSerial(body_part), body_part);
                if (!AddDevice(device)) {
                    continue;
                }

                device->UpdateStatus(TrackerStatus::OK);
                if (const auto battery = queued_bone_battery_.extract(body_part)) {
                    const BatteryInfo& info = battery.mapped();
                    device->UpdateBattery(static_cast<float>(info.level) / 100.f, info.charging);
                }
            }

            const datatypes::math::Quat* orientation = bone->orientation();
            const datatypes::math::Vec3f* tail_position = bone->tail_position();
            const datatypes::math::Vec3f* linear_velocity = bone->linear_velocity();
            const datatypes::math::Vec3f* angular_velocity = bone->angular_velocity();

            device->UpdatePose(orientation, tail_position, linear_velocity, angular_velocity);
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
            logger_->Debug("Got BoneBatteryUpdate(bone=BodyPart::{} battery_level={} charging={}) with no device", EnumNameBodyPart(body_part), battery_level, charging);
            queued_bone_battery_.try_emplace(body_part, battery_level, charging);
            break;
        }

        logger_->Debug("Got BoneBatteryUpdate(bone=BodyPart::{} battery_level={} charging={})", EnumNameBodyPart(body_part), battery_level, charging);
        device->UpdateBattery(static_cast<float>(battery_level) / 100.f, charging);
        break;
    }
    default:
        break;
    }
}

void VRDriver::OnBridgeMessage(BridgeTransport::MessageHeader&& message) {
    std::visit([this](const auto* msg) {
        OnBridgeMessage(msg);
    },
               message);
}

bool VRDriver::ShouldBlockStandbyMode() {
    return false;
}

void VRDriver::EnterStandby() {
}

void VRDriver::LeaveStandby() {
}

std::vector<std::shared_ptr<IVRDevice>> VRDriver::GetDevices() {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    std::vector<std::shared_ptr<IVRDevice>> devices;
    devices.assign(devices.begin(), devices.end());
    return devices;
}

const std::vector<vr::VREvent_t>& VRDriver::GetOpenVREvents() {
    return openvr_events_;
}

std::chrono::milliseconds VRDriver::GetLastFrameTime() {
    return frame_timing_;
}

bool VRDriver::AddDevice(std::shared_ptr<IVRDevice> device) {
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
        logger_->Error("Tried to re-add device with role BodyPart::{}", EnumNameBodyPart(body_part));
        return false;
    }

    auto serial = device->GetSerial();
    if (serial.empty()) {
        logger_->Error("Tried to add device for role BodyPart::{} with empty serial number (unhandled)", EnumNameBodyPart(body_part));
        return false;
    }

    // Doesn't apply until restart of SteamVR
    auto role = GetTrackerRole(body_part);
    if (!role.empty()) {
        vr::VRSettings()->SetString(vr::k_pch_Trackers_Section, ("/devices/slimevr/" + serial).c_str(), role.c_str());
    }

    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(serial.c_str(), openvr_device_class, device.get())) {
        logger_->Error("Failed to add device for BodyPart::{} (\"{}\")", EnumNameBodyPart(body_part), device->GetSerial());
        return false;
    }

    devices_.push_back(device);
    devices_by_role_[body_part] = device;
    logger_->Info("Added device {} for BodyPart::{}", device->GetSerial(), EnumNameBodyPart(body_part));
    return true;
}

SettingsValue VRDriver::GetSettingsValue(std::string key) {
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

vr::HmdQuaternion_t VRDriver::GetRotation(vr::HmdMatrix34_t& matrix) {
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

vr::HmdVector3_t VRDriver::GetPosition(vr::HmdMatrix34_t& matrix) {
    vr::HmdVector3_t vector;

    vector.v[0] = matrix.m[0][3];
    vector.v[1] = matrix.m[1][3];
    vector.v[2] = matrix.m[2][3];

    return vector;
}

UniverseTranslation UniverseTranslation::parse(simdjson::ondemand::object& obj) {
    UniverseTranslation res;
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
void UniverseTranslation::apply(vr::HmdVector3_t& pos, vr::HmdQuaternion_t& q) {
    pos.v[0] += translation.v[0];
    pos.v[1] += translation.v[1];
    pos.v[2] += translation.v[2];

    // rotate by quaternion w = cos(-trans.yaw / 2), x = 0, y = sin(-trans.yaw / 2), z = 0
    auto tmp_w = cos(-yaw / 2);
    auto tmp_y = sin(-yaw / 2);
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
    float tmp_sin = sin(-yaw);
    float tmp_cos = cos(-yaw);
    auto pos_x = pos.v[0] * tmp_cos + pos.v[2] * tmp_sin;
    auto pos_z = pos.v[0] * -tmp_sin + pos.v[2] * tmp_cos;

    pos.v[0] = pos_x;
    pos.v[2] = pos_z;
}

std::optional<UniverseTranslation> VRDriver::SearchUniverse(const simdjson::padded_string& json, uint64_t target) {
    simdjson::ondemand::document doc = simdjson::ondemand::parser::get_parser().iterate(json);

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
            return UniverseTranslation::parse(standing_uni.value());
        }
    }

    return std::nullopt;
}

std::optional<UniverseTranslation> VRDriver::SearchUniverses(uint64_t target) {
    vr::PropertyContainerHandle_t hmd_prop_container = vr::VRProperties()->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd);
    auto driver_chap_json = vr::VRProperties()->GetStringProperty(hmd_prop_container, vr::Prop_DriverProvidedChaperoneJson_String);
    if (driver_chap_json != "") {
        try {
            auto universe = SearchUniverse(driver_chap_json, target);
            if (universe)
                return universe;
        } catch (simdjson::simdjson_error& e) {
            logger_->Error("Error loading driver-provided chaperone JSON: {}", e.what());
        }
    }

    auto driver_chap_path = vr::VRProperties()->GetStringProperty(hmd_prop_container, vr::Prop_DriverProvidedChaperonePath_String);
    if (driver_chap_path != "") {
        try {
            auto universe = SearchUniverse(simdjson::padded_string::load(driver_chap_path).take_value(), target);
            if (universe)
                return universe;
        } catch (simdjson::simdjson_error& e) {
            logger_->Error("Error loading chaperone from driver-provided path {}: {}", driver_chap_path, e.what());
        }
    }

    std::error_code ec; // so exists doesn't throw
    if (default_chap_path_.has_value() && std::filesystem::exists(default_chap_path_.value(), ec)) {
        try {
            auto universe = SearchUniverse(simdjson::padded_string::load(default_chap_path_.value().string()).take_value(), target);
            if (universe)
                return universe;
        } catch (simdjson::simdjson_error& e) {
            logger_->Error("Error loading chaperone from default path: {}", e.what());
        }
    }

    return std::nullopt;
}

std::optional<UniverseTranslation> VRDriver::GetCurrentUniverse() {
    if (current_universe_) {
        return current_universe_->second;
    }

    return std::nullopt;
}

} // namespace SlimeVRDriver
