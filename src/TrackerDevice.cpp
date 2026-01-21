#include "TrackerDevice.hpp"
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

SlimeVRDriver::TrackerDevice::TrackerDevice(std::string serial, int device_id, TrackerRole tracker_role) :
	serial_(serial),
	tracker_role_(tracker_role),
	device_id_(device_id),
	is_left_hand_(tracker_role_ == TrackerRole::LEFT_CONTROLLER || tracker_role_ == TrackerRole::LEFT_HAND),
	is_right_hand_(tracker_role_ == TrackerRole::RIGHT_CONTROLLER || tracker_role_ == TrackerRole::RIGHT_HAND),
	fingertracking_enabled_(is_left_hand_ || is_right_hand_),
	is_controller_(tracker_role_ == TrackerRole::LEFT_CONTROLLER || tracker_role_ == TrackerRole::RIGHT_CONTROLLER || tracker_role_ == TrackerRole::LEFT_HAND || tracker_role_ == TrackerRole::RIGHT_HAND),
	last_pose_(MakeDefaultPose()),
	last_pose_atomic_(MakeDefaultPose())
{
}

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

void SlimeVRDriver::TrackerDevice::PositionMessage(messages::Position& position) {
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

	bool double_tap = false;
	bool triple_tap = false;

	//if (fingertracking_enabled_) {
	//	// Set finger rotations
	//	vr::VRBoneTransform_t finger_skeleton_[31]{};
	//	for (int i = 0; i < position.finger_bone_rotations_size(); i++)
	//	{
	//		// Get data from protobuf
	//		auto fingerData = position.finger_bone_rotations(i);
	//		int fingerBoneName = static_cast<int>(fingerData.name());

	//		// Map from our 15 bones to OpenVR's 31 bones
	//		int boneIndex = protobuf_fingers_to_openvr[fingerBoneName];
	//		finger_skeleton_[boneIndex].orientation = {
	//			fingerData.w(),
	//			fingerData.x(),
	//			fingerData.y(),
	//			fingerData.z()
	//		};
	//	}

	//	// Update the finger skeleton for this hand. With and without controller have the same pose.
	//	vr::VRDriverInput()->UpdateSkeletonComponent(skeletal_component_handle_, vr::VRSkeletalMotionRange_WithController, finger_skeleton_, 31);
	//	vr::VRDriverInput()->UpdateSkeletonComponent(skeletal_component_handle_, vr::VRSkeletalMotionRange_WithoutController, finger_skeleton_, 31);
	//}

	pose.deviceIsConnected = true;
	pose.poseIsValid = true;
	pose.result = vr::ETrackingResult::TrackingResult_Running_OK;

	// Set inputs
	vr::VRDriverInput()->UpdateBooleanComponent(this->double_tap_component_, double_tap, 0);
	vr::VRDriverInput()->UpdateBooleanComponent(this->triple_tap_component_, triple_tap, 0);

	// Notify SteamVR that pose was updated
	last_pose_atomic_ = (last_pose_ = pose);

	GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(device_index_, pose, sizeof(vr::DriverPose_t));
}
void SlimeVRDriver::TrackerDevice::ControllerInputMessage(messages::ControllerInput& controllerInput) {
	if (was_activated_) {
		// Get inputs from protobuf
		LogInput("Check handle for trigger before update.", this->trigger_component_);
		vr::VRDriverInput()->UpdateScalarComponent(this->trigger_component_, controllerInput.trigger(), 0);

		LogInput("Check handle for trigger touch before update.", this->trigger_component_touch_);
		vr::VRDriverInput()->UpdateBooleanComponent(this->trigger_component_touch_, controllerInput.trigger() > 0.5f, 0);

		LogInput("Check handle for grip before update.", this->grip_value_component_);
		vr::VRDriverInput()->UpdateScalarComponent(this->grip_value_component_, controllerInput.grip(), 0);

		LogInput("Check handle for stick x before update.", this->stick_x_component_);
		vr::VRDriverInput()->UpdateScalarComponent(this->stick_x_component_, controllerInput.thumbstick_x(), 0);

		LogInput("Check handle for stick y before update.", this->stick_y_component_);
		vr::VRDriverInput()->UpdateScalarComponent(this->stick_y_component_, controllerInput.thumbstick_y(), 0);

		LogInput("Check handle for button a before update.", this->button_a_component_);
		vr::VRDriverInput()->UpdateBooleanComponent(this->button_a_component_, controllerInput.button_1(), 0);

		LogInput("Check handle for button b before update.", this->button_b_component_);
		vr::VRDriverInput()->UpdateBooleanComponent(this->button_b_component_, controllerInput.button_2(), 0);

		LogInput("Check handle for stick click before update.", this->stick_click_component_);
		vr::VRDriverInput()->UpdateBooleanComponent(this->stick_click_component_, controllerInput.stick_click(), 0);

		LogInput("Check handle for system before update.", this->system_component);
		vr::VRDriverInput()->UpdateBooleanComponent(this->system_component, controllerInput.menu_recenter(), 0);

		LogInput("Check handle for system touch before update.", this->system_component_touch);
		vr::VRDriverInput()->UpdateBooleanComponent(this->system_component_touch, controllerInput.menu_recenter(), 0);
	}
}
void SlimeVRDriver::TrackerDevice::BatteryMessage(messages::Battery& battery) {
	if (this->device_index_ == vr::k_unTrackedDeviceIndexInvalid)
		return;

	// Get the properties handle
	auto containerHandle_ = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(this->device_index_);

	vr::ETrackedPropertyError err;

	// Set that the tracker reports battery level in case it has not already been set to true
	// It's a given that the tracker supports reporting battery life because otherwise a BatteryMessage would not be received
	if (vr::VRProperties()->GetBoolProperty(containerHandle_, vr::Prop_DeviceProvidesBatteryStatus_Bool, &err) != true) {
		vr::VRProperties()->SetBoolProperty(containerHandle_, vr::Prop_DeviceProvidesBatteryStatus_Bool, true);
	}

	if (battery.is_charging()) {
		vr::VRProperties()->SetBoolProperty(containerHandle_, vr::Prop_DeviceIsCharging_Bool, true);
	}
	else {
		vr::VRProperties()->SetBoolProperty(containerHandle_, vr::Prop_DeviceIsCharging_Bool, false);
	}

	// Set the battery Level; 0 = 0%, 1 = 100%
	vr::VRProperties()->SetFloatProperty(containerHandle_, vr::Prop_DeviceBatteryPercentage_Float, battery.battery_level());
}

