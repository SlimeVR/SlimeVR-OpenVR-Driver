#include "VRDriver.hpp"
#include <TrackerDevice.hpp>
#include "bridge/bridge.hpp"
#include "TrackerRole.hpp"
#include <google/protobuf/arena.h>
#include <simdjson.h>
#include "VRPaths_openvr.hpp"
#define _USE_MATH_DEFINES
#include <math.h>


vr::EVRInitError SlimeVRDriver::VRDriver::Init(vr::IVRDriverContext* pDriverContext)
{
    // Perform driver context initialisation
    if (vr::EVRInitError init_error = vr::InitServerDriverContext(pDriverContext); init_error != vr::EVRInitError::VRInitError_None) {
        return init_error;
    }

    Log("Activating SlimeVR Driver...");

    try {
        auto json = simdjson::padded_string::load(GetVRPathRegistryFilename()); // load VR Path Registry
        simdjson::ondemand::document doc = json_parser.iterate(json);
        auto path = std::string { doc.get_object()["config"].at(0).get_string().value() };
        // Log(path);
        default_chap_path_ = GetDefaultChaperoneFromConfigPath(path);
    } catch (simdjson::simdjson_error& e) {
        std::stringstream ss;
        ss << "Error getting VR Config path, continuing: " << e.error();
        Log(ss.str());
    }

    Log("SlimeVR Driver Loaded Successfully");

	return vr::VRInitError_None;
}

void SlimeVRDriver::VRDriver::Cleanup()
{
}

