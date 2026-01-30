#include "TrackerDevice.hpp"

SlimeVRDriver::TrackerDevice::TrackerDevice(std::string serial, int device_id, TrackerRole tracker_role):
    serial_(serial),
    tracker_role_(tracker_role),
    device_id_(device_id),
    last_pose_(MakeDefaultPose()),
    last_pose_atomic_(MakeDefaultPose())
{ }

std::string SlimeVRDriver::TrackerDevice::GetSerial() {
    return serial_;
}

void SlimeVRDriver::TrackerDevice::Update() {
    if (device_index_ == vr::k_unTrackedDeviceIndexInvalid) return;

    // Check if this device was asked to be identified
    auto events = GetDriver()->GetOpenVREvents();
    for (auto event : events) {
        // Note here, event.trackedDeviceIndex does not necessarily equal device_index_, not sure why, but the component handle will match so we can just use that instead
        //if (event.trackedDeviceIndex == device_index_) {
        if (event.eventType == vr::EVREventType::VREvent_Input_HapticVibration) {
            if (event.data.hapticVibration.componentHandle == haptic_component_) {
                did_vibrate_ = true;
            }
        }
        //}
    }

    // Check if we need to keep vibrating
    if (did_vibrate_) {
        vibrate_anim_state_ += GetDriver()->GetLastFrameTime().count() / 1000.f;
        if (vibrate_anim_state_ > 1.0f) {
            did_vibrate_ = false;
            vibrate_anim_state_ = 0.0f;
        }
    }
}

void SlimeVRDriver::TrackerDevice::PositionMessage(messages::Position &position) {
    if (device_index_ == vr::k_unTrackedDeviceIndexInvalid) return;

    // Setup pose for this frame
    auto pose = last_pose_;
    //send the new position and rotation from the pipe to the tracker object
    if (position.has_x()) {
        pose.vecPosition[0] = position.x();
        pose.vecPosition[1] = position.y();
        pose.vecPosition[2] = position.z();
    }

    pose.qRotation.w = position.qw();
    pose.qRotation.x = position.qx();
    pose.qRotation.y = position.qy();
    pose.qRotation.z = position.qz();

    if (position.has_vx()) { 
        pose.vecVelocity[0] = position.vx(); 
        pose.vecVelocity[1] = position.vy(); 
        pose.vecVelocity[2] = position.vz(); 
    }
    else { // If velocity isn't being sent, don't keep stale values 
        pose.vecVelocity[0] = 0.0f; pose.vecVelocity[1] = 0.0f; pose.vecVelocity[2] = 0.0f; 
    }

    auto current_universe = GetDriver()->GetCurrentUniverse();
    if (current_universe.has_value()) {
        auto trans = current_universe.value();

        // TODO: set this once, somewhere?
        pose.vecWorldFromDriverTranslation[0] = -trans.translation.v[0];
        pose.vecWorldFromDriverTranslation[1] = -trans.translation.v[1];
        pose.vecWorldFromDriverTranslation[2] = -trans.translation.v[2];

        pose.qWorldFromDriverRotation.w = cos(trans.yaw / 2);
        pose.qWorldFromDriverRotation.x = 0;
        pose.qWorldFromDriverRotation.y = sin(trans.yaw / 2);
        pose.qWorldFromDriverRotation.z = 0;
    }

    pose.deviceIsConnected = true;
    pose.poseIsValid = true;
    pose.result = vr::ETrackingResult::TrackingResult_Running_OK;

    // Notify SteamVR that pose was updated
    last_pose_atomic_ = (last_pose_ = pose);
    GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(device_index_, pose, sizeof(vr::DriverPose_t));
}

void SlimeVRDriver::TrackerDevice::BatteryMessage(messages::Battery &battery) {
    if (this->device_index_ == vr::k_unTrackedDeviceIndexInvalid)
        return;

    // Get the properties handle
    auto props = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(this->device_index_);

    vr::ETrackedPropertyError err;

    // Set that the tracker reports battery level in case it has not already been set to true
    // It's a given that the tracker supports reporting battery life because otherwise a BatteryMessage would not be received
    if (vr::VRProperties()->GetBoolProperty(props, vr::Prop_DeviceProvidesBatteryStatus_Bool, &err) != true) {
        vr::VRProperties()->SetBoolProperty(props, vr::Prop_DeviceProvidesBatteryStatus_Bool, true);
    }

    if (battery.is_charging()) {
        vr::VRProperties()->SetBoolProperty(props, vr::Prop_DeviceIsCharging_Bool, true);
    } else {
        vr::VRProperties()->SetBoolProperty(props, vr::Prop_DeviceIsCharging_Bool, false);
    } 
    
    // Set the battery Level; 0 = 0%, 1 = 100%
    vr::VRProperties()->SetFloatProperty(props, vr::Prop_DeviceBatteryPercentage_Float, battery.battery_level());
}

