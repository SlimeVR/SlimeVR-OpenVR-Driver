#pragma once
#include <memory>
#include <vector>
#include <chrono>
#include <variant>
#include <optional>
#include <openvr_driver.h>
#include "IVRDevice.hpp"
#include <simdjson.h>

namespace SlimeVRDriver {
    class UniverseTranslation {
        public:
            // TODO: do we want to store this differently?
            vr::HmdVector3_t translation;
            float yaw;

            static UniverseTranslation parse(simdjson::ondemand::object &obj);
    };

    typedef std::variant<std::monostate, std::string, int, float, bool> SettingsValue;

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
        virtual std::vector<vr::VREvent_t> GetOpenVREvents() = 0;

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
         * @return Value of the key, std::monostate if the value is malformed or missing.
         */
        virtual SettingsValue GetSettingsValue(std::string key) = 0;

        /**
         * Gets the OpenVR VRDriverInput pointer.
         * 
         * @return OpenVR VRDriverInput pointer.
         */
        virtual vr::IVRDriverInput* GetInput() = 0;

        /**
         * Gets the OpenVR VRDriverProperties pointer.
         * 
         * @return OpenVR VRDriverProperties pointer.
         */
        virtual vr::CVRPropertyHelpers* GetProperties() = 0;

        /**
         * Gets the OpenVR VRServerDriverHost pointer.
         * 
         * @return OpenVR VRServerDriverHost pointer.
         */
        virtual vr::IVRServerDriverHost* GetDriverHost() = 0;

        /**
         * Gets the current UniverseTranslation.
         */
        virtual std::optional<UniverseTranslation> GetCurrentUniverse() = 0;

        virtual inline const char* const* GetInterfaceVersions() override {
            return vr::k_InterfaceVersions;
        };

        virtual ~IVRDriver() {}
    };
}