void SlimeVRDriver::VRDriver::RunFrame()
{
    google::protobuf::Arena arena;

    // Collect events
    vr::VREvent_t event;
    std::vector<vr::VREvent_t> events;
    while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event)))
    {
        events.push_back(event);
    }
    this->openvr_events_ = std::move(events);

    // Update frame timing
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    this->frame_timing_ = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_frame_time_);
    this->last_frame_time_ = now;

    // Update devices
    for(auto& device : this->devices_)
        device->Update();
    
    BridgeStatus status = runBridgeFrame(*this);
    if(status == BRIDGE_CONNECTED) {
        messages::ProtobufMessage* message = google::protobuf::Arena::CreateMessage<messages::ProtobufMessage>(&arena);
        // Read all messages from the bridge
        while(getNextBridgeMessage(*message, *this)) {
            if(message->has_tracker_added()) {
                messages::TrackerAdded ta = message->tracker_added();
                switch(getDeviceType(static_cast<TrackerRole>(ta.tracker_role()))) {
                    case DeviceType::TRACKER:
                        this->AddDevice(std::make_shared<TrackerDevice>(ta.tracker_serial(),  ta.tracker_id(), static_cast<TrackerRole>(ta.tracker_role())));
                        Log("New tracker device added " + ta.tracker_serial() + " (id " + std::to_string(ta.tracker_id()) + ")");
                    break;
                }
            } else if(message->has_position()) {
                messages::Position pos = message->position();
                auto device = this->devices_by_id.find(pos.tracker_id());
                if(device != this->devices_by_id.end()) {
                    device->second->PositionMessage(pos);
                }
            } else if(message->has_tracker_status()) {
                messages::TrackerStatus status = message->tracker_status();
                auto device = this->devices_by_id.find(status.tracker_id());
                if (device != this->devices_by_id.end()) {
                    device->second->StatusMessage(status);
                }
            }
        }

        if(!sentHmdAddMessage) {
            // Send add message for HMD
            messages::TrackerAdded* trackerAdded = google::protobuf::Arena::CreateMessage<messages::TrackerAdded>(&arena);
            message->set_allocated_tracker_added(trackerAdded);
            trackerAdded->set_tracker_id(0);
            trackerAdded->set_tracker_role(TrackerRole::HMD);
            trackerAdded->set_tracker_serial("HMD");
            trackerAdded->set_tracker_name("HMD");
            sendBridgeMessage(*message, *this);

            messages::TrackerStatus* trackerStatus = google::protobuf::Arena::CreateMessage<messages::TrackerStatus>(&arena);
            message->set_allocated_tracker_status(trackerStatus);
            trackerStatus->set_tracker_id(0);
            trackerStatus->set_status(messages::TrackerStatus_Status::TrackerStatus_Status_OK);
            sendBridgeMessage(*message, *this);

            sentHmdAddMessage = true;
            Log("Sent HMD hello message");
        }

        uint64_t universe = vr::VRProperties()->GetUint64Property(vr::VRProperties()->TrackedDeviceToPropertyContainer(0), vr::Prop_CurrentUniverseId_Uint64);
        if (!current_universe.has_value() || current_universe.value().first != universe) {
            auto res = search_universes(universe);
            if (res.has_value()) {
                current_universe.emplace(universe, res.value());
            } else {
                Log("Failed to find current universe!");
            }
        }

        vr::TrackedDevicePose_t hmd_pose[10];
        vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0, hmd_pose, 10);

        vr::HmdQuaternion_t q = GetRotation(hmd_pose[0].mDeviceToAbsoluteTracking);
        vr::HmdVector3_t pos = GetPosition(hmd_pose[0].mDeviceToAbsoluteTracking);

        if (current_universe.has_value()) {
            auto trans = current_universe.value().second;
            pos.v[0] += trans.translation.v[0];
            pos.v[1] += trans.translation.v[1];
            pos.v[2] += trans.translation.v[2];

            // rotate by quaternion w = cos(trans.yaw / 2), x = 0, y = sin(trans.yaw / 2), z = 0
            // add a factor of PI/2 to fudge a 90 degree rotation. (why...?)
            auto tmp_w = cos((trans.yaw + M_PI/2) / 2);
            auto tmp_y = sin((trans.yaw + M_PI/2) / 2);
            // auto new_w = q.w * tmp_w - q.y * tmp_y;
            // auto new_x = q.x * tmp_w - q.z * tmp_y;
            // auto new_y = q.w * tmp_y + q.y * tmp_w;
            // auto new_z = q.x * tmp_y + q.z * tmp_y;
            auto new_w = tmp_w * q.w - tmp_y * q.y;
            auto new_x = tmp_w * q.x + tmp_y * q.z;
            auto new_y = tmp_w * q.y + tmp_y * q.w;
            auto new_z = tmp_w * q.z - tmp_y * q.x;

            q.w = new_w;
            q.x = new_x;
            q.y = new_y;
            q.z = new_z;

            auto pos_x = pos.v[0] * tmp_w - pos.v[2] * tmp_y;
            auto pos_z = pos.v[2] * tmp_w + pos.v[0] * tmp_y;

            pos.v[0] = pos_x;
            pos.v[2] = pos_z;
        }

        messages::Position* hmdPosition = google::protobuf::Arena::CreateMessage<messages::Position>(&arena);
        message->set_allocated_position(hmdPosition);

        hmdPosition->set_tracker_id(0);
        hmdPosition->set_data_source(messages::Position_DataSource_FULL);
        hmdPosition->set_x(pos.v[0]);
        hmdPosition->set_y(pos.v[1]);
        hmdPosition->set_z(pos.v[2]);
        hmdPosition->set_qx((float) q.x);
        hmdPosition->set_qy((float) q.y);
        hmdPosition->set_qz((float) q.z);
        hmdPosition->set_qw((float) q.w);

        sendBridgeMessage(*message, *this);
    } else {
        // If bridge not connected, assume we need to resend hmd tracker add message
        sentHmdAddMessage = false;

    }
}

bool SlimeVRDriver::VRDriver::ShouldBlockStandbyMode()
{
    return false;
}

void SlimeVRDriver::VRDriver::EnterStandby()
{
}

void SlimeVRDriver::VRDriver::LeaveStandby()
{
}

std::vector<std::shared_ptr<SlimeVRDriver::IVRDevice>> SlimeVRDriver::VRDriver::GetDevices()
{
    return this->devices_;
}

std::vector<vr::VREvent_t> SlimeVRDriver::VRDriver::GetOpenVREvents()
{
    return this->openvr_events_;
}

std::chrono::milliseconds SlimeVRDriver::VRDriver::GetLastFrameTime()
{
    return this->frame_timing_;
}

