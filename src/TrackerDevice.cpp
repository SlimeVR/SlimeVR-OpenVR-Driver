#include "TrackerDevice.hpp"

SlimeVRDriver::TrackerDevice::TrackerDevice(std::string serial, int device_id, TrackerRole tracker_role) :
	serial_(serial),
	tracker_role_(tracker_role),
	device_id_(device_id),
	is_left_hand_(tracker_role_ == TrackerRole::LEFT_CONTROLLER || tracker_role_ == TrackerRole::LEFT_HAND),
	is_right_hand_(tracker_role_ == TrackerRole::RIGHT_CONTROLLER || tracker_role_ == TrackerRole::RIGHT_HAND),
	fingertracking_enabled_(is_left_hand_ || is_right_hand_),
	is_controller_(tracker_role_ == TrackerRole::LEFT_CONTROLLER || tracker_role_ == TrackerRole::RIGHT_CONTROLLER),
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

	if (is_controller_) {
		// Get inputs from protobuf
		bool double_tap = false;
		bool triple_tap = false;
		bool x_pressed = false;
		bool y_pressed = false;
		bool a_pressed = false;
		bool b_pressed = false;
		bool stick_click = false;
		float stick_x = 0.0f;
		float stick_y = 0.0f;
		float trigger = 0.0f;
		float grip = 0.0f;

		for (int i = 0; i < position.input_size(); ++i) {
			const auto& input = position.input(i);
			switch (input.type()) {
			case messages::Input_InputType_DOUBLE_TAP:
				double_tap = true;
				break;
			case messages::Input_InputType_TRIPLE_TAP:
				triple_tap = true;
				break;
			case messages::Input_InputType_TRIGGER:
				trigger = input.value();
				break;
			case messages::Input_InputType_GRIP:
				grip = input.value();
				break;
			case messages::Input_InputType_BUTTON_X:
				x_pressed = true;
				break;
			case messages::Input_InputType_BUTTON_Y:
				y_pressed = true;
				break;
			case messages::Input_InputType_BUTTON_A:
				a_pressed = true;
				break;
			case messages::Input_InputType_BUTTON_B:
				b_pressed = true;
				break;
			case messages::Input_InputType_STICK_X:
				stick_x = input.value();
				break;
			case messages::Input_InputType_STICK_Y:
				stick_y = input.value();
				break;
			case messages::Input_InputType_STICK_CLICK:
				stick_click = true;
				break;
			}
		}

		// Set inputs
		GetDriver()->GetInput()->UpdateBooleanComponent(this->double_tap_component_, double_tap, 0);
		GetDriver()->GetInput()->UpdateBooleanComponent(this->triple_tap_component_, triple_tap, 0);

		if (is_left_hand_) {
			GetDriver()->GetInput()->UpdateScalarComponent(left_trigger_component_, trigger, 0);
			GetDriver()->GetInput()->UpdateScalarComponent(left_grip_value_component_, grip, 0);
			GetDriver()->GetInput()->UpdateScalarComponent(left_stick_x_component_, stick_x, 0);
			GetDriver()->GetInput()->UpdateScalarComponent(left_stick_y_component_, stick_y, 0);
			GetDriver()->GetInput()->UpdateBooleanComponent(button_x_component_, x_pressed, 0);
			GetDriver()->GetInput()->UpdateBooleanComponent(button_y_component_, y_pressed, 0);
			GetDriver()->GetInput()->UpdateBooleanComponent(left_stick_click_component_, stick_click, 0);
		}
		else if (is_right_hand_) {
			GetDriver()->GetInput()->UpdateScalarComponent(right_trigger_component_, trigger, 0);
			GetDriver()->GetInput()->UpdateScalarComponent(right_grip_value_component_, grip, 0);
			GetDriver()->GetInput()->UpdateScalarComponent(right_stick_x_component_, stick_x, 0);
			GetDriver()->GetInput()->UpdateScalarComponent(right_stick_y_component_, stick_y, 0);
			GetDriver()->GetInput()->UpdateBooleanComponent(button_a_component_, a_pressed, 0);
			GetDriver()->GetInput()->UpdateBooleanComponent(button_b_component_, b_pressed, 0);
			GetDriver()->GetInput()->UpdateBooleanComponent(right_stick_click_component_, stick_click, 0);
		}
	}

	if (fingertracking_enabled_) {
		// Set finger rotations
		vr::VRBoneTransform_t finger_skeleton_[31]{};
		for (int i = 0; i < position.finger_bone_rotations_size(); i++)
		{
			// Get data from protobuf
			auto fingerData = position.finger_bone_rotations(i);
			int fingerBoneName = static_cast<int>(fingerData.name());

			// Map from our 15 bones to OpenVR's 31 bones
			int boneIndex = protobuf_fingers_to_openvr[fingerBoneName];
			finger_skeleton_[boneIndex].orientation = {
				fingerData.w(),
				fingerData.x(),
				fingerData.y(),
				fingerData.z()
			};
		}

		// Update the finger skeleton for this hand. With and without controller have the same pose.
		vr::VRDriverInput()->UpdateSkeletonComponent(skeletal_component_handle_, vr::VRSkeletalMotionRange_WithController, finger_skeleton_, 31);
		vr::VRDriverInput()->UpdateSkeletonComponent(skeletal_component_handle_, vr::VRSkeletalMotionRange_WithoutController, finger_skeleton_, 31);
	}

	pose.deviceIsConnected = true;
	pose.poseIsValid = true;
	pose.result = vr::ETrackingResult::TrackingResult_Running_OK;

	// Notify SteamVR that pose was updated
	last_pose_atomic_ = (last_pose_ = pose);
	GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(device_index_, pose, sizeof(vr::DriverPose_t));
}

