#pragma once
#include <thread>
#include <mutex>
#include <format>
#include <string>
#include <iostream>
#include <openvr_driver.h>

class Logger {
public:
    Logger() : name_("") { }
    Logger(const std::string& name) : name_(name) { }
    template<typename... Args>
    void Log(const std::format_string<Args...> format_str, Args&&... args) {
        std::string message = std::vformat(format_str.get(), std::make_format_args(args...));
        std::string prefixed = name_.length() ? std::format("{}: {}", name_, message) : message;
        std::lock_guard<std::mutex> lock(mutex_);
        LogImpl(prefixed.c_str());
    };
protected:
    virtual void LogImpl(const char* string) = 0;
    std::string name_;
    std::mutex mutex_;
};

class NullLogger: public Logger {
    using Logger::Logger;
protected:
    void LogImpl(const char* message) override {};
};

class ConsoleLogger: public Logger {
    using Logger::Logger;
protected:
    void LogImpl(const char* message) override {
        std::cout << message << '\n' << std::flush;
    }
};

class VRLogger: public Logger {
    using Logger::Logger;
protected:
    void LogImpl(const char* message) override {
        vr::VRDriverLog()->Log(message);
    }
};