void SlimeVRDriver::TrackerDevice::StatusMessage(messages::TrackerStatus &status) {
    if (device_index_ == vr::k_unTrackedDeviceIndexInvalid) return;
    
    vr::DriverPose_t pose = last_pose_;
    switch (status.status()) {
        case messages::TrackerStatus_Status_OK:
            pose.deviceIsConnected = true;
            pose.poseIsValid = true;
            break;
        case messages::TrackerStatus_Status_DISCONNECTED:
            pose.deviceIsConnected = false;
            pose.poseIsValid = false;
            break;
        case messages::TrackerStatus_Status_ERROR:
        case messages::TrackerStatus_Status_BUSY:
        default:
            pose.deviceIsConnected = true;
            pose.poseIsValid = false;
            break;
    }

    // TODO: send position/rotation of 0 instead of last pose?
    
    last_pose_atomic_ = (last_pose_ = pose);
    GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(device_index_, pose, sizeof(vr::DriverPose_t));
}

DeviceType SlimeVRDriver::TrackerDevice::GetDeviceType() {
    return DeviceType::TRACKER;
}

vr::TrackedDeviceIndex_t SlimeVRDriver::TrackerDevice::GetDeviceIndex() {
    return device_index_;
}

vr::EVRInitError SlimeVRDriver::TrackerDevice::Activate(uint32_t unObjectId) {
    device_index_ = unObjectId;

    logger_->Log("Activating tracker {}", serial_);

    // Get the properties handle
    auto props = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(device_index_);

    // Set some universe ID (Must be 2 or higher)
    GetDriver()->GetProperties()->SetUint64Property(props, vr::Prop_CurrentUniverseId_Uint64, 4);
    
    // Set up a model "number" (not needed but good to have)
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "SlimeVR Virtual Tracker");

    // Opt out of hand selection
    GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_OptOut);
    vr::VRProperties()->SetInt32Property(props, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_GenericTracker);
    vr::VRProperties()->SetInt32Property(props, vr::Prop_ControllerHandSelectionPriority_Int32, -1);

    // Set up a render model path
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_RenderModelName_String, "{htc}/rendermodels/vr_tracker_vive_1_0");

    // Set the icon
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReady_String, "{slimevr}/icons/tracker_status_ready.png");

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceOff_String, "{slimevr}/icons/tracker_status_off.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearching_String, "{slimevr}/icons/tracker_status_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{slimevr}/icons/tracker_status_ready_alert.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{slimevr}/icons/tracker_status_ready_alert.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceNotReady_String, "{slimevr}/icons/tracker_status_error.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceStandby_String, "{slimevr}/icons/tracker_status_standby.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceAlertLow_String, "{slimevr}/icons/tracker_status_ready_low.png");

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_InputProfilePath_String, "{slimevr}/input/slimevr_tracker_profile.json");

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ControllerType_String, "slimevr_tracker");

    auto role = GetViveRole(tracker_role_);
    if (role != "") {
        vr::VRSettings()->SetString(vr::k_pch_Trackers_Section, ("/devices/slimevr/" + serial_).c_str(), role.c_str());
    }

    return vr::EVRInitError::VRInitError_None;
}

void SlimeVRDriver::TrackerDevice::Deactivate() {
    device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

void SlimeVRDriver::TrackerDevice::EnterStandby() {
}

void* SlimeVRDriver::TrackerDevice::GetComponent(const char* pchComponentNameAndVersion) {
    return nullptr;
}

void SlimeVRDriver::TrackerDevice::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) {
    if (unResponseBufferSize >= 1) {
        pchResponseBuffer[0] = 0;
    }
}

vr::DriverPose_t SlimeVRDriver::TrackerDevice::GetPose() {
    return last_pose_atomic_;
}

int SlimeVRDriver::TrackerDevice::GetDeviceId() {
    return device_id_;
}

void SlimeVRDriver::TrackerDevice::SetDeviceId(int device_id) {
    device_id_ = device_id;
}