void SlimeVRDriver::TrackerDevice::StatusMessage(messages::TrackerStatus& status) {
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
	if (is_controller_) {
		return DeviceType::CONTROLLER;
	}
	return DeviceType::TRACKER;
}


vr::TrackedDeviceIndex_t SlimeVRDriver::TrackerDevice::GetDeviceIndex() {
	return device_index_;
}

vr::EVRInitError SlimeVRDriver::TrackerDevice::Activate(uint32_t unObjectId) {
	device_index_ = unObjectId;

	logger_->Log("Activating tracker %s", serial_.c_str());

	const std::string log_dir = "C:\\Temp\\SlimeVRLogs\\";

	// Create directory if it doesn't exist
	try {
		fs::create_directories(log_dir);
	}
	catch (...) {
		// If this fails, we silently continue (driver must not crash)
	}

	// One log file per tracker
	const std::string log_path = log_dir + "input.log";

	input_log_.open(log_path, std::ios::out | std::ios::app);
	if (input_log_.is_open()) {
		input_log_ << "=== Activating tracker " << serial_ << " ===" << std::endl;
	}

	// Get the properties handle
	containerHandle_ = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(device_index_);

	// Set some universe ID (Must be 2 or higher)
	GetDriver()->GetProperties()->SetUint64Property(containerHandle_, vr::Prop_CurrentUniverseId_Uint64, 4);

	// Set up a model "number" (not needed but good to have)
	if (is_controller_) {
		GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_ModelNumber_String, "SlimeVR Virtual Controller");
	}
	else {
		GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_ModelNumber_String, "SlimeVR Virtual Tracker");
	}

	GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_ManufacturerName_String, "SlimeVR");

	//// Hand selection
	if (is_left_hand_) {
		GetDriver()->GetProperties()->SetInt32Property(containerHandle_, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);
	}
	else if (is_right_hand_) {
		GetDriver()->GetProperties()->SetInt32Property(containerHandle_, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_RightHand);
	}
	else {
		GetDriver()->GetProperties()->SetInt32Property(containerHandle_, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_OptOut);
	}

	// Should be treated as controller or as tracker? (Hand = Tracker and Controller = Controller)
	if (is_controller_) {
		vr::VRProperties()->SetInt32Property(containerHandle_, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_Controller);
		vr::VRProperties()->SetStringProperty(containerHandle_, vr::Prop_ControllerType_String, "slimevr_virtual_controller");
		vr::VRProperties()->SetInt32Property(containerHandle_, vr::Prop_ControllerHandSelectionPriority_Int32, 2147483647); // Prioritizes our controller over whatever else.
	}
	else {
		vr::VRProperties()->SetInt32Property(containerHandle_, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_GenericTracker);
	}

	// Set up a render model path (index controllers for controllers and vive trackers 1.0 for trackers)
	std::string model_path;
	if (is_controller_) {
		vr::VRProperties()->SetStringProperty(containerHandle_, vr::Prop_RenderModelName_String, is_right_hand_ ? "{indexcontroller}valve_controller_knu_1_0_right" : "{indexcontroller}valve_controller_knu_1_0_left");
	}
	else {
		GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_RenderModelName_String, "{htc}/rendermodels/vr_tracker_vive_1_0");
	}

	// Set the icons
	GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_NamedIconPathDeviceReady_String, "{slimevr}/icons/tracker_status_ready.png");
	GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_NamedIconPathDeviceOff_String, "{slimevr}/icons/tracker_status_off.png");
	GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_NamedIconPathDeviceSearching_String, "{slimevr}/icons/tracker_status_ready.png");
	GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{slimevr}/icons/tracker_status_ready_alert.png");
	GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{slimevr}/icons/tracker_status_ready_alert.png");
	GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_NamedIconPathDeviceNotReady_String, "{slimevr}/icons/tracker_status_error.png");
	GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_NamedIconPathDeviceStandby_String, "{slimevr}/icons/tracker_status_standby.png");
	GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_NamedIconPathDeviceAlertLow_String, "{slimevr}/icons/tracker_status_ready_low.png");

	// Set inputs
	if (is_controller_) {
		GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_InputProfilePath_String, "{slimevr}/input/slimevr_controller_bindings.json");
		uint64_t supportedButtons = 0xFFFFFFFFFFFFFFFFULL;
		vr::VRProperties()->SetUint64Property(containerHandle_, vr::Prop_SupportedButtons_Uint64, supportedButtons);

		LogInfo("Creating /pose/raw component");
		vr::EVRInputError input_error = vr::VRDriverInput()->CreatePoseComponent(containerHandle_, "/pose/raw", &this->raw_pose_component_handle_);
		LogInputError(input_error, "/pose/raw", this->raw_pose_component_handle_);

		LogInfo("Creating /pose/tip component");
		input_error = vr::VRDriverInput()->CreatePoseComponent(containerHandle_, "/pose/tip", &this->aim_pose_component_handle_);
		LogInputError(input_error, "/pose/tip", this->aim_pose_component_handle_);

		LogInfo("Creating /input/double_tap/click component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/double_tap/click", &this->double_tap_component_);
		LogInputError(input_error, "/input/double_tap/click", this->double_tap_component_);

		LogInfo("Creating /input/triple_tap/click component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/triple_tap/click", &this->triple_tap_component_);
		LogInputError(input_error, "/input/triple_tap/click", this->triple_tap_component_);

		LogInfo("Creating /input/a/click component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/a/click", &this->button_a_component_);
		LogInputError(input_error, "/input/a/click", this->button_a_component_);

		LogInfo("Creating /input/a/touch component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/a/touch", &this->button_a_component_touch_);
		LogInputError(input_error, "/input/a/touch", this->button_a_component_touch_);

		LogInfo("Creating /input/b/click component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/b/click", &this->button_b_component_);
		LogInputError(input_error, "/input/b/click", this->button_b_component_);

		LogInfo("Creating /input/b/touch component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/b/touch", &this->button_b_component_touch_);
		LogInputError(input_error, "/input/b/touch", this->button_b_component_touch_);

		LogInfo("Creating /input/system/click component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/system/click", &this->system_component);
		LogInputError(input_error, "/input/system/click", this->system_component);

		LogInfo("Creating /input/system/touch component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/system/touch", &this->system_component_touch);
		LogInputError(input_error, "/input/system/touch", this->system_component_touch);

		LogInfo("Creating /input/trackpad/click component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/trackpad/click", &this->trackpad_click_component_);
		LogInputError(input_error, "/input/trackpad/click", this->trackpad_click_component_);

		LogInfo("Creating /input/trackpad/touch component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/trackpad/touch", &this->trackpad_touch_component_);
		LogInputError(input_error, "/input/trackpad/touch", this->trackpad_touch_component_);

		LogInfo("Creating /input/joystick/click component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/joystick/click", &this->stick_click_component_);
		LogInputError(input_error, "/input/joystick/click", this->stick_click_component_);

		LogInfo("Creating /input/joystick/touch component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/joystick/touch", &this->stick_click_component_touch_);
		LogInputError(input_error, "/input/joystick/touch", this->stick_click_component_touch_);

		// Scalar components

		LogInfo("Creating /input/trigger/value component");
		input_error = vr::VRDriverInput()->CreateScalarComponent(containerHandle_, "/input/trigger/value", &this->trigger_component_, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
		LogInputError(input_error, "/input/trigger/value", this->trigger_component_);

		LogInfo("Creating /input/trigger/touch component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/trigger/touch", &this->trigger_component_touch_);
		LogInputError(input_error, "/input/trigger/touch", this->trigger_component_touch_);

		LogInfo("Creating /input/trigger/clickcomponent");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/trigger/click", &this->trigger_component_click_);
		LogInputError(input_error, "/input/trigger/click", this->trigger_component_click_);

		LogInfo("Creating /input/grip/value component");
		input_error = vr::VRDriverInput()->CreateScalarComponent(containerHandle_, "/input/grip/value", &this->grip_value_component_, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
		LogInputError(input_error, "/input/grip/value", this->grip_value_component_);

		LogInfo("Creating /input/grip/touch component");
		input_error = vr::VRDriverInput()->CreateBooleanComponent(containerHandle_, "/input/grip/touch", &this->grip_value_component_touch_);
		LogInputError(input_error, "/input/grip/touch", this->grip_value_component_touch_);

		LogInfo("Creating /input/trackpad/x component");
		input_error = vr::VRDriverInput()->CreateScalarComponent(containerHandle_, "/input/trackpad/x", &this->trackpad_x_component_, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
		LogInputError(input_error, "/input/trackpad/x", this->trackpad_x_component_);

		LogInfo("Creating /input/trackpad/y component");
		input_error = vr::VRDriverInput()->CreateScalarComponent(containerHandle_, "/input/trackpad/y", &this->trackpad_y_component_, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
		LogInputError(input_error, "/input/trackpad/y", this->trackpad_y_component_);

		LogInfo("Creating /input/joystick/x component");
		input_error = vr::VRDriverInput()->CreateScalarComponent(containerHandle_, "/input/joystick/x", &this->stick_x_component_, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
		LogInputError(input_error, "/input/joystick/x", this->stick_x_component_);

		LogInfo("Creating /input/joystick/y component");
		input_error = vr::VRDriverInput()->CreateScalarComponent(containerHandle_, "/input/joystick/y", &this->stick_y_component_, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
		LogInputError(input_error, "/input/joystick/y", this->stick_y_component_);

		LogInfo("Creating /output/haptic component");
		input_error = vr::VRDriverInput()->CreateHapticComponent(containerHandle_, "/output/haptic", &this->haptic_component_);
		LogInputError(input_error, "/output/haptic", this->haptic_component_);
	}

	// Automatically select vive tracker roles and set hints for games that need it (Beat Saber avatar mod, for example)
	if (!is_controller_) {
		auto role_hint = GetViveRoleHint(tracker_role_);
		if (role_hint != "") {
			GetDriver()->GetProperties()->SetStringProperty(containerHandle_, vr::Prop_ControllerType_String, role_hint.c_str());
		}

		auto role = GetViveRole(tracker_role_);
		if (role != "") {
			vr::VRSettings()->SetString(vr::k_pch_Trackers_Section, ("/devices/slimevr/" + serial_).c_str(), role.c_str());
		}
	}

	// Setup skeletal input for fingertracking
	if (fingertracking_enabled_) {
		vr::VRDriverInput()->CreateSkeletonComponent(
			containerHandle_,
			is_right_hand_ ? "/input/skeleton/right" : "/input/skeleton/left",
			is_right_hand_ ? "/skeleton/hand/right" : "/skeleton/hand/left",
			"/pose/raw",
			vr::EVRSkeletalTrackingLevel::VRSkeletalTracking_Full,
			NULL, // Fist
			31,
			&skeletal_component_handle_);

		// Update the skeleton so steamvr knows we have an active skeletal input device
		vr::VRBoneTransform_t finger_skeleton_[31]{};
		vr::VRDriverInput()->UpdateSkeletonComponent(skeletal_component_handle_, vr::VRSkeletalMotionRange_WithController, finger_skeleton_, 31);
		vr::VRDriverInput()->UpdateSkeletonComponent(skeletal_component_handle_, vr::VRSkeletalMotionRange_WithoutController, finger_skeleton_, 31);
	}
	was_activated_ = true;
	return vr::EVRInitError::VRInitError_None;
}

void SlimeVRDriver::TrackerDevice::LogInfo(const char* message) {
	if (input_log_.is_open()) {
		input_log_ << "[Info] " << message << std::endl;
		input_log_.flush();
	}
}

void SlimeVRDriver::TrackerDevice::LogInputError(vr::EVRInputError err, const char* path, vr::VRInputComponentHandle_t componentHandle) {
	if (!input_log_.is_open()) return;

	bool validHandle = componentHandle == vr::k_ulInvalidInputComponentHandle

		input_log_ << "["
		<< (err == vr::VRInputError_None ? "Info" : "InputError")
		<< "] "
		<< path
		<< "\r\n Handle: "
		<< componentHandle
		<< "\r\n Handle Is Valid: "
		<< (validHandle ? "true" : "false")
		<< "\r\m Failure Result: "
		<< GetInputErrorName(err)
		<< " (" << err << ")"
		<< std::endl;
	input_log_.flush(); // force write immediately
}
void SlimeVRDriver::TrackerDevice::LogInput(const char* path, vr::VRInputComponentHandle_t componentHandle) {
	if (!input_log_.is_open()) return;

	bool validHandle = componentHandle == vr::k_ulInvalidInputComponentHandle

		input_log_ << "["
		<< "Info")
		<< "] "
		<< path
		<< "\r\n Handle: "
		<< componentHandle
		<< "\r\n Handle Is Valid: "
		<< (validHandle ? "true" : "false")
		<< std::endl;
	input_log_.flush(); // force write immediately
}

const char* SlimeVRDriver::TrackerDevice::GetInputErrorName(vr::EVRInputError err) {
	switch (err) {
	case vr::VRInputError_None: return "None";
	case vr::VRInputError_NameNotFound: return "NameNotFound";
	case vr::VRInputError_WrongType: return "WrongType";
		// Add others as needed
	default: return "Unknown";
	}
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
