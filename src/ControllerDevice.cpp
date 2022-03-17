#include "ControllerDevice.hpp"

SlimeVRDriver::ControllerDevice::ControllerDevice(std::string serial, int deviceId, ControllerDevice::Handedness handedness):
    serial_(serial), handedness_(handedness), deviceId_(deviceId)
{
}

std::string SlimeVRDriver::ControllerDevice::GetSerial()
{
    return this->serial_;
}

void SlimeVRDriver::ControllerDevice::Update()
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

DeviceType SlimeVRDriver::ControllerDevice::GetDeviceType()
{
    return DeviceType::CONTROLLER;
}

SlimeVRDriver::ControllerDevice::Handedness SlimeVRDriver::ControllerDevice::GetHandedness()
{
    return this->handedness_;
}

vr::TrackedDeviceIndex_t SlimeVRDriver::ControllerDevice::GetDeviceIndex()
{
    return this->device_index_;
}

vr::EVRInitError SlimeVRDriver::ControllerDevice::Activate(uint32_t unObjectId)
{
    this->device_index_ = unObjectId;

    GetDriver()->Log("Activating controller " + this->serial_);

    // Get the properties handle
    auto props = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(this->device_index_);

    // Setup inputs and outputs
    GetDriver()->GetInput()->CreateHapticComponent(props, "/output/haptic", &this->haptic_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/a/click", &this->a_button_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/a/touch", &this->a_button_touch_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/b/click", &this->b_button_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/b/touch", &this->b_button_touch_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/trigger/click", &this->trigger_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/trigger/touch", &this->trigger_touch_component_);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/trigger/value", &this->trigger_value_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedOneSided);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/grip/touch", &this->grip_touch_component_);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/grip/value", &this->grip_value_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedOneSided);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/grip/force", &this->grip_force_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedOneSided);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/system/click", &this->system_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/system/touch", &this->system_touch_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/trackpad/click", &this->trackpad_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/trackpad/touch", &this->trackpad_touch_component_); 
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/trackpad/x", &this->trackpad_x_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedTwoSided);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/trackpad/y", &this->trackpad_y_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedTwoSided);
    
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/joystick/click", &this->joystick_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/joystick/touch", &this->joystick_touch_component_);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/joystick/x", &this->joystick_x_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedTwoSided);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/joystick/y", &this->joystick_y_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedTwoSided);

    // Set some universe ID (Must be 2 or higher)
    GetDriver()->GetProperties()->SetUint64Property(props, vr::Prop_CurrentUniverseId_Uint64, 2);
    
    // Set up a model "number" (not needed but good to have)
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "example_controller");

    // Set up a render model path
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_RenderModelName_String, "{example}example_controller");

    // Give SteamVR a hint at what hand this controller is for
    if (this->handedness_ == Handedness::LEFT) {
        GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);
    }
    else if (this->handedness_ == Handedness::RIGHT) {
        GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_RightHand);
    }
    else {
        GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_OptOut);
    }

    // Set controller profile
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_InputProfilePath_String, "{example}/input/example_controller_bindings.json");

    // Change the icon depending on which handedness this controller is using (ANY uses right)
    std::string controller_handedness_str = this->handedness_ == Handedness::LEFT ? "left" : "right";
    std::string controller_ready_file = "{example}/icons/controller_ready_" + controller_handedness_str + ".png";
    std::string controller_not_ready_file = "{example}/icons/controller_not_ready_" + controller_handedness_str + ".png";

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReady_String, controller_ready_file.c_str());

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceOff_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearching_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearchingAlert_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReadyAlert_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceNotReady_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceStandby_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceAlertLow_String, controller_not_ready_file.c_str());

    return vr::EVRInitError::VRInitError_None;
}

void SlimeVRDriver::ControllerDevice::PositionMessage(messages::Position &position)
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

    // Post pose
    GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(this->device_index_, pose, sizeof(vr::DriverPose_t));
    this->last_pose_ = pose;
}

void SlimeVRDriver::ControllerDevice::Deactivate()
{
    this->device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

void SlimeVRDriver::ControllerDevice::EnterStandby()
{
}

void* SlimeVRDriver::ControllerDevice::GetComponent(const char* pchComponentNameAndVersion)
{
    return nullptr;
}

void SlimeVRDriver::ControllerDevice::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1)
        pchResponseBuffer[0] = 0;
}

vr::DriverPose_t SlimeVRDriver::ControllerDevice::GetPose()
{
    return last_pose_;
}

int SlimeVRDriver::ControllerDevice::getDeviceId()
{
    return deviceId_;
}
