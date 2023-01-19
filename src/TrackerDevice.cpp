#include "TrackerDevice.hpp"

SlimeVRDriver::TrackerDevice::TrackerDevice(std::string serial, int deviceId, TrackerRole trackerRole_):
    serial_(serial), trackerRole(trackerRole_), deviceId_(deviceId)
{
    this->last_pose_ = MakeDefaultPose();
    this->isSetup = false;
}

std::string SlimeVRDriver::TrackerDevice::GetSerial()
{
    return this->serial_;
}

void SlimeVRDriver::TrackerDevice::Update()
{
    if (this->device_index_ == vr::k_unTrackedDeviceIndexInvalid)
        return;

    // Check if this device was asked to be identified
    auto events = GetDriver()->GetOpenVREvents();
    for (auto event : events) {
        // Note here, event.trackedDeviceIndex does not necessarily equal this->device_index_, not sure why, but the component handle will match so we can just use that instead
        //if (event.trackedDeviceIndex == this->device_index_) {
        if (event.eventType == vr::EVREventType::VREvent_Input_HapticVibration) {
            if (event.data.hapticVibration.componentHandle == this->haptic_component_) {
                this->did_vibrate_ = true;
            }
        }
        //}
    }

    // Check if we need to keep vibrating
    if (this->did_vibrate_) {
        this->vibrate_anim_state_ += (GetDriver()->GetLastFrameTime().count()/1000.f);
        if (this->vibrate_anim_state_ > 1.0f) {
            this->did_vibrate_ = false;
            this->vibrate_anim_state_ = 0.0f;
        }
    }
}

void SlimeVRDriver::TrackerDevice::PositionMessage(messages::Position &position)
{
    if (this->device_index_ == vr::k_unTrackedDeviceIndexInvalid)
        return;

    // Setup pose for this frame
    auto pose = this->last_pose_;
    //send the new position and rotation from the pipe to the tracker object
    if(position.has_x()) {
        pose.vecPosition[0] = position.x();
        pose.vecPosition[1] = position.y();
        pose.vecPosition[2] = position.z();
    }

    pose.qRotation.w = position.qw();
    pose.qRotation.x = position.qx();
    pose.qRotation.y = position.qy();
    pose.qRotation.z = position.qz();

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

    // Post pose
    GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(this->device_index_, pose, sizeof(vr::DriverPose_t));
    this->last_pose_ = pose;
}

void SlimeVRDriver::TrackerDevice::StatusMessage(messages::TrackerStatus &status)
{
    auto pose = this->last_pose_;
    switch (status.status())
    {
    case messages::TrackerStatus_Status_OK:
        pose.deviceIsConnected = true;
        pose.poseIsValid = true;
        break;
    case messages::TrackerStatus_Status_DISCONNECTED:
        pose.deviceIsConnected = false;
        pose.poseIsValid = false;
        break;
    default:
    case messages::TrackerStatus_Status_ERROR:
    case messages::TrackerStatus_Status_BUSY:
        pose.deviceIsConnected = true;
        pose.poseIsValid = false;
        break;
    }

    // TODO: send position/rotation of 0 instead of last pose?

    GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(this->device_index_, pose, sizeof(vr::DriverPose_t));

    // TODO: update this->last_pose_?
}

DeviceType SlimeVRDriver::TrackerDevice::GetDeviceType()
{
    return DeviceType::TRACKER;
}

vr::TrackedDeviceIndex_t SlimeVRDriver::TrackerDevice::GetDeviceIndex()
{
    return this->device_index_;
}

vr::EVRInitError SlimeVRDriver::TrackerDevice::Activate(uint32_t unObjectId)
{
    this->device_index_ = unObjectId;

    GetDriver()->Log("Activating tracker " + this->serial_);

    // Get the properties handle
    auto props = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(this->device_index_);

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
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReady_String, "{slimevr}/icons/tracker_status_ready.b4bfb144.png");

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceOff_String, "{slimevr}/icons/tracker_status_off.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearching_String, "{slimevr}/icons/tracker_status_ready.b4bfb144.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{slimevr}/icons/tracker_status_ready_alert.b4bfb144.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{slimevr}/icons/tracker_status_ready_alert.b4bfb144.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceNotReady_String, "{slimevr}/icons/tracker_status_off.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceStandby_String, "{slimevr}/icons/tracker_status_standby.b4bfb144.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceAlertLow_String, "{slimevr}/icons/tracker_status_ready_low.b4bfb144.png");

    // Note: icons not currently being loaded. Instead, "placeholder_tracker_status.png" is used.

    // Automatically select vive tracker roles and set hints for games that need it (Beat Saber avatar mod, for example)
    auto roleHint = getViveRoleHint(trackerRole);
    if(roleHint != "")
	    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ControllerType_String, roleHint.c_str());

    auto role = getViveRole(trackerRole);
    if(role != "")
        vr::VRSettings()->SetString(vr::k_pch_Trackers_Section, ("/devices/slimevr/" + this->serial_).c_str(), role.c_str());

    return vr::EVRInitError::VRInitError_None;
}

void SlimeVRDriver::TrackerDevice::Deactivate()
{
    this->device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

void SlimeVRDriver::TrackerDevice::EnterStandby()
{
}

void* SlimeVRDriver::TrackerDevice::GetComponent(const char* pchComponentNameAndVersion)
{
    return nullptr;
}

void SlimeVRDriver::TrackerDevice::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1)
        pchResponseBuffer[0] = 0;
}

vr::DriverPose_t SlimeVRDriver::TrackerDevice::GetPose()
{
    return last_pose_;
}

int SlimeVRDriver::TrackerDevice::getDeviceId()
{
    return deviceId_;
}
