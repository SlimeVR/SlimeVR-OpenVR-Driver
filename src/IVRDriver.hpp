#pragma once
#include "IVRDevice.hpp"
#include <chrono>
#include <memory>
#include <openvr_driver.h>
#include <optional>
#include <simdjson.h>
#include <variant>
#include <vector>


namespace SlimeVRDriver {
class UniverseTranslation {
public:
  // TODO: do we want to store this differently?
  vr::HmdVector3_t translation;
  float yaw;

  static UniverseTranslation parse(simdjson::ondemand::object &obj);
};

typedef std::variant<std::monostate, std::string, int, float, bool>
    SettingsValue;

class IVRDriver : protected vr::IServerTrackedDeviceProvider {
public:
  /**
   * Returns all devices being managed by this driver.
   *
   * @return A vector of shared pointers to all managed devices.
   */
  virtual std::vector<std::shared_ptr<IVRDevice>> GetDevices() = 0;

        /**
         * Returns all OpenVR events that happened on the current frame.
         * 
         * @return A vector of current frame's OpenVR events.
         */
        virtual const std::vector<vr::VREvent_t>& GetOpenVREvents() = 0;

  /**
   * Returns the milliseconds between last frame and this frame.
   *
   * @return Milliseconds between last frame and this frame.
   */
  virtual std::chrono::milliseconds GetLastFrameTime() = 0;

  /**
   * Adds a device to the driver.
   *
   * @param device A shared pointer to the device to be added.
   * @return True on success, false on failure.
   */
  virtual bool AddDevice(std::shared_ptr<IVRDevice> device) = 0;

  /**
   * Returns the value of a settings key.
   *
   * @param key The settings key
   * @return Value of the key, std::monostate if the value is malformed or
   * missing.
   */
  virtual SettingsValue GetSettingsValue(std::string key) = 0;

  /**
   * Gets the OpenVR VRDriverInput pointer.
   *
   * @return OpenVR VRDriverInput pointer.
   */
  virtual vr::IVRDriverInput *GetInput() = 0;

  /**
   * Gets the OpenVR VRDriverProperties pointer.
   *
   * @return OpenVR VRDriverProperties pointer.
   */
  virtual vr::CVRPropertyHelpers *GetProperties() = 0;

  /**
   * Gets the OpenVR VRServerDriverHost pointer.
   *
   * @return OpenVR VRServerDriverHost pointer.
   */
  virtual vr::IVRServerDriverHost *GetDriverHost() = 0;

  /**
   * Gets the current UniverseTranslation.
   */
  virtual std::optional<UniverseTranslation> GetCurrentUniverse() = 0;

  /**
   * Gets the current pose from an external controller (e.g. Virtual Desktop /
   * Steam Link on Quest) for the given hand, if one is connected and tracked.
   * Used to prefer external hand position when in view, falling back to SlimeVR
   * when not.
   */
  virtual std::optional<vr::DriverPose_t>
  GetExternalPoseForHand(bool left_hand) = 0;

  /** Pose lerp speed (0–1) for smoothing VD/SlimeVR transitions. From driver config file. */
  virtual float GetPoseLerpSpeed() = 0;

  virtual inline const char *const *GetInterfaceVersions() override {
    return vr::k_InterfaceVersions;
  };

  virtual ~IVRDriver() {}
};
} // namespace SlimeVRDriver