void SlimeVRDriver::TrackerDevice::BatteryMessage(messages::Battery& battery) {
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
	}
	else {
		vr::VRProperties()->SetBoolProperty(props, vr::Prop_DeviceIsCharging_Bool, false);
	}

	// Set the battery Level; 0 = 0%, 1 = 100%
	vr::VRProperties()->SetFloatProperty(props, vr::Prop_DeviceBatteryPercentage_Float, battery.battery_level());
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
	return DeviceType::TRACKER;
}

vr::TrackedDeviceIndex_t SlimeVRDriver::TrackerDevice::GetDeviceIndex() {
	return device_index_;
}

vr::EVRInitError SlimeVRDriver::TrackerDevice::Activate(uint32_t unObjectId) {
	device_index_ = unObjectId;

	logger_->Log("Activating tracker %s", serial_.c_str());

	// Get the properties handle
	auto props = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(device_index_);

	// Set some universe ID (Must be 2 or higher)
	GetDriver()->GetProperties()->SetUint64Property(props, vr::Prop_CurrentUniverseId_Uint64, 4);

	// Set up a model "number" (not needed but good to have)
	if (is_controller_) {
		GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "SlimeVR Virtual Controller");
	}
	else {
		GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "SlimeVR Virtual Tracker");
	}

	// Hand selection
	if (is_left_hand_) {
		GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);
	}
	else if (is_right_hand_) {
		GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_RightHand);
	}
	else {
		GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_OptOut);
	}

	// Should be treated as controller or as tracker? (Hand = Tracker and Controller = Controller)
	if (is_controller_) {
		vr::VRProperties()->SetInt32Property(props, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_Controller);
		vr::VRProperties()->SetInt32Property(props, vr::Prop_ControllerHandSelectionPriority_Int32, 2147483647); // Prioritizes our controller over whatever else.
	}
	else {
		vr::VRProperties()->SetInt32Property(props, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_GenericTracker);
	}

	// Set up a render model path (index controllers for controllers and vive trackers 1.0 for trackers)
	if (is_controller_) {
		vr::VRProperties()->SetStringProperty(props, vr::Prop_RenderModelName_String, is_right_hand_ ? "{indexcontroller}valve_controller_knu_1_0_right" : "{indexcontroller}valve_controller_knu_1_0_left");
	}
	else {
		GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_RenderModelName_String, "{htc}/rendermodels/vr_tracker_vive_1_0");
	}

	// Set the icons
	GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReady_String, "{slimevr}/icons/tracker_status_ready.png");
	GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceOff_String, "{slimevr}/icons/tracker_status_off.png");
	GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearching_String, "{slimevr}/icons/tracker_status_ready.png");
	GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{slimevr}/icons/tracker_status_ready_alert.png");
	GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{slimevr}/icons/tracker_status_ready_alert.png");
	GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceNotReady_String, "{slimevr}/icons/tracker_status_error.png");
	GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceStandby_String, "{slimevr}/icons/tracker_status_standby.png");
	GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceAlertLow_String, "{slimevr}/icons/tracker_status_ready_low.png");

	// Set inputs
	if (is_controller_) {
		std::string hand_prefix = is_left_hand_ ? "/input/left/" : "/input/right/";
		GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_InputProfilePath_String, "{slimevr}/input/slimevr_controller_bindings.json");

		GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/double_tap/click", &this->double_tap_component_);
		GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/triple_tap/click", &this->triple_tap_component_);

		GetDriver()->GetInput()->CreateBooleanComponent(props, "input/x/click", &button_x_component_);
		GetDriver()->GetInput()->CreateBooleanComponent(props, "input/y/click", &button_y_component_);
		GetDriver()->GetInput()->CreateBooleanComponent(props, "input/a/click", &button_a_component_);
		GetDriver()->GetInput()->CreateBooleanComponent(props, "input/b/click", &button_b_component_);
		GetDriver()->GetInput()->CreateBooleanComponent(props, "input/menu/click", &menu_component_);
		GetDriver()->GetInput()->CreateBooleanComponent(props, "input/system/click", &system_component_);
		GetDriver()->GetInput()->CreateBooleanComponent(props, hand_prefix + "stick/click", &stick_click_component_);
		GetDriver()->GetInput()->CreateBooleanComponent(props, hand_prefix + "grip/click", &grip_click_component_);

		// Scalar components
		GetDriver()->GetInput()->CreateScalarComponent(props, hand_prefix + "trigger/value", &trigger_component_handle_);
		GetDriver()->GetInput()->CreateScalarComponent(props, hand_prefix + "grip/value", &grip_value_component_handle_);
		GetDriver()->GetInput()->CreateScalarComponent(props, hand_prefix + "stick/x", &stick_x_component_handle_);
		GetDriver()->GetInput()->CreateScalarComponent(props, hand_prefix + "stick/y", &stick_y_component_handle_);
	}

	// Automatically select vive tracker roles and set hints for games that need it (Beat Saber avatar mod, for example)
	if (!is_controller_) {
		auto role_hint = GetViveRoleHint(tracker_role_);
		if (role_hint != "") {
			GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ControllerType_String, role_hint.c_str());
		}

		auto role = GetViveRole(tracker_role_);
		if (role != "") {
			vr::VRSettings()->SetString(vr::k_pch_Trackers_Section, ("/devices/slimevr/" + serial_).c_str(), role.c_str());
		}
	}

	// Setup skeletal input for fingertracking
	if (fingertracking_enabled_) {
		vr::VRDriverInput()->CreateSkeletonComponent(
			props,
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