bool SlimeVRDriver::VRDriver::AddDevice(std::shared_ptr<IVRDevice> device)
{
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
    bool result = vr::VRServerDriverHost()->TrackedDeviceAdded(device->GetSerial().c_str(), openvr_device_class, device.get());
    if(result) {
        this->devices_.push_back(device);
        this->devices_by_id[device->getDeviceId()] = device;
        this->devices_by_serial[device->GetSerial()] = device;
    } else {
        std::shared_ptr<IVRDevice> oldDevice = this->devices_by_serial[device->GetSerial()];
        if(oldDevice->getDeviceId() != device->getDeviceId()) {
            this->devices_by_id[device->getDeviceId()] = oldDevice;
            Log("Device overridden from id " + std::to_string(oldDevice->getDeviceId()) + " to " + std::to_string(device->getDeviceId()) + " for serial " + device->GetSerial());
        } else {
            Log("Device readded id " + std::to_string(device->getDeviceId()) + ", serial " + device->GetSerial());
        }
    }
    return result;
}

SlimeVRDriver::SettingsValue SlimeVRDriver::VRDriver::GetSettingsValue(std::string key)
{
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

void SlimeVRDriver::VRDriver::Log(std::string message)
{
    std::string message_endl = message + "\n";
    vr::VRDriverLog()->Log(message_endl.c_str());
}

vr::IVRDriverInput* SlimeVRDriver::VRDriver::GetInput()
{
    return vr::VRDriverInput();
}

vr::CVRPropertyHelpers* SlimeVRDriver::VRDriver::GetProperties()
{
    return vr::VRProperties();
}

vr::IVRServerDriverHost* SlimeVRDriver::VRDriver::GetDriverHost()
{
    return vr::VRServerDriverHost();
}

//-----------------------------------------------------------------------------
// Purpose: Calculates quaternion (qw,qx,qy,qz) representing the rotation
// from: https://github.com/Omnifinity/OpenVR-Tracking-Example/blob/master/HTC%20Lighthouse%20Tracking%20Example/LighthouseTracking.cpp
//-----------------------------------------------------------------------------

vr::HmdQuaternion_t SlimeVRDriver::VRDriver::GetRotation(vr::HmdMatrix34_t &matrix) {
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

vr::HmdVector3_t SlimeVRDriver::VRDriver::GetPosition(vr::HmdMatrix34_t &matrix) {
    vr::HmdVector3_t vector;

    vector.v[0] = matrix.m[0][3];
    vector.v[1] = matrix.m[1][3];
    vector.v[2] = matrix.m[2][3];

    return vector;
}

SlimeVRDriver::UniverseTranslation SlimeVRDriver::UniverseTranslation::parse(simdjson::ondemand::object &obj) {
    SlimeVRDriver::UniverseTranslation res;
    int iii = 0;
    for (auto component: obj["translation"]) {
        if (iii > 2) {
            break; // TODO: 4 components in a translation vector? should this be an error?
        }
        res.translation.v[iii] = component.get_double();
        iii += 1;
    }
    res.yaw = obj["yaw"].get_double();

    return res;
}

std::optional<SlimeVRDriver::UniverseTranslation> SlimeVRDriver::VRDriver::search_universe(std::string path, uint64_t target) {
    try {
        auto json = simdjson::padded_string::load(path); // load VR Path Registry
        simdjson::ondemand::document doc = json_parser.iterate(json);

        for (simdjson::ondemand::object uni: doc["universes"]) {
            // TODO: universeID comes after the translation, would it be faster to unconditionally parse the translation?
            if (uni["universeID"].get_uint64_in_string() == target) {
                return SlimeVRDriver::UniverseTranslation::parse(uni["standing"].get_object().value());
            }
        }
    } catch (simdjson::simdjson_error& e) {
        std::stringstream ss;
        ss << "Error getting universes from \"" << path << "\": " << e.error();
        Log(ss.str());
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<SlimeVRDriver::UniverseTranslation> SlimeVRDriver::VRDriver::search_universes(uint64_t target) {
    auto driver_chap_path = vr::VRProperties()->GetStringProperty(vr::VRProperties()->TrackedDeviceToPropertyContainer(0), vr::Prop_DriverProvidedChaperonePath_String);
    if (driver_chap_path != "") {
        auto driver_res = search_universe(driver_chap_path, target);
        if (driver_res.has_value()) {
            return driver_res.value();
        }
    }

    if (default_chap_path_.has_value()) {
        return search_universe(default_chap_path_.value(), target);
    }
    
    return std::nullopt;
}

std::optional<SlimeVRDriver::UniverseTranslation> SlimeVRDriver::VRDriver::GetCurrentUniverse() {
    if (current_universe.has_value()) {
        return current_universe.value().second;
    } else {
        return std::nullopt;
    }